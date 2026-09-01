#include <expedition_online/build_info.hpp>
#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace eo = expedition_online;
namespace proto = expedition_online::protocol;

namespace
{
struct Options
{
    std::filesystem::path game_path;
    std::filesystem::path mod_path;
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
};

auto trim(std::string value) -> std::string
{
    const auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

auto lowercase(std::string value) -> std::string
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

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
        if (argument == "--game-path") options.game_path = value();
        else if (argument == "--mod-path") options.mod_path = value();
        else if (argument == "--host") options.host = value();
        else if (argument == "--port")
        {
            const auto parsed = std::stoi(value());
            if (parsed < 1 || parsed > 65535) throw std::runtime_error("port must be in 1..65535");
            options.port = static_cast<std::uint16_t>(parsed);
        }
        else if (argument == "--help" || argument == "-h")
        {
            std::cout << "ExpeditionOnlineDoctor --game-path <Clair Obscur folder> [--host address] [--port number]\n";
            std::exit(0);
        }
        else throw std::runtime_error("unknown argument: " + argument);
    }
    return options;
}

struct ClientSettings
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{7777};
};

auto read_client_settings(const std::filesystem::path& path) -> ClientSettings
{
    ClientSettings settings;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = lowercase(trim(line.substr(0, separator)));
        const auto value = trim(line.substr(separator + 1));
        if (key == "serverhost" || key == "host") settings.host = value;
        else if (key == "serverport" || key == "port")
        {
            const auto parsed = std::stoi(value);
            if (parsed >= 1 && parsed <= 65535) settings.port = static_cast<std::uint16_t>(parsed);
        }
    }
    return settings;
}

auto print_check(bool pass, const std::string& label, const std::string& detail = {}) -> bool
{
    std::cout << (pass ? "[PASS] " : "[FAIL] ") << label;
    if (!detail.empty()) std::cout << " - " << detail;
    std::cout << '\n';
    return pass;
}

auto check_server(const std::string& host, std::uint16_t port, std::string& detail) -> bool
{
    std::string error;
    auto socket = eo::net::connect_tcp(host, port, error);
    if (socket == eo::net::kInvalidSocket)
    {
        detail = error;
        return false;
    }
    try
    {
        const auto hello = proto::make_frame(proto::MessageType::hello,
                                             1,
                                             proto::Hello{"Doctor", eo::build_info::identity("Doctor", proto::kProtocolVersion)});
        const auto bytes = proto::encode_frame(hello);
        if (!eo::net::send_all(socket, bytes, error)) throw std::runtime_error(error);
        if (!eo::net::wait_readable(socket, 2500, error)) throw std::runtime_error(error.empty() ? "server did not answer" : error);
        std::array<std::uint8_t, 4096> buffer{};
        const auto received = eo::net::receive_some(socket, buffer, error);
        if (received <= 0) throw std::runtime_error(error.empty() ? "server closed during Hello" : error);
        proto::FrameDecoder decoder;
        decoder.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
        for (const auto& frame : decoder.take_frames())
        {
            if (frame.version != proto::kProtocolVersion)
                throw std::runtime_error("protocol mismatch: server=" + std::to_string(frame.version) +
                                         " client=" + std::to_string(proto::kProtocolVersion));
            if (frame.type == proto::MessageType::welcome)
            {
                detail = host + ':' + std::to_string(port) + " Protocol " + std::to_string(proto::kProtocolVersion);
                eo::net::shutdown_socket(socket);
                eo::net::close_socket(socket);
                return true;
            }
            if (frame.type == proto::MessageType::error)
                throw std::runtime_error(proto::decode_error(frame.payload).message);
        }
        throw std::runtime_error("Welcome not received");
    }
    catch (const std::exception& exception)
    {
        detail = exception.what();
        eo::net::shutdown_socket(socket);
        eo::net::close_socket(socket);
        return false;
    }
}
} // namespace

auto main(int argc, char** argv) -> int
{
    try
    {
        const auto options = parse_options(argc, argv);
        std::filesystem::path win64;
        std::filesystem::path ue4ss;
        std::filesystem::path mod;
        if (!options.mod_path.empty())
        {
            mod = std::filesystem::absolute(options.mod_path);
            ue4ss = mod.parent_path().parent_path();
            win64 = ue4ss.parent_path();
        }
        else
        {
            if (options.game_path.empty()) throw std::runtime_error("use --game-path with the Clair Obscur installation folder");
            win64 = std::filesystem::absolute(options.game_path) / "Sandfall" / "Binaries" / "Win64";
            ue4ss = win64 / "ue4ss";
            mod = ue4ss / "Mods" / "ExpeditionOnline";
        }

        const auto main_dll = mod / "dlls" / "main.dll";
        const auto config = mod / "config" / "config.ini";
        const auto enabled = mod / "enabled.txt";
        const auto ue4ss_present = std::filesystem::is_directory(ue4ss) &&
                                   (std::filesystem::exists(ue4ss / "UE4SS.dll") ||
                                    std::filesystem::exists(win64 / "UE4SS.dll") ||
                                    std::filesystem::exists(win64 / "dwmapi.dll") ||
                                    std::filesystem::exists(win64 / "xinput1_3.dll"));

        bool ready{true};
        ready &= print_check(std::filesystem::is_directory(win64), "Clair Obscur Win64", win64.string());
        ready &= print_check(ue4ss_present, "UE4SS detected", ue4ss.string());
        ready &= print_check(std::filesystem::exists(main_dll), "ExpeditionOnline main.dll", main_dll.string());
        ready &= print_check(std::filesystem::exists(config), "config.ini", config.string());
        ready &= print_check(std::filesystem::exists(enabled), "ExpeditionOnline enabled", enabled.string());

        auto settings = std::filesystem::exists(config) ? read_client_settings(config) : ClientSettings{};
        if (options.host) settings.host = *options.host;
        if (options.port) settings.port = *options.port;
        eo::net::SocketRuntime sockets;
        std::string server_detail;
        const auto server_ok = check_server(settings.host, settings.port, server_detail);
        ready &= print_check(server_ok, "Server reachable", server_detail);
        ready &= print_check(server_ok, "Protocol compatible",
                             "client=" + std::to_string(proto::kProtocolVersion));

        std::cout << "[INFO] ExpeditionOnline.log: " << (mod / "ExpeditionOnline.log").string() << '\n';
        std::cout << "[INFO] UE4SS.log: " << (win64 / "UE4SS.log").string() << '\n';
        std::cout << "[INFO] " << eo::build_info::identity("Doctor", proto::kProtocolVersion) << '\n';
        std::cout << (ready ? "\nREADY TO PLAY\n" : "\nNOT READY - fix the failed checks above\n");
        return ready ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] Doctor could not run - " << exception.what() << '\n';
        return 1;
    }
}
