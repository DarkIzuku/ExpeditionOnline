#pragma once

#include <expedition_online/client/config.hpp>
#include <expedition_online/client/logger.hpp>
#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace expedition_online::client
{
class NetworkClient
{
  public:
    NetworkClient(const ClientConfig& config, Logger& logger);
    ~NetworkClient();
    NetworkClient(const NetworkClient&) = delete;
    auto operator=(const NetworkClient&) -> NetworkClient& = delete;

    auto start() -> void;
    auto stop() -> void;
    auto enqueue(protocol::Frame frame) -> void;
    auto drain_incoming() -> std::vector<protocol::Frame>;
    auto connected() const noexcept -> bool;
    auto next_sequence() noexcept -> std::uint64_t;

  private:
    auto run() -> void;
    auto run_connection(net::SocketHandle socket) -> void;
    auto push_incoming(protocol::Frame frame) -> void;
    auto take_outgoing() -> std::deque<protocol::Frame>;

    const ClientConfig& config_;
    Logger& logger_;
    std::atomic_bool stopping_{};
    std::atomic_bool connected_{};
    std::atomic_uint64_t sequence_{1};
    std::atomic<net::SocketHandle> active_socket_{net::kInvalidSocket};
    std::thread worker_;
    std::mutex outgoing_mutex_;
    std::deque<protocol::Frame> outgoing_;
    std::mutex incoming_mutex_;
    std::vector<protocol::Frame> incoming_;
};
} // namespace expedition_online::client
