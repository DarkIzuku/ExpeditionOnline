#include <expedition_online/build_info.hpp>
#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eo = expedition_online;
namespace proto = expedition_online::protocol;

namespace
{
struct Options
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{7777};
    int server_timeout_seconds{15};
};

auto parse_options(int argc, char** argv) -> Options
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const auto value = [&]() -> std::string {
            if (index + 1 >= argc) throw std::runtime_error("missing value for " + argument);
            return argv[++index];
        };
        if (argument == "--host") options.host = value();
        else if (argument == "--port")
        {
            const auto parsed = std::stoi(value());
            if (parsed < 1 || parsed > 65535) throw std::runtime_error("port must be in 1..65535");
            options.port = static_cast<std::uint16_t>(parsed);
        }
        else if (argument == "--server-timeout")
            options.server_timeout_seconds = std::clamp(std::stoi(value()), 3, 300);
        else if (argument == "--help" || argument == "-h")
        {
            std::cout << "ExpeditionOnlineSelfTest [--host address] [--port number] [--server-timeout seconds]\n";
            std::exit(0);
        }
        else throw std::runtime_error("unknown argument: " + argument);
    }
    return options;
}

auto player_id_of(const proto::Frame& frame) -> std::optional<std::uint64_t>
{
    switch (frame.type)
    {
    case proto::MessageType::welcome: return proto::decode_welcome(frame.payload).player_id;
    case proto::MessageType::zone_state: return proto::decode_zone_state(frame.payload).player_id;
    case proto::MessageType::appearance_state: return proto::decode_appearance_state(frame.payload).player_id;
    case proto::MessageType::transform_snapshot: return proto::decode_transform_snapshot(frame.payload).player_id;
    case proto::MessageType::player_joined: return proto::decode_player_joined(frame.payload).player_id;
    case proto::MessageType::player_left: return proto::decode_player_left(frame.payload).player_id;
    default: return std::nullopt;
    }
}

class TestClient
{
  public:
    TestClient(std::string host, std::uint16_t port, std::string name)
        : host_(std::move(host)), port_(port), name_(std::move(name))
    {
    }

    ~TestClient()
    {
        close();
    }

    auto connect_and_hello() -> void
    {
        std::string error;
        socket_ = eo::net::connect_tcp(host_, port_, error);
        if (socket_ == eo::net::kInvalidSocket) throw std::runtime_error(name_ + " connect failed: " + error);
        eo::net::set_no_delay(socket_, true, error);
        send(proto::make_frame(proto::MessageType::hello,
                               sequence_++,
                               proto::Hello{name_, eo::build_info::identity("SelfTest", proto::kProtocolVersion)}));
        const auto welcome = wait_for(proto::MessageType::welcome, std::nullopt, std::chrono::seconds(3));
        id_ = proto::decode_welcome(welcome.payload).player_id;
    }

    auto id() const noexcept -> std::uint64_t { return id_; }

    auto send(const proto::Frame& frame) -> void
    {
        const auto bytes = proto::encode_frame(frame);
        std::string error;
        if (!eo::net::send_all(socket_, bytes, error)) throw std::runtime_error(name_ + " send failed: " + error);
    }

    template <typename T>
    auto send_message(proto::MessageType type, const T& value) -> void
    {
        send(proto::make_frame(type, sequence_++, value));
    }

    auto send_empty(proto::MessageType type) -> void
    {
        send(proto::Frame{proto::kProtocolVersion, type, sequence_++, {}});
    }

