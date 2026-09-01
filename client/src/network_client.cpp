#include <expedition_online/client/network_client.hpp>
#include <expedition_online/build_info.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace expedition_online::client
{
NetworkClient::NetworkClient(const ClientConfig& config, Logger& logger) : config_(config), logger_(logger) {}

NetworkClient::~NetworkClient()
{
    stop();
}

auto NetworkClient::start() -> void
{
    if (worker_.joinable()) return;
    stopping_ = false;
    worker_ = std::thread([this] { run(); });
}

auto NetworkClient::stop() -> void
{
    if (stopping_.exchange(true)) return;
    const auto socket = active_socket_.load();
    net::shutdown_socket(socket);
    if (worker_.joinable()) worker_.join();
}

auto NetworkClient::enqueue(protocol::Frame frame) -> void
{
    std::lock_guard lock(outgoing_mutex_);
    if (frame.type == protocol::MessageType::transform_snapshot)
    {
        std::erase_if(outgoing_, [](const auto& queued) {
            return queued.type == protocol::MessageType::transform_snapshot;
        });
    }
    if (outgoing_.size() >= 256) outgoing_.pop_front();
    outgoing_.push_back(std::move(frame));
}

auto NetworkClient::drain_incoming() -> std::vector<protocol::Frame>
{
    std::lock_guard lock(incoming_mutex_);
    auto frames = std::move(incoming_);
    incoming_.clear();
    return frames;
}

auto NetworkClient::connected() const noexcept -> bool
{
    return connected_;
}

auto NetworkClient::next_sequence() noexcept -> std::uint64_t
{
    return sequence_++;
}

auto NetworkClient::run() -> void
{
    try
    {
        net::SocketRuntime runtime;
        while (!stopping_)
        {
            std::string error;
            logger_.info("CONNECTING host=" + config_.host + " port=" + std::to_string(config_.port));
            auto socket = net::connect_tcp(config_.host, config_.port, error);
            if (socket == net::kInvalidSocket)
            {
                logger_.warning("CONNECT_FAILED " + error);
            }
            else
            {
                active_socket_ = socket;
                net::set_no_delay(socket, true, error);
                try
                {
                    run_connection(socket);
                }
                catch (const std::exception& exception)
                {
                    if (!stopping_) logger_.warning(std::string("CONNECTION_LOST ") + exception.what());
                }
                connected_ = false;
                net::shutdown_socket(socket);
                net::close_socket(socket);
                active_socket_ = net::kInvalidSocket;
                {
                    std::lock_guard lock(outgoing_mutex_);
                    outgoing_.clear();
                }
            }

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.reconnect_delay_ms);
            while (!stopping_ && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    catch (const std::exception& exception)
    {
        logger_.error(std::string("NETWORK_THREAD_FATAL ") + exception.what());
    }
}

auto NetworkClient::run_connection(net::SocketHandle socket) -> void
{
    std::string error;
    const auto hello = protocol::make_frame(protocol::MessageType::hello,
                                            next_sequence(),
                                            protocol::Hello{config_.player_name,
                                                            build_info::identity("Client",
                                                                                 protocol::kProtocolVersion)});
    const auto hello_bytes = protocol::encode_frame(hello);
    if (!net::send_all(socket, hello_bytes, error)) throw std::runtime_error(error);

    protocol::FrameDecoder decoder;
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    bool welcomed{};
    auto last_received = std::chrono::steady_clock::now();
    auto next_heartbeat = last_received + std::chrono::seconds(config_.heartbeat_interval_seconds);
    while (!stopping_)
    {
        if (welcomed)
        {
            for (const auto& frame : take_outgoing())
            {
                const auto bytes = protocol::encode_frame(frame);
                if (!net::send_all(socket, bytes, error)) throw std::runtime_error(error);
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_heartbeat)
            {
                const auto ping = protocol::Frame{protocol::kProtocolVersion,
                                                  protocol::MessageType::ping,
                                                  next_sequence(),
                                                  {}};
                const auto bytes = protocol::encode_frame(ping);
                if (!net::send_all(socket, bytes, error)) throw std::runtime_error(error);
                next_heartbeat = now + std::chrono::seconds(config_.heartbeat_interval_seconds);
            }
            if (now - last_received > std::chrono::seconds(config_.server_timeout_seconds))
            {
                throw std::runtime_error("SERVER_TIMEOUT no data for " +
                                         std::to_string(config_.server_timeout_seconds) + " seconds");
            }
        }

        if (net::wait_readable(socket, 25, error))
        {
            const auto received = net::receive_some(socket, buffer, error);
            if (received <= 0) throw std::runtime_error(error);
            last_received = std::chrono::steady_clock::now();
            decoder.push(std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(received)));
            for (auto& frame : decoder.take_frames())
            {
                if (frame.version != protocol::kProtocolVersion)
                {
                    logger_.error("PROTOCOL_MISMATCH client=" + std::to_string(protocol::kProtocolVersion) +
                                  " server=" + std::to_string(frame.version));
                    throw std::runtime_error("server protocol version mismatch");
                }
                if (!protocol::is_known_message_type(frame.type))
                {
                    throw std::runtime_error("server sent an unknown message type");
                }
                if (protocol::is_empty_payload_message(frame.type) && !frame.payload.empty())
                {
                    throw std::runtime_error("server sent a malformed heartbeat");
                }
                if (frame.type == protocol::MessageType::ping)
                {
                    const auto pong = protocol::Frame{protocol::kProtocolVersion,
                                                      protocol::MessageType::pong,
                                                      frame.sequence,
                                                      {}};
                    const auto bytes = protocol::encode_frame(pong);
                    if (!net::send_all(socket, bytes, error)) throw std::runtime_error(error);
                    continue;
                }
                if (frame.type == protocol::MessageType::pong)
                {
                    continue;
                }
                if (frame.type == protocol::MessageType::welcome)
                {
                    welcomed = true;
                    connected_ = true;
                    const auto welcome = protocol::decode_welcome(frame.payload);
                    logger_.info("CONNECTED player_id=" + std::to_string(welcome.player_id) + " " +
                                 build_info::identity("Client", protocol::kProtocolVersion));
                }
                push_incoming(std::move(frame));
            }
        }
        else if (!error.empty())
        {
            throw std::runtime_error(error);
        }
    }
}

auto NetworkClient::push_incoming(protocol::Frame frame) -> void
{
    std::lock_guard lock(incoming_mutex_);
    if (incoming_.size() >= 1024) incoming_.erase(incoming_.begin());
    incoming_.push_back(std::move(frame));
}

auto NetworkClient::take_outgoing() -> std::deque<protocol::Frame>
{
    std::lock_guard lock(outgoing_mutex_);
    auto frames = std::move(outgoing_);
    outgoing_.clear();
    return frames;
}
} // namespace expedition_online::client
