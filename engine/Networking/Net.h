#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace av {

// Initialises the socket library for the process lifetime of the guard
// (WSAStartup on Windows, nothing on POSIX). Safe to nest.
class NetInit {
public:
    NetInit();
    ~NetInit();
    NetInit(const NetInit&) = delete;
    NetInit& operator=(const NetInit&) = delete;
};

struct NetAddress {
    std::uint32_t ip = 0x7F000001; // host byte order (127.0.0.1)
    std::uint16_t port = 0;

    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }
    std::string toString() const;
    // "host", "host:port", "1.2.3.4:5"; resolves names via DNS. nullopt on failure.
    static std::optional<NetAddress> parse(const std::string& text, std::uint16_t defaultPort);
};

using SocketHandle = std::intptr_t;

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port 0 = any free port. loopbackOnly binds to 127.0.0.1. `shared`
    // allows several processes to bind the same port and enables broadcast
    // (LAN discovery).
    bool open(std::uint16_t port, bool loopbackOnly, bool shared = false);
    bool broadcast(std::uint16_t port, const std::uint8_t* data, std::size_t size);
    void close();
    bool isOpen() const { return handle_ != kInvalid; }
    std::uint16_t localPort() const;

    bool sendTo(const NetAddress& to, const std::uint8_t* data, std::size_t size);
    // Returns bytes received (> 0), 0 if nothing pending, -1 on error.
    int receiveFrom(NetAddress& from, std::uint8_t* buffer, std::size_t capacity);

    static constexpr SocketHandle kInvalid = -1;

private:
    SocketHandle handle_ = kInvalid;
};

// Reliable, ordered stream carrying length-prefixed frames:
// [u16 length][u8 type][payload...], length = 1 + payload size.
class TcpStream {
public:
    struct Frame {
        std::uint8_t type;
        std::vector<std::uint8_t> payload;
    };

    explicit TcpStream(SocketHandle handle, NetAddress peer);
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    static std::unique_ptr<TcpStream> connect(const NetAddress& to, int timeoutMs);

    bool connected() const { return connected_; }
    const NetAddress& peer() const { return peer_; }

    // Queues a frame and tries to flush. Returns false once disconnected.
    bool send(std::uint8_t type, const std::vector<std::uint8_t>& payload);
    bool flush();
    // Reads whatever is available (non-blocking) and appends complete frames.
    bool receive(std::vector<Frame>& out);
    void close();

    static constexpr std::size_t kMaxFrame = 32 * 1024;

private:
    SocketHandle handle_;
    NetAddress peer_;
    bool connected_ = true;
    std::vector<std::uint8_t> inbox_, outbox_;
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    bool listen(std::uint16_t port, bool loopbackOnly);
    void close();
    std::uint16_t localPort() const;
    // Non-blocking; nullptr when no connection is pending.
    std::unique_ptr<TcpStream> accept();

private:
    SocketHandle handle_ = UdpSocket::kInvalid;
};

} // namespace av