    auto wait_for(proto::MessageType type,
                  std::optional<std::uint64_t> player_id,
                  std::chrono::milliseconds timeout) -> proto::Frame
    {
        const auto matches = [&](const proto::Frame& frame) {
            if (frame.type != type) return false;
            return !player_id || player_id_of(frame) == player_id;
        };
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto found = std::find_if(pending_.begin(), pending_.end(), matches);
            if (found != pending_.end())
            {
                auto result = std::move(*found);
                pending_.erase(found);
                return result;
            }
            receive_once(50);
        }
        throw std::runtime_error(name_ + " timed out waiting for " + proto::message_type_name(type));
    }

    auto close_reset() -> void
    {
        if (socket_ == eo::net::kInvalidSocket) return;
        linger reset_linger{1, 0};
        setsockopt(socket_, SOL_SOCKET, SO_LINGER,
                   reinterpret_cast<const char*>(&reset_linger), sizeof(reset_linger));
        eo::net::close_socket(socket_);
    }

    auto close() -> void
    {
        if (socket_ == eo::net::kInvalidSocket) return;
        eo::net::shutdown_socket(socket_);
        eo::net::close_socket(socket_);
    }

  private:
    auto receive_once(int timeout_ms) -> void
    {
        std::string error;
        if (!eo::net::wait_readable(socket_, timeout_ms, error))
        {
            if (!error.empty()) throw std::runtime_error(name_ + " wait failed: " + error);
            return;
        }
        std::array<std::uint8_t, 64U * 1024U> buffer{};
        const auto received = eo::net::receive_some(socket_, buffer, error);
        if (received <= 0) throw std::runtime_error(name_ + " connection closed: " + error);
        decoder_.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
        for (auto& frame : decoder_.take_frames())
        {
            if (frame.version != proto::kProtocolVersion) throw std::runtime_error("protocol mismatch during self-test");
            if (frame.type == proto::MessageType::ping)
            {
                send(proto::Frame{proto::kProtocolVersion, proto::MessageType::pong, frame.sequence, {}});
                continue;
            }
            pending_.push_back(std::move(frame));
        }
    }

    std::string host_;
    std::uint16_t port_{};
    std::string name_;
    eo::net::SocketHandle socket_{eo::net::kInvalidSocket};
    std::uint64_t sequence_{1};
    std::uint64_t id_{};
    proto::FrameDecoder decoder_;
    std::vector<proto::Frame> pending_;
};

auto verify_malformed_isolation(const Options& options, TestClient& observer) -> void
{
    std::string error;
    auto socket = eo::net::connect_tcp(options.host, options.port, error);
    if (socket == eo::net::kInvalidSocket) throw std::runtime_error("malformed client could not connect: " + error);
    const auto illegal = proto::make_frame(proto::MessageType::zone_state,
                                           1,
                                           proto::ZoneState{999999, "ZoneA"});
    const auto bytes = proto::encode_frame(illegal);
    if (!eo::net::send_all(socket, bytes, error)) throw std::runtime_error("malformed packet send failed");
    if (!eo::net::wait_readable(socket, 2000, error)) throw std::runtime_error("malformed client was not rejected");
    std::array<std::uint8_t, 4096> buffer{};
    const auto received = eo::net::receive_some(socket, buffer, error);
    eo::net::shutdown_socket(socket);
    eo::net::close_socket(socket);
    if (received <= 0) throw std::runtime_error("malformed rejection had no diagnostic frame");
    proto::FrameDecoder decoder;
    decoder.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
    const auto frames = decoder.take_frames();
    if (std::none_of(frames.begin(), frames.end(), [](const auto& frame) {
            return frame.type == proto::MessageType::error;
        }))
        throw std::runtime_error("malformed client did not receive Error");

    TestClient unknown_type(options.host, options.port, "SelfTest-UnknownType");
    unknown_type.connect_and_hello();
    unknown_type.send(proto::Frame{proto::kProtocolVersion,
                                   static_cast<proto::MessageType>(65000),
                                   500,
                                   {}});
    (void)unknown_type.wait_for(proto::MessageType::error, std::nullopt, std::chrono::seconds(2));

    TestClient wrong_protocol(options.host, options.port, "SelfTest-WrongProtocol");
    wrong_protocol.connect_and_hello();
    wrong_protocol.send(proto::Frame{static_cast<std::uint16_t>(proto::kProtocolVersion + 1),
                                     proto::MessageType::ping,
                                     600,
                                     {}});
    (void)wrong_protocol.wait_for(proto::MessageType::error, std::nullopt, std::chrono::seconds(2));

    observer.send_empty(proto::MessageType::ping);
    (void)observer.wait_for(proto::MessageType::pong, std::nullopt, std::chrono::seconds(2));
}

