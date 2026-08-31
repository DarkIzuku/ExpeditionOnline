#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace eo = expedition_online;
namespace proto = expedition_online::protocol;

namespace
{
struct Options
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{7777};
    std::string name{"Probe"};
    std::string zone{"ProbeZone"};
    int duration_seconds{10};
};

auto parse_options(int argc, char** argv) -> Options
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--host" && index + 1 < argc) options.host = argv[++index];
        else if (argument == "--port" && index + 1 < argc) options.port = static_cast<std::uint16_t>(std::stoi(argv[++index]));
        else if (argument == "--name" && index + 1 < argc) options.name = argv[++index];
        else if (argument == "--zone" && index + 1 < argc) options.zone = argv[++index];
        else if (argument == "--duration" && index + 1 < argc) options.duration_seconds = std::stoi(argv[++index]);
        else throw std::runtime_error("unknown or incomplete argument: " + argument);
    }
    return options;
}

auto send_frame(eo::net::SocketHandle socket, const proto::Frame& frame) -> void
{
    const auto bytes = proto::encode_frame(frame);
    std::string error;
    if (!eo::net::send_all(socket, bytes, error)) throw std::runtime_error(error);
}

auto show_frame(const proto::Frame& frame) -> void
{
    std::cout << "RECV " << proto::message_type_name(frame.type) << " seq=" << frame.sequence;
    switch (frame.type)
    {
    case proto::MessageType::welcome:
        std::cout << " player=" << proto::decode_welcome(frame.payload).player_id;
        break;
    case proto::MessageType::zone_state:
    {
        const auto value = proto::decode_zone_state(frame.payload);
        std::cout << " player=" << value.player_id << " zone=" << value.zone;
        break;
    }
    case proto::MessageType::player_joined:
    {
        const auto value = proto::decode_player_joined(frame.payload);
        std::cout << " player=" << value.player_id << " name=" << value.player_name;
        break;
    }
    case proto::MessageType::appearance_state:
    {
        const auto value = proto::decode_appearance_state(frame.payload);
        std::cout << " player=" << value.player_id << " character=" << value.character_class;
        break;
    }
    case proto::MessageType::transform_snapshot:
    {
        const auto value = proto::decode_transform_snapshot(frame.payload);
        std::cout << " player=" << value.player_id << " xyz=" << value.x << ',' << value.y << ',' << value.z;
        break;
    }
    case proto::MessageType::player_left:
        std::cout << " player=" << proto::decode_player_left(frame.payload).player_id;
        break;
    case proto::MessageType::error:
    {
        const auto value = proto::decode_error(frame.payload);
        std::cout << " code=" << value.code << " message=" << value.message;
        break;
    }
    default:
        break;
    }
    std::cout << '\n';
}
} // namespace

auto main(int argc, char** argv) -> int
{
    eo::net::SocketHandle socket = eo::net::kInvalidSocket;
    try
    {
        const auto options = parse_options(argc, argv);
        eo::net::SocketRuntime sockets;
        std::string error;
        socket = eo::net::connect_tcp(options.host, options.port, error);
        if (socket == eo::net::kInvalidSocket) throw std::runtime_error(error);
        eo::net::set_no_delay(socket, true, error);

        std::uint64_t sequence{1};
        send_frame(socket,
                   proto::make_frame(proto::MessageType::hello,
                                     sequence++,
                                     proto::Hello{options.name, "probe-0.2.0"}));
        send_frame(socket,
                   proto::make_frame(proto::MessageType::zone_state,
                                     sequence++,
                                     proto::ZoneState{0, options.zone}));
        send_frame(socket,
                   proto::make_frame(proto::MessageType::appearance_state,
                                     sequence++,
                                     proto::AppearanceState{0, "ProbeCharacter", "ProbeBody", "ProbeHair"}));
        std::cout << "CONNECTED name=" << options.name << " zone=" << options.zone << '\n';

        proto::FrameDecoder decoder;
        std::array<std::uint8_t, 64U * 1024U> buffer{};
        const auto start = std::chrono::steady_clock::now();
        auto next_snapshot = start;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(options.duration_seconds))
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_snapshot)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                const auto position = static_cast<float>(elapsed) / 1000.0F;
                const auto timestamp = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                send_frame(socket,
                           proto::make_frame(proto::MessageType::transform_snapshot,
                                             sequence++,
                                             proto::TransformSnapshot{0, timestamp, position, 2.0F, 3.0F, 0.0F, position, 0.0F}));
                next_snapshot = now + std::chrono::milliseconds(250);
            }

            if (eo::net::wait_readable(socket, 50, error))
            {
                const auto received = eo::net::receive_some(socket, buffer, error);
                if (received <= 0) throw std::runtime_error(error);
                decoder.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
                for (const auto& frame : decoder.take_frames()) show_frame(frame);
            }
            else if (!error.empty())
            {
                throw std::runtime_error(error);
            }
        }

        eo::net::shutdown_socket(socket);
        eo::net::close_socket(socket);
        std::cout << "DONE\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        eo::net::shutdown_socket(socket);
        eo::net::close_socket(socket);
        std::cerr << "FATAL: " << exception.what() << '\n';
        return 1;
    }
}
