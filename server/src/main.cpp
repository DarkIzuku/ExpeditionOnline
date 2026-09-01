#include <expedition_online/build_info.hpp>
#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eo = expedition_online;
namespace proto = expedition_online::protocol;

namespace
{
struct ServerConfig
{
    std::string bind_host{"0.0.0.0"};
    std::uint16_t port{7777};
    int max_players{8};
    int heartbeat_interval_seconds{5};
    int client_timeout_seconds{15};
    int max_frames_per_second{120};
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

auto steady_ms() noexcept -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

auto load_config(const std::filesystem::path& path) -> ServerConfig
{
    ServerConfig config;
    std::ifstream input(path);
    if (!input) return config;

    std::string line;
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = lowercase(trim(line.substr(0, separator)));
        const auto value = trim(line.substr(separator + 1));
        if (key == "bind_host" || key == "bindaddress") config.bind_host = value;
        else if (key == "port")
        {
            const auto parsed = std::stoi(value);
            if (parsed < 1 || parsed > 65535) throw std::runtime_error("port must be in 1..65535");
            config.port = static_cast<std::uint16_t>(parsed);
        }
        else if (key == "max_clients" || key == "maxplayers")
            config.max_players = std::clamp(std::stoi(value), 1, 1024);
        else if (key == "heartbeatintervalseconds" || key == "heartbeatinterval" ||
                 key == "heartbeat_interval_seconds")
            config.heartbeat_interval_seconds = std::clamp(std::stoi(value), 1, 60);
        else if (key == "clienttimeoutseconds" || key == "clienttimeout" || key == "client_timeout_seconds")
            config.client_timeout_seconds = std::clamp(std::stoi(value), 3, 300);
        else if (key == "maxframespersecond" || key == "max_frames_per_second")
            config.max_frames_per_second = std::clamp(std::stoi(value), 10, 1000);
    }
    if (config.client_timeout_seconds <= config.heartbeat_interval_seconds)
        throw std::runtime_error("ClientTimeout must be greater than HeartbeatInterval");
    return config;
}

class Logger
{
  public:
    explicit Logger(const std::filesystem::path& path) : file_(path, std::ios::app) {}

    auto write(const std::string& message) -> void
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        std::ostringstream line;
        line << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "] " << message;
        std::lock_guard lock(mutex_);
        std::cout << line.str() << std::endl;
        if (file_)
        {
            file_ << line.str() << '\n';
            file_.flush();
        }
    }

  private:
    std::mutex mutex_;
    std::ofstream file_;
};

struct Session
{
    std::uint64_t id{};
    eo::net::SocketHandle socket{eo::net::kInvalidSocket};
    std::string peer;
    std::string player_name;
    std::string zone;
    std::optional<proto::AppearanceState> appearance;
    std::optional<proto::TransformSnapshot> transform;
    std::optional<proto::MovementState> movement;
    std::atomic_bool alive{true};
    std::atomic_bool greeted{};
    std::atomic_bool removed{};
    std::atomic<std::int64_t> last_activity_ms{steady_ms()};
    std::atomic<std::int64_t> last_ping_ms{steady_ms()};
    std::chrono::steady_clock::time_point frame_window_start{std::chrono::steady_clock::now()};
    int frames_in_window{};
    std::mutex send_mutex;
};

struct Delivery
{
    std::shared_ptr<Session> target;
    proto::Frame frame;
};

class Server
{
  public:
    Server(ServerConfig config, Logger& logger) : config_(std::move(config)), logger_(logger) {}

