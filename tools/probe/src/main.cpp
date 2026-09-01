#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <array>
#include <chrono>
#include <cmath>
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
    float x{};
    float y{};
    float z{};
    float yaw{};
    float radius{300.0F};
    float angular_speed{1.0F};
    int snapshot_hz{4};
    bool show_help{};
};

auto print_usage() -> void
{
    std::cout
        << "ExpeditionOnlineProbe - simulated exploration client\n\n"
        << "Usage:\n"
        << "  ExpeditionOnlineProbe.exe [options]\n\n"
        << "Options:\n"
        << "  --host <address>    Server address (default: 127.0.0.1)\n"
        << "  --port <port>       Server TCP port (default: 7777)\n"
        << "  --name <name>       Simulated player name (default: Probe)\n"
        << "  --zone <zone>       Exact LOCAL_ZONE value (default: ProbeZone)\n"
        << "  --duration <sec>    Test duration in seconds (default: 10)\n"
        << "  --x <float>         Circle center X from LOCAL_TRANSFORM (default: 0)\n"
        << "  --y <float>         Circle center Y from LOCAL_TRANSFORM (default: 0)\n"
        << "  --z <float>         Fixed Z from LOCAL_TRANSFORM (default: 0)\n"
        << "  --yaw <float>       Fixed yaw in degrees (default: 0)\n"
        << "  --radius <float>    Circular movement radius (default: 300)\n"
        << "  --angular-speed <float>  Circle speed in radians/sec (default: 1)\n"
        << "  --snapshot-hz <rate> Snapshot send rate in Hz (default: 4)\n"
        << "  --help              Show this help\n";
}

auto parse_options(int argc, char** argv) -> Options
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const auto next_value = [&]() -> std::string {
            if (index + 1 >= argc) throw std::runtime_error("missing value for argument: " + argument);
            return argv[++index];
        };
        if (argument == "--help" || argument == "-h") options.show_help = true;
        else if (argument == "--host") options.host = next_value();
        else if (argument == "--port")
        {
            const auto port = std::stoi(next_value());
            if (port < 1 || port > 65535) throw std::runtime_error("port must be in 1..65535");
            options.port = static_cast<std::uint16_t>(port);
        }
        else if (argument == "--name") options.name = next_value();
        else if (argument == "--zone") options.zone = next_value();
        else if (argument == "--duration") options.duration_seconds = std::stoi(next_value());
        else if (argument == "--x") options.x = std::stof(next_value());
        else if (argument == "--y") options.y = std::stof(next_value());
        else if (argument == "--z") options.z = std::stof(next_value());
        else if (argument == "--yaw") options.yaw = std::stof(next_value());
        else if (argument == "--radius") options.radius = std::stof(next_value());
        else if (argument == "--angular-speed") options.angular_speed = std::stof(next_value());
        else if (argument == "--snapshot-hz") options.snapshot_hz = std::stoi(next_value());
        else throw std::runtime_error("unknown or incomplete argument: " + argument);
    }
    if (options.duration_seconds < 1) throw std::runtime_error("duration must be at least 1 second");
    if (!std::isfinite(options.x) || !std::isfinite(options.y) || !std::isfinite(options.z) ||
        !std::isfinite(options.yaw) || !std::isfinite(options.radius) || !std::isfinite(options.angular_speed))
    {
        throw std::runtime_error("position, yaw and radius must be finite numbers");
    }
    if (options.radius < 0.0F) throw std::runtime_error("radius must be zero or greater");
    if (options.snapshot_hz < 1 || options.snapshot_hz > 60)
    {
        throw std::runtime_error("snapshot-hz must be in 1..60");
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
        std::cout << " player=" << value.player_id << " xyz=" << value.x << ',' << value.y << ',' << value.z
                  << " yaw=" << value.yaw;
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
        if (options.show_help)
        {
            print_usage();
            return 0;
        }
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
        std::cout << "CONNECTED name=" << options.name << " zone=" << options.zone
                  << " base=" << options.x << ',' << options.y << ',' << options.z
                  << " yaw=" << options.yaw << " radius=" << options.radius
                  << " angular_speed=" << options.angular_speed
                  << " snapshot_hz=" << options.snapshot_hz << '\n';

        proto::FrameDecoder decoder;
        std::array<std::uint8_t, 64U * 1024U> buffer{};
        const auto start = std::chrono::steady_clock::now();
        auto next_snapshot = start;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(options.duration_seconds))
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_snapshot)
            {
                const auto elapsed = std::chrono::duration<float>(now - start).count();
                const auto angle = elapsed * options.angular_speed;
                const auto x = options.x + std::cos(angle) * options.radius;
                const auto y = options.y + std::sin(angle) * options.radius;
                const auto timestamp = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                send_frame(socket,
                           proto::make_frame(proto::MessageType::transform_snapshot,
                                             sequence++,
                                             proto::TransformSnapshot{0, timestamp, x, y, options.z, 0.0F, options.yaw, 0.0F}));
                next_snapshot = now + std::chrono::milliseconds(1000 / options.snapshot_hz);
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
