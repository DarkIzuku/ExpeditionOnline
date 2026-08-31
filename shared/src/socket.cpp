#include <expedition_online/socket.hpp>

#include <array>
#include <climits>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#include <Windows.h>
#endif

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

namespace expedition_online::net
{
namespace
{
auto numeric_port(std::uint16_t port) -> std::string
{
    return std::to_string(port);
}

auto gai_error_text(int code) -> std::string
{
#if defined(_WIN32)
    return gai_strerrorA(code);
#else
    return gai_strerror(code);
#endif
}
} // namespace

SocketRuntime::SocketRuntime()
{
#if defined(_WIN32)
    WSADATA data{};
    const auto result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
    {
        throw std::runtime_error("WSAStartup failed: " + std::to_string(result));
    }
#endif
    active_ = true;
}

SocketRuntime::~SocketRuntime()
{
#if defined(_WIN32)
    if (active_)
    {
        WSACleanup();
    }
#endif
}

auto connect_tcp(const std::string& host, std::uint16_t port, std::string& error) -> SocketHandle
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses{};
    const auto result = getaddrinfo(host.c_str(), numeric_port(port).c_str(), &hints, &addresses);
    if (result != 0)
    {
        error = "getaddrinfo: " + gai_error_text(result);
        return kInvalidSocket;
    }

    SocketHandle connected = kInvalidSocket;
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        auto candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == kInvalidSocket)
        {
            continue;
        }
        if (::connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
        {
            connected = candidate;
            break;
        }
        close_socket(candidate);
    }
    freeaddrinfo(addresses);

    if (connected == kInvalidSocket)
    {
        error = "connect: " + last_socket_error();
    }
    return connected;
}

auto create_listener(const std::string& bind_host,
                     std::uint16_t port,
                     int backlog,
                     std::string& error) -> SocketHandle
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addresses{};
    const char* host = bind_host.empty() || bind_host == "*" ? nullptr : bind_host.c_str();
    const auto result = getaddrinfo(host, numeric_port(port).c_str(), &hints, &addresses);
    if (result != 0)
    {
        error = "getaddrinfo: " + gai_error_text(result);
        return kInvalidSocket;
    }

    SocketHandle listener = kInvalidSocket;
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        auto candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == kInvalidSocket)
        {
            continue;
        }

        int reuse = 1;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (::bind(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 &&
            ::listen(candidate, backlog) == 0)
        {
            listener = candidate;
            break;
        }
        close_socket(candidate);
    }
    freeaddrinfo(addresses);

    if (listener == kInvalidSocket)
    {
        error = "bind/listen: " + last_socket_error();
    }
    return listener;
}

auto accept_client(SocketHandle listener, std::string& peer, std::string& error) -> SocketHandle
{
    sockaddr_storage address{};
#if defined(_WIN32)
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    auto client = ::accept(listener, reinterpret_cast<sockaddr*>(&address), &address_size);
    if (client == kInvalidSocket)
    {
        error = "accept: " + last_socket_error();
        return kInvalidSocket;
    }

    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&address),
                    address_size,
                    host.data(),
                    static_cast<int>(host.size()),
                    service.data(),
                    static_cast<int>(service.size()),
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    {
        peer = std::string(host.data()) + ":" + service.data();
    }
    else
    {
        peer = "unknown";
    }
    return client;
}

auto send_all(SocketHandle socket, std::span<const std::uint8_t> bytes, std::string& error) -> bool
{
    std::size_t sent{};
    while (sent < bytes.size())
    {
        const auto remaining = bytes.size() - sent;
        const auto chunk = static_cast<int>(remaining > static_cast<std::size_t>(INT_MAX) ? INT_MAX : remaining);
        const auto result = ::send(socket,
                                   reinterpret_cast<const char*>(bytes.data() + sent),
                                   chunk,
                                   0);
        if (result <= 0)
        {
            error = "send: " + last_socket_error();
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

auto receive_some(SocketHandle socket, std::span<std::uint8_t> buffer, std::string& error) -> int
{
    const auto requested = static_cast<int>(buffer.size() > static_cast<std::size_t>(INT_MAX) ? INT_MAX : buffer.size());
    const auto result = ::recv(socket, reinterpret_cast<char*>(buffer.data()), requested, 0);
    if (result < 0)
    {
        error = "recv: " + last_socket_error();
    }
    else if (result == 0)
    {
        error = "peer closed the connection";
    }
    return result;
}

auto wait_readable(SocketHandle socket, int timeout_ms, std::string& error) -> bool
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

#if defined(_WIN32)
    const auto result = select(0, &readable, nullptr, nullptr, &timeout);
#else
    const auto result = select(socket + 1, &readable, nullptr, nullptr, &timeout);
#endif
    if (result < 0)
    {
        error = "select: " + last_socket_error();
        return false;
    }
    error.clear();
    return result > 0 && FD_ISSET(socket, &readable) != 0;
}

auto set_no_delay(SocketHandle socket, bool enabled, std::string& error) -> bool
{
    const int value = enabled ? 1 : 0;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
    {
        error = "setsockopt(TCP_NODELAY): " + last_socket_error();
        return false;
    }
    return true;
}

auto shutdown_socket(SocketHandle socket) -> void
{
    if (socket == kInvalidSocket)
    {
        return;
    }
#if defined(_WIN32)
    ::shutdown(socket, SD_BOTH);
#else
    ::shutdown(socket, SHUT_RDWR);
#endif
}

auto close_socket(SocketHandle& socket) -> void
{
    if (socket == kInvalidSocket)
    {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
    socket = kInvalidSocket;
}

auto last_socket_error() -> std::string
{
#if defined(_WIN32)
    const auto code = WSAGetLastError();
    char* message{};
    const auto flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const auto size = FormatMessageA(flags,
                                     nullptr,
                                     static_cast<DWORD>(code),
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<char*>(&message),
                                     0,
                                     nullptr);
    std::string result = size != 0 && message != nullptr ? std::string(message, size) : ("error " + std::to_string(code));
    if (message != nullptr)
    {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
    {
        result.pop_back();
    }
    return result;
#else
    return std::strerror(errno);
#endif
}
} // namespace expedition_online::net