    auto run() -> int
    {
        std::string error;
        listener_ = eo::net::create_listener(config_.bind_host, config_.port, config_.max_players, error);
        if (listener_ == eo::net::kInvalidSocket)
        {
            logger_.write("ERROR listener startup failed: " + error);
            return 1;
        }

        logger_.write("SERVER_READY address=" + config_.bind_host + ':' + std::to_string(config_.port) +
                      " max_players=" + std::to_string(config_.max_players) + " " +
                      eo::build_info::identity("Server", proto::kProtocolVersion));
        watchdog_ = std::thread([this] { watchdog_loop(); });

        while (!stopping_)
        {
            std::string peer;
            auto socket = eo::net::accept_client(listener_, peer, error);
            if (socket == eo::net::kInvalidSocket)
            {
                if (!stopping_) logger_.write("WARN accept failed: " + error);
                continue;
            }

            std::shared_ptr<Session> session;
            {
                std::lock_guard lock(state_mutex_);
                if (sessions_.size() >= static_cast<std::size_t>(config_.max_players))
                {
                    logger_.write("WARN SERVER_FULL peer=" + peer);
                    eo::net::shutdown_socket(socket);
                    eo::net::close_socket(socket);
                    continue;
                }
                session = std::make_shared<Session>();
                session->id = next_player_id_++;
                session->socket = socket;
                session->peer = peer;
                session->last_activity_ms = steady_ms();
                session->last_ping_ms = steady_ms();
                sessions_.emplace(session->id, session);
            }

            eo::net::set_no_delay(socket, true, error);
            logger_.write("PLAYER_CONNECTED id=" + std::to_string(session->id) + " peer=" + peer);
            workers_.emplace_back([this, session] { client_loop(session); });
        }

        stop_all_sessions();
        if (watchdog_.joinable()) watchdog_.join();
        for (auto& worker : workers_)
        {
            if (worker.joinable()) worker.join();
        }
        logger_.write("SERVER_STOPPED");
        return 0;
    }

    auto stop() -> void
    {
        if (stopping_.exchange(true)) return;
        eo::net::shutdown_socket(listener_);
        eo::net::close_socket(listener_);
        stop_all_sessions();
    }

  private:
    auto client_loop(const std::shared_ptr<Session>& session) -> void
    {
        proto::FrameDecoder decoder;
        std::array<std::uint8_t, 64U * 1024U> buffer{};
        std::string error;

        try
        {
            while (!stopping_ && session->alive)
            {
                const auto received = eo::net::receive_some(session->socket, buffer, error);
                if (received <= 0)
                {
                    if (decoder.buffered_bytes() != 0) error = "truncated frame at connection close";
                    break;
                }
                session->last_activity_ms = steady_ms();
                decoder.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
                for (auto& frame : decoder.take_frames())
                {
                    enforce_rate_limit(session);
                    if (frame.version != proto::kProtocolVersion)
                    {
                        logger_.write("PROTOCOL_MISMATCH id=" + std::to_string(session->id) +
                                      " client=" + std::to_string(frame.version) +
                                      " server=" + std::to_string(proto::kProtocolVersion));
                        send_error(session, 1001, "Protocol mismatch. Install matching ExpeditionOnline builds.");
                        throw proto::ProtocolError("protocol version mismatch");
                    }
                    if (!proto::is_known_message_type(frame.type))
                    {
                        send_error(session, 1003, "unknown MessageType");
                        throw proto::ProtocolError("unknown MessageType");
                    }
                    if (proto::is_empty_payload_message(frame.type) && !frame.payload.empty())
                    {
                        send_error(session, 1004, "heartbeat payload must be empty");
                        throw proto::ProtocolError("malformed heartbeat payload");
                    }
                    handle_frame(session, frame);
                }
            }
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }

        disconnect(session, error.empty() ? "connection ended" : error);
    }

