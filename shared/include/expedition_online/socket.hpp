#pragma once

#include <cstdint>
#include <span>
#include <string>

#if defined(_WIN32)
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace expedition_online::net
{
#if defined(_WIN32)
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

class SocketRuntime
{
  public:
    SocketRuntime();
    ~SocketRuntime();
    SocketRuntime(const SocketRuntime&) = delete;
    auto operator=(const SocketRuntime&) -> SocketRuntime& = delete;

  private:
    bool active_{};
};

auto connect_tcp(const std::string& host, std::uint16_t port, std::string& error) -> SocketHandle;
auto create_listener(const std::string& bind_host,
                     std::uint16_t port,
                     int backlog,
                     std::string& error) -> SocketHandle;
auto accept_client(SocketHandle listener, std::string& peer, std::string& error) -> SocketHandle;
auto send_all(SocketHandle socket, std::span<const std::uint8_t> bytes, std::string& error) -> bool;
auto receive_some(SocketHandle socket, std::span<std::uint8_t> buffer, std::string& error) -> int;
auto wait_readable(SocketHandle socket, int timeout_ms, std::string& error) -> bool;
auto set_no_delay(SocketHandle socket, bool enabled, std::string& error) -> bool;
auto shutdown_socket(SocketHandle socket) -> void;
auto close_socket(SocketHandle& socket) -> void;
auto last_socket_error() -> std::string;
} // namespace expedition_online::net
