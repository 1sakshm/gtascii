#include "Networking/Net.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLen = int;
#define AV_SEND_FLAGS 0
#define AV_CLOSE closesocket
#define AV_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#define AV_INPROGRESS(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
static int lastError() { return WSAGetLastError(); }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SockLen = socklen_t;
using SOCKET = int;
#ifdef MSG_NOSIGNAL
#define AV_SEND_FLAGS MSG_NOSIGNAL
#else
#define AV_SEND_FLAGS 0
#endif
#define AV_CLOSE ::close
#define AV_WOULDBLOCK(e) ((e) == EWOULDBLOCK || (e) == EAGAIN)
#define AV_INPROGRESS(e) ((e) == EINPROGRESS || (e) == EWOULDBLOCK)
static int lastError() { return errno; }
#endif

namespace av {

namespace {

std::atomic<int> g_initCount{0};

SOCKET toSock(SocketHandle h) { return static_cast<SOCKET>(h); }

bool setNonBlocking(SocketHandle h)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(toSock(h), FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(toSock(h), F_GETFL, 0);
    return flags >= 0 && fcntl(toSock(h), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

sockaddr_in makeAddr(std::uint32_t ip, std::uint16_t port)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port);
    return a;
}

std::uint16_t boundPort(SocketHandle h)
{
    sockaddr_in a{};
    SockLen len = sizeof a;
    if (getsockname(toSock(h), reinterpret_cast<sockaddr*>(&a), &len) != 0) {
        return 0;
    }
    return ntohs(a.sin_port);
}

} // namespace

NetInit::NetInit()
{
#ifdef _WIN32
    if (g_initCount.fetch_add(1) == 0) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
#endif
}

NetInit::~NetInit()
{
#ifdef _WIN32
    if (g_initCount.fetch_sub(1) == 1) {
        WSACleanup();
    }
#endif
}

std::string NetAddress::toString() const
{
    return std::to_string((ip >> 24) & 0xFF) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF) + ":" + std::to_string(port);
}

std::optional<NetAddress> NetAddress::parse(const std::string& text, std::uint16_t defaultPort)
{
    const NetInit init; // name resolution needs the socket library even before any socket exists
    std::string host = text;
    NetAddress out;
    out.port = defaultPort;
    const auto colon = text.rfind(':');
    if (colon != std::string::npos) {
        host = text.substr(0, colon);
        const int p = std::atoi(text.substr(colon + 1).c_str());
        if (p <= 0 || p > 65535) {
            return std::nullopt;
        }
        out.port = static_cast<std::uint16_t>(p);
    }
    if (host.empty() || host == "localhost") {
        out.ip = 0x7F000001;
        return out;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return std::nullopt;
    }
    out.ip = ntohl(reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(result);
    return out;
}

// ---- UDP ---------------------------------------------------------------

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(std::uint16_t port, bool loopbackOnly, bool shared)
{
    close();
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == static_cast<SOCKET>(kInvalid)) {
        return false;
    }
    handle_ = static_cast<SocketHandle>(s);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
    if (shared) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
#ifdef SO_REUSEPORT
        setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof one);
#endif
    }
    const sockaddr_in a = makeAddr(loopbackOnly ? 0x7F000001 : INADDR_ANY, port);
    if (bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || !setNonBlocking(handle_)) {
        close();
        return false;
    }
    return true;
}

void UdpSocket::close()
{
    if (handle_ != kInvalid) {
        AV_CLOSE(toSock(handle_));
        handle_ = kInvalid;
    }
}

std::uint16_t UdpSocket::localPort() const { return boundPort(handle_); }

bool UdpSocket::broadcast(std::uint16_t port, const std::uint8_t* data, std::size_t size)
{
    NetAddress all;
    all.ip = 0xFFFFFFFFu; // 255.255.255.255
    all.port = port;
    return sendTo(all, data, size);
}

bool UdpSocket::sendTo(const NetAddress& to, const std::uint8_t* data, std::size_t size)
{
    if (handle_ == kInvalid) {
        return false;
    }
    const sockaddr_in a = makeAddr(to.ip, to.port);
    return sendto(toSock(handle_), reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&a), sizeof a) == static_cast<int>(size);
}

int UdpSocket::receiveFrom(NetAddress& from, std::uint8_t* buffer, std::size_t capacity)
{
    if (handle_ == kInvalid) {
        return -1;
    }
    sockaddr_in a{};
    SockLen len = sizeof a;
    const int n = static_cast<int>(recvfrom(toSock(handle_), reinterpret_cast<char*>(buffer), static_cast<int>(capacity), 0,
                                            reinterpret_cast<sockaddr*>(&a), &len));
    if (n < 0) {
        const int e = lastError();
#ifdef _WIN32
        // ICMP port-unreachable surfaces as WSAECONNRESET on Windows; ignore it.
        if (AV_WOULDBLOCK(e) || e == WSAECONNRESET) return 0;
#else
        if (AV_WOULDBLOCK(e)) return 0;
#endif
        return -1;
    }
    from.ip = ntohl(a.sin_addr.s_addr);
    from.port = ntohs(a.sin_port);
    return n;
}

// ---- TCP ---------------------------------------------------------------