    auto enforce_rate_limit(const std::shared_ptr<Session>& session) -> void
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - session->frame_window_start >= std::chrono::seconds(1))
        {
            session->frame_window_start = now;
            session->frames_in_window = 0;
        }
        if (++session->frames_in_window > config_.max_frames_per_second)
        {
            send_error(session, 1005, "message rate limit exceeded");
            throw proto::ProtocolError("basic flood limit exceeded");
        }
    }

    auto handle_frame(const std::shared_ptr<Session>& session, const proto::Frame& frame) -> void
    {
        if (!session->greeted)
        {
            if (frame.type != proto::MessageType::hello)
            {
                send_error(session, 1002, "Hello must be the first message");
                throw proto::ProtocolError("message before Hello");
            }
            const auto hello = proto::decode_hello(frame.payload);
            if (hello.player_name.size() > 64 || hello.client_build.size() > 512)
                throw proto::ProtocolError("Hello fields exceed server limits");
            session->player_name = hello.player_name.empty() ? ("Player" + std::to_string(session->id)) : hello.player_name;
            session->greeted = true;
            send(session,
                 proto::make_frame(proto::MessageType::welcome,
                                   next_server_sequence_++,
                                   proto::Welcome{session->id}));
            logger_.write("PLAYER_READY id=" + std::to_string(session->id) + " name=" + session->player_name +
                          " build=" + hello.client_build);
            return;
        }

        switch (frame.type)
        {
        case proto::MessageType::zone_state:
            handle_zone(session, proto::decode_zone_state(frame.payload));
            break;
        case proto::MessageType::appearance_state:
            handle_appearance(session, proto::decode_appearance_state(frame.payload), frame.sequence);
            break;
        case proto::MessageType::transform_snapshot:
            handle_transform(session, proto::decode_transform_snapshot(frame.payload), frame.sequence);
            break;
        case proto::MessageType::movement_state:
            handle_movement(session, proto::decode_movement_state(frame.payload), frame.sequence);
            break;
        case proto::MessageType::ping:
            send(session, proto::Frame{proto::kProtocolVersion, proto::MessageType::pong, frame.sequence, {}});
            break;
        case proto::MessageType::pong: break;
        default:
            send_error(session, 1003, std::string("client message not allowed: ") + proto::message_type_name(frame.type));
            throw proto::ProtocolError("client sent a server-only message");
        }
    }

    auto handle_zone(const std::shared_ptr<Session>& session, proto::ZoneState state) -> void
    {
        if (state.zone.size() > 256) throw proto::ProtocolError("zone exceeds server limit");
        state.player_id = session->id;
        std::vector<Delivery> deliveries;
        std::string old_zone;
        {
            std::lock_guard lock(state_mutex_);
            old_zone = session->zone;
            if (old_zone == state.zone) return;

            if (!old_zone.empty())
                append_zone_deliveries(deliveries,
                                       old_zone,
                                       session->id,
                                       proto::make_frame(proto::MessageType::player_left,
                                                         next_server_sequence_++,
                                                         proto::PlayerLeft{session->id}));

            session->zone = state.zone;
            const auto own_zone = proto::make_frame(proto::MessageType::zone_state,
                                                    next_server_sequence_++,
                                                    state);
            for (const auto& [peer_id, peer] : sessions_)
            {
                if (peer_id == session->id || !peer->alive || !peer->greeted || peer->zone != state.zone) continue;
                deliveries.push_back({peer,
                                      proto::make_frame(proto::MessageType::player_joined,
                                                        next_server_sequence_++,
                                                        proto::PlayerJoined{session->id, session->player_name})});
                deliveries.push_back({session,
                                      proto::make_frame(proto::MessageType::player_joined,
                                                        next_server_sequence_++,
                                                        proto::PlayerJoined{peer_id, peer->player_name})});
                deliveries.push_back({peer, own_zone});
                deliveries.push_back({session,
                                      proto::make_frame(proto::MessageType::zone_state,
                                                        next_server_sequence_++,
                                                        proto::ZoneState{peer_id, peer->zone})});
                if (peer->appearance)
                    deliveries.push_back({session,
                                          proto::make_frame(proto::MessageType::appearance_state,
                                                            next_server_sequence_++,
                                                            *peer->appearance)});
                if (peer->transform)
                    deliveries.push_back({session,
                                          proto::make_frame(proto::MessageType::transform_snapshot,
                                                            next_server_sequence_++,
                                                            *peer->transform)});
                if (peer->movement)
                    deliveries.push_back({session,
                                          proto::make_frame(proto::MessageType::movement_state,
                                                            next_server_sequence_++,
                                                            *peer->movement)});
                if (session->appearance)
                    deliveries.push_back({peer,
                                          proto::make_frame(proto::MessageType::appearance_state,
                                                            next_server_sequence_++,
                                                            *session->appearance)});
                if (session->transform)
                    deliveries.push_back({peer,
                                          proto::make_frame(proto::MessageType::transform_snapshot,
                                                            next_server_sequence_++,
                                                            *session->transform)});
                if (session->movement)
                    deliveries.push_back({peer,
                                          proto::make_frame(proto::MessageType::movement_state,
                                                            next_server_sequence_++,
                                                            *session->movement)});
            }
        }
        deliver(deliveries);
        logger_.write("PLAYER_ZONE_CHANGE id=" + std::to_string(session->id) + " from=" +
                      (old_zone.empty() ? "<none>" : old_zone) + " to=" +
                      (state.zone.empty() ? "<none>" : state.zone));
    }

    auto handle_appearance(const std::shared_ptr<Session>& session,
                           proto::AppearanceState state,
                           std::uint64_t sequence) -> void
    {
        if (state.character_class.size() > 128 || state.outfit_mesh.size() > 2048 || state.hair_mesh.size() > 2048)
            throw proto::ProtocolError("appearance fields exceed server limits");
        state.player_id = session->id;
        std::vector<Delivery> deliveries;
        {
            std::lock_guard lock(state_mutex_);
            session->appearance = state;
            if (!session->zone.empty())
                append_zone_deliveries(deliveries,
                                       session->zone,
                                       session->id,
                                       proto::make_frame(proto::MessageType::appearance_state, sequence, state));
        }
        deliver(deliveries);
        logger_.write("APPEARANCE id=" + std::to_string(session->id) + " character=" + state.character_class);
    }

    auto handle_transform(const std::shared_ptr<Session>& session,
                          proto::TransformSnapshot state,
                          std::uint64_t sequence) -> void
    {
        if (!std::isfinite(state.x) || !std::isfinite(state.y) || !std::isfinite(state.z) ||
            !std::isfinite(state.pitch) || !std::isfinite(state.yaw) || !std::isfinite(state.roll))
            throw proto::ProtocolError("transform contains a non-finite value");
        state.player_id = session->id;
        std::vector<Delivery> deliveries;
        {
            std::lock_guard lock(state_mutex_);
            session->transform = state;
            if (!session->zone.empty())
                append_zone_deliveries(deliveries,
                                       session->zone,
                                       session->id,
                                       proto::make_frame(proto::MessageType::transform_snapshot, sequence, state));
        }
        deliver(deliveries);
    }

    auto handle_movement(const std::shared_ptr<Session>& session,
                         proto::MovementState state,
                         std::uint64_t sequence) -> void
    {
        // UE CharacterMovement's network-safe modes are None through Custom.
        // MOVE_MAX and arbitrary bytes are never forwarded into the game.
        if (state.movement_mode > 6)
        {
            send_error(session, 1004, "invalid MovementMode");
            throw proto::ProtocolError("invalid MovementMode");
        }
        state.player_id = session->id;
        std::vector<Delivery> deliveries;
        {
            std::lock_guard lock(state_mutex_);
            session->movement = state;
            if (!session->zone.empty())
                append_zone_deliveries(deliveries,
                                       session->zone,
                                       session->id,
                                       proto::make_frame(proto::MessageType::movement_state, sequence, state));
        }
        deliver(deliveries);
    }

    auto append_zone_deliveries(std::vector<Delivery>& deliveries,
                                const std::string& zone,
                                std::uint64_t except_id,
                                const proto::Frame& frame) -> void
    {
        for (const auto& [peer_id, peer] : sessions_)
        {
            if (peer_id != except_id && peer->alive && peer->greeted && peer->zone == zone)
                deliveries.push_back({peer, frame});
        }
    }

    auto deliver(const std::vector<Delivery>& deliveries) -> void
    {
        for (const auto& delivery : deliveries) send(delivery.target, delivery.frame);
    }

    auto send(const std::shared_ptr<Session>& session, const proto::Frame& frame) -> bool
    {
        if (!session->alive) return false;
        const auto bytes = proto::encode_frame(frame);
        std::string error;
        std::lock_guard lock(session->send_mutex);
        if (!session->alive) return false;
        if (!eo::net::send_all(session->socket, bytes, error))
        {
            session->alive = false;
            eo::net::shutdown_socket(session->socket);
            return false;
        }
        return true;
    }

    auto send_error(const std::shared_ptr<Session>& session, std::uint16_t code, const std::string& message) -> void
    {
        send(session,
             proto::make_frame(proto::MessageType::error,
                               next_server_sequence_++,
                               proto::ErrorMessage{code, message}));
    }

    auto disconnect(const std::shared_ptr<Session>& session, const std::string& reason) -> void
    {
        if (session->removed.exchange(true)) return;
        session->alive = false;

        std::vector<Delivery> deliveries;
        {
            std::lock_guard lock(state_mutex_);
            const auto found = sessions_.find(session->id);
            if (found != sessions_.end())
            {
                if (!session->zone.empty())
                    append_zone_deliveries(deliveries,
                                           session->zone,
                                           session->id,
                                           proto::make_frame(proto::MessageType::player_left,
                                                             next_server_sequence_++,
                                                             proto::PlayerLeft{session->id}));
                sessions_.erase(found);
            }
        }
        {
            std::lock_guard send_lock(session->send_mutex);
            eo::net::shutdown_socket(session->socket);
            eo::net::close_socket(session->socket);
        }
        deliver(deliveries);
        logger_.write("PLAYER_DISCONNECTED id=" + std::to_string(session->id) + " reason=" + reason);
    }

    auto watchdog_loop() -> void
    {
        while (!stopping_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto now = steady_ms();
            std::vector<std::shared_ptr<Session>> sessions;
            {
                std::lock_guard lock(state_mutex_);
                for (const auto& [id, session] : sessions_)
                {
                    (void)id;
                    sessions.push_back(session);
                }
            }
            for (const auto& session : sessions)
            {
                if (!session->alive) continue;
                if (now - session->last_activity_ms.load() >
                    static_cast<std::int64_t>(config_.client_timeout_seconds) * 1000)
                {
                    logger_.write("CLIENT_TIMEOUT id=" + std::to_string(session->id));
                    disconnect(session, "heartbeat timeout");
                    continue;
                }
                if (session->greeted &&
                    now - session->last_ping_ms.load() >=
                        static_cast<std::int64_t>(config_.heartbeat_interval_seconds) * 1000)
                {
                    session->last_ping_ms = now;
                    if (!send(session,
                              proto::Frame{proto::kProtocolVersion,
                                           proto::MessageType::ping,
                                           next_server_sequence_++,
                                           {}}))
                        disconnect(session, "heartbeat send failed");
                }
            }
        }
    }

    auto stop_all_sessions() -> void
    {
        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard lock(state_mutex_);
            for (const auto& [id, session] : sessions_)
            {
                (void)id;
                sessions.push_back(session);
            }
        }
        for (const auto& session : sessions)
        {
            session->alive = false;
            eo::net::shutdown_socket(session->socket);
        }
    }

    ServerConfig config_;
    Logger& logger_;
    std::atomic_bool stopping_{};
    eo::net::SocketHandle listener_{eo::net::kInvalidSocket};
    std::mutex state_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Session>> sessions_;
    std::vector<std::thread> workers_;
    std::thread watchdog_;
    std::uint64_t next_player_id_{1};
    std::atomic_uint64_t next_server_sequence_{1};
};