auto run_self_test(const Options& options) -> void
{
    TestClient alice(options.host, options.port, "SelfTest-A");
    alice.connect_and_hello();
    alice.send_message(proto::MessageType::zone_state, proto::ZoneState{999, "ZoneA"});
    alice.send_message(proto::MessageType::appearance_state,
                       proto::AppearanceState{999, "Maelle", "OutfitA", "HairA"});
    alice.send_message(proto::MessageType::transform_snapshot,
                       proto::TransformSnapshot{999, 1000, 100.0F, 200.0F, 300.0F, 0.0F, 45.0F, 0.0F});

    TestClient bob(options.host, options.port, "SelfTest-B");
    bob.connect_and_hello();
    bob.send_message(proto::MessageType::appearance_state,
                     proto::AppearanceState{888, "Sciel", "OutfitB", "HairB"});
    bob.send_message(proto::MessageType::transform_snapshot,
                     proto::TransformSnapshot{888, 1100, 400.0F, 500.0F, 600.0F, 0.0F, 90.0F, 0.0F});
    bob.send_message(proto::MessageType::zone_state, proto::ZoneState{888, "ZoneA"});

    (void)bob.wait_for(proto::MessageType::player_joined, alice.id(), std::chrono::seconds(3));
    (void)bob.wait_for(proto::MessageType::zone_state, alice.id(), std::chrono::seconds(3));
    (void)bob.wait_for(proto::MessageType::appearance_state, alice.id(), std::chrono::seconds(3));
    (void)bob.wait_for(proto::MessageType::transform_snapshot, alice.id(), std::chrono::seconds(3));
    (void)alice.wait_for(proto::MessageType::player_joined, bob.id(), std::chrono::seconds(3));
    (void)alice.wait_for(proto::MessageType::zone_state, bob.id(), std::chrono::seconds(3));
    (void)alice.wait_for(proto::MessageType::appearance_state, bob.id(), std::chrono::seconds(3));
    (void)alice.wait_for(proto::MessageType::transform_snapshot, bob.id(), std::chrono::seconds(3));

    bob.send_message(proto::MessageType::zone_state, proto::ZoneState{bob.id(), "ZoneB"});
    (void)alice.wait_for(proto::MessageType::player_left, bob.id(), std::chrono::seconds(3));
    bob.send_message(proto::MessageType::zone_state, proto::ZoneState{bob.id(), "ZoneA"});
    (void)alice.wait_for(proto::MessageType::player_joined, bob.id(), std::chrono::seconds(3));
    (void)bob.wait_for(proto::MessageType::player_joined, alice.id(), std::chrono::seconds(3));

    const auto old_alice_id = alice.id();
    alice.close_reset();
    (void)bob.wait_for(proto::MessageType::player_left, old_alice_id, std::chrono::seconds(4));

    TestClient reconnected(options.host, options.port, "SelfTest-A-Reconnected");
    reconnected.connect_and_hello();
    if (reconnected.id() == old_alice_id) throw std::runtime_error("reconnect reused a stale session id");
    reconnected.send_message(proto::MessageType::appearance_state,
                             proto::AppearanceState{old_alice_id, "Maelle", "OutfitA2", "HairA2"});
    reconnected.send_message(proto::MessageType::transform_snapshot,
                             proto::TransformSnapshot{old_alice_id, 2000, 700.0F, 800.0F, 900.0F, 0.0F, 0.0F, 0.0F});
    reconnected.send_message(proto::MessageType::zone_state,
                             proto::ZoneState{old_alice_id, "ZoneA"});
    (void)bob.wait_for(proto::MessageType::player_joined, reconnected.id(), std::chrono::seconds(3));

    verify_malformed_isolation(options, bob);
    reconnected.close();

    TestClient silent(options.host, options.port, "SelfTest-Timeout");
    silent.connect_and_hello();
    silent.send_message(proto::MessageType::zone_state, proto::ZoneState{0, "ZoneA"});
    (void)bob.wait_for(proto::MessageType::player_joined, silent.id(), std::chrono::seconds(3));
    (void)bob.wait_for(proto::MessageType::player_left,
                       silent.id(),
                       std::chrono::seconds(options.server_timeout_seconds + 5));
}
} // namespace

auto main(int argc, char** argv) -> int
{
    try
    {
        const auto options = parse_options(argc, argv);
        eo::net::SocketRuntime sockets;
        run_self_test(options);
        std::cout << "SELF_TEST PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "SELF_TEST FAIL: " << exception.what() << '\n';
        return 1;
    }
}