TcpStream::TcpStream(SocketHandle handle, NetAddress peer)
    : handle_(handle)
    , peer_(peer)
{
    setNonBlocking(handle_);
    int one = 1;
    setsockopt(toSock(handle_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(toSock(handle_), SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof one);
#endif
}

TcpStream::~TcpStream() { close(); }

void TcpStream::close()
{
    if (handle_ != UdpSocket::kInvalid) {
        AV_CLOSE(toSock(handle_));
        handle_ = UdpSocket::kInvalid;
    }
    connected_ = false;
}

std::unique_ptr<TcpStream> TcpStream::connect(const NetAddress& to, int timeoutMs)
{
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == static_cast<SOCKET>(UdpSocket::kInvalid)) {
        return nullptr;
    }
    const auto h = static_cast<SocketHandle>(s);
    setNonBlocking(h);
    const sockaddr_in a = makeAddr(to.ip, to.port);
    if (::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 && !AV_INPROGRESS(lastError())) {
        AV_CLOSE(s);
        return nullptr;
    }
    fd_set writeSet, errorSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(s, &writeSet);
    FD_SET(s, &errorSet);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(static_cast<int>(s) + 1, nullptr, &writeSet, &errorSet, &tv) <= 0 || FD_ISSET(s, &errorSet)) {
        AV_CLOSE(s);
        return nullptr;
    }
    int err = 0;
    SockLen len = sizeof err;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    if (err != 0) {
        AV_CLOSE(s);
        return nullptr;
    }
    return std::make_unique<TcpStream>(h, to);
}

bool TcpStream::send(std::uint8_t type, const std::vector<std::uint8_t>& payload)
{
    if (!connected_) {
        return false;
    }
    const std::size_t len = payload.size() + 1;
    if (len > kMaxFrame) {
        return false;
    }
    outbox_.push_back(static_cast<std::uint8_t>(len & 0xFF));
    outbox_.push_back(static_cast<std::uint8_t>(len >> 8));
    outbox_.push_back(type);
    outbox_.insert(outbox_.end(), payload.begin(), payload.end());
    return flush();
}

bool TcpStream::flush()
{
    while (connected_ && !outbox_.empty()) {
        const int n = static_cast<int>(::send(toSock(handle_), reinterpret_cast<const char*>(outbox_.data()),
                                              static_cast<int>(outbox_.size()), AV_SEND_FLAGS));
        if (n > 0) {
            outbox_.erase(outbox_.begin(), outbox_.begin() + n);
        } else if (n < 0 && AV_WOULDBLOCK(lastError())) {
            break; // try again next time
        } else {
            close();
        }
    }
    if (outbox_.size() > 1024 * 1024) {
        close(); // peer not reading: drop it rather than grow without bound
    }
    return connected_;
}

bool TcpStream::receive(std::vector<Frame>& out)
{
    std::uint8_t buf[8192];
    while (connected_) {
        const int n = static_cast<int>(recv(toSock(handle_), reinterpret_cast<char*>(buf), sizeof buf, 0));
        if (n > 0) {
            inbox_.insert(inbox_.end(), buf, buf + n);
        } else if (n < 0 && AV_WOULDBLOCK(lastError())) {
            break;
        } else {
            close(); // orderly shutdown (0) or error
        }
    }
    std::size_t pos = 0;
    while (inbox_.size() - pos >= 3) {
        const std::size_t len = inbox_[pos] | (static_cast<std::size_t>(inbox_[pos + 1]) << 8);
        if (len == 0 || len > kMaxFrame) {
            close();
            break;
        }
        if (inbox_.size() - pos - 2 < len) {
            break;
        }
        Frame f;
        f.type = inbox_[pos + 2];
        f.payload.assign(inbox_.begin() + static_cast<std::ptrdiff_t>(pos + 3),
                         inbox_.begin() + static_cast<std::ptrdiff_t>(pos + 2 + len));
        out.push_back(std::move(f));
        pos += 2 + len;
    }
    inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<std::ptrdiff_t>(pos));
    return connected_;
}

TcpListener::~TcpListener() { close(); }

bool TcpListener::listen(std::uint16_t port, bool loopbackOnly)
{
    close();
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == static_cast<SOCKET>(UdpSocket::kInvalid)) {
        return false;
    }
    handle_ = static_cast<SocketHandle>(s);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    const sockaddr_in a = makeAddr(loopbackOnly ? 0x7F000001 : INADDR_ANY, port);
    if (bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 16) != 0 || !setNonBlocking(handle_)) {
        close();
        return false;
    }
    return true;
}

void TcpListener::close()
{
    if (handle_ != UdpSocket::kInvalid) {
        AV_CLOSE(toSock(handle_));
        handle_ = UdpSocket::kInvalid;
    }
}

std::uint16_t TcpListener::localPort() const { return boundPort(handle_); }

std::unique_ptr<TcpStream> TcpListener::accept()
{
    if (handle_ == UdpSocket::kInvalid) {
        return nullptr;
    }
    sockaddr_in a{};
    SockLen len = sizeof a;
    const SOCKET s = ::accept(toSock(handle_), reinterpret_cast<sockaddr*>(&a), &len);
    if (s == static_cast<SOCKET>(UdpSocket::kInvalid)) {
        return nullptr;
    }
    NetAddress peer;
    peer.ip = ntohl(a.sin_addr.s_addr);
    peer.port = ntohs(a.sin_port);
    return std::make_unique<TcpStream>(static_cast<SocketHandle>(s), peer);
}

} // namespace av