Server* active_server{};

#if defined(_WIN32)
auto WINAPI console_handler(DWORD signal) -> BOOL
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT)
    {
        if (active_server) active_server->stop();
        return TRUE;
    }
    return FALSE;
}
#else
auto signal_handler(int) -> void
{
    if (active_server) active_server->stop();
}
#endif

auto print_usage() -> void
{
    std::cout << "ExpeditionOnlineServer [--config path] [--host address] [--port number] "
                 "[--max-players number] [--heartbeat seconds] [--timeout seconds]\n";
}
} // namespace

auto main(int argc, char** argv) -> int
{
    try
    {
        std::filesystem::path config_path{"server.ini"};
        for (int index = 1; index < argc; ++index)
        {
            if (std::string_view(argv[index]) == "--config" && index + 1 < argc) config_path = argv[++index];
        }

        auto config = load_config(config_path);
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--config") ++index;
            else if (argument == "--host" && index + 1 < argc) config.bind_host = argv[++index];
            else if (argument == "--port" && index + 1 < argc)
            {
                const auto parsed = std::stoi(argv[++index]);
                if (parsed < 1 || parsed > 65535) throw std::runtime_error("port must be in 1..65535");
                config.port = static_cast<std::uint16_t>(parsed);
            }
            else if ((argument == "--max-players" || argument == "--max-clients") && index + 1 < argc)
                config.max_players = std::clamp(std::stoi(argv[++index]), 1, 1024);
            else if (argument == "--heartbeat" && index + 1 < argc)
                config.heartbeat_interval_seconds = std::clamp(std::stoi(argv[++index]), 1, 60);
            else if (argument == "--timeout" && index + 1 < argc)
                config.client_timeout_seconds = std::clamp(std::stoi(argv[++index]), 3, 300);
            else if (argument == "--help" || argument == "-h")
            {
                print_usage();
                return 0;
            }
            else throw std::runtime_error("unknown or incomplete argument: " + argument);
        }

        eo::net::SocketRuntime sockets;
        Logger logger("ExpeditionOnlineServer.log");
        Server server(config, logger);
        active_server = &server;
#if defined(_WIN32)
        SetConsoleCtrlHandler(console_handler, TRUE);
#else
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
#endif
        const auto result = server.run();
        active_server = nullptr;
        return result;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FATAL: " << exception.what() << '\n';
        return 1;
    }
}
