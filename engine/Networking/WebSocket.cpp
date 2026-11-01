#include "WebSocket.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <Security/SecureTransport.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace av {

bool parseUrl(const std::string& url, ParsedUrl& out)
{
    std::string rest;
    auto starts = [&](const char* prefix) {
        const std::size_t n = std::strlen(prefix);
        if (url.size() >= n && url.compare(0, n, prefix) == 0) {
            rest = url.substr(n);
            return true;
        }
        return false;
    };
    if (starts("wss://") || starts("https://")) {
        out.secure = true;
    } else if (starts("ws://") || starts("http://")) {
        out.secure = false;
    } else {
        return false;
    }
    const std::size_t slash = rest.find_first_of("/?");
    std::string hostPort = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (!out.path.empty() && out.path[0] == '?') out.path = "/" + out.path;
    out.port = out.secure ? 443 : 80;
    const std::size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        const int p = std::atoi(hostPort.c_str() + colon + 1);
        if (p <= 0 || p > 65535) return false;
        out.port = static_cast<std::uint16_t>(p);
        hostPort.resize(colon);
    }
    out.host = hostPort;
    return !out.host.empty();
}

namespace {

std::vector<std::uint8_t> bytesOf(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace

#if defined(_WIN32)
// ===========================================================================
// Windows: WinHTTP does TLS, proxies and the WebSocket protocol.
// ===========================================================================

namespace {

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string winError(const char* what)
{
    const DWORD e = GetLastError();
    switch (e) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return std::string(what) + ": cannot resolve the server name (offline?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return std::string(what) + ": cannot reach the server";
    case ERROR_WINHTTP_TIMEOUT: return std::string(what) + ": timed out";
    case ERROR_WINHTTP_SECURE_FAILURE: return std::string(what) + ": secure connection failed";
    default: return std::string(what) + " failed (error " + std::to_string(e) + ")";
    }
}

} // namespace

struct WebSocket::Impl {
    HINTERNET session = nullptr, connection = nullptr, request = nullptr, socket = nullptr;
    std::thread reader;
    std::atomic<bool> open{false};
    std::atomic<bool> readerDone{true};
    std::mutex inboxMutex, sendMutex;
    std::vector<Message> inbox;
    std::string reason;

    ~Impl() { shutdown(); }

    void fail(const std::string& why)
    {
        std::lock_guard<std::mutex> lock(inboxMutex);
        if (reason.empty()) reason = why;
        open = false;
    }

    void readLoop()
    {
        std::vector<std::uint8_t> buffer(64 * 1024), message;
        for (;;) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD err = WinHttpWebSocketReceive(socket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
            if (err != NO_ERROR) {
                fail("connection lost");
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT code = 0;
                char text[124] = {};
                DWORD len = 0;
                std::string why = "closed by the server";
                if (WinHttpWebSocketQueryCloseStatus(socket, &code, text, sizeof text, &len) == NO_ERROR && len > 0) {
                    why.assign(text, len);
                }
                fail(why);
                break;
            }
            message.insert(message.end(), buffer.begin(), buffer.begin() + read);
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                std::lock_guard<std::mutex> lock(inboxMutex);
                inbox.push_back({type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, std::move(message)});
                message.clear();
            }
        }
        readerDone = true;
    }

    void shutdown()
    {
        if (socket) {
            if (open) {
                WinHttpWebSocketShutdown(socket, 1000, nullptr, 0);
            }
            for (int i = 0; i < 100 && !readerDone; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            WinHttpCloseHandle(socket); // aborts a receive that is still pending
            socket = nullptr;
        }
        if (reader.joinable()) reader.join();
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        request = connection = session = nullptr;
        open = false;
    }
};

bool WebSocket::connect(const std::string& url, int timeoutMs, std::string& error, int* httpStatus)
{
    close();
    impl_ = std::make_unique<Impl>();
    Impl& s = *impl_;
    if (httpStatus) *httpStatus = 0;
    ParsedUrl u;
    if (!parseUrl(url, u)) {
        error = "bad URL '" + url + "'";
        return false;
    }
    s.session = WinHttpOpen(L"gtascii/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s.session) {
        error = winError("WinHTTP");
        return false;
    }
    WinHttpSetTimeouts(s.session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    s.connection = WinHttpConnect(s.session, widen(u.host).c_str(), u.port, 0);
    if (!s.connection) {
        error = winError("connect");
        return false;
    }
    s.request = WinHttpOpenRequest(s.connection, L"GET", widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!s.request || !WinHttpSetOption(s.request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        error = winError("request");
        return false;
    }
    if (!WinHttpSendRequest(s.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(s.request, nullptr)) {
        error = winError("connect");
        return false;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(s.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
        if (httpStatus) *httpStatus = static_cast<int>(status);
        char body[256] = {};
        DWORD got = 0;
        WinHttpReadData(s.request, body, sizeof body - 1, &got);
        error = got > 0 ? std::string(body, got) : "server answered HTTP " + std::to_string(status);
        return false;
    }
    s.socket = WinHttpWebSocketCompleteUpgrade(s.request, 0);
    if (!s.socket) {
        error = winError("WebSocket upgrade");
        return false;
    }
    WinHttpCloseHandle(s.request);
    s.request = nullptr;
    DWORD forever = 0;
    WinHttpSetOption(s.socket, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
    s.open = true;
    s.readerDone = false;
    s.reader = std::thread([&s] { s.readLoop(); });
    return true;
}

static bool winSend(WebSocket::Impl* s, WINHTTP_WEB_SOCKET_BUFFER_TYPE type, const std::vector<std::uint8_t>& data)
{
    if (!s || !s->open) return false;
    std::lock_guard<std::mutex> lock(s->sendMutex);
    const DWORD err = WinHttpWebSocketSend(s->socket, type, const_cast<std::uint8_t*>(data.data()), static_cast<DWORD>(data.size()));
    if (err != NO_ERROR) {
        s->fail("connection lost");
        return false;
    }
    return true;
}

bool WebSocket::send(const std::vector<std::uint8_t>& data)
{
    return winSend(impl_.get(), WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, data);
}

bool WebSocket::sendText(const std::string& text)
{
    return winSend(impl_.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, bytesOf(text));
}

#else
// ===========================================================================
// POSIX: own WebSocket framing over a TCP socket, optionally wrapped in TLS.
// One I/O thread owns the connection (TLS objects are not thread-safe).
// ===========================================================================

namespace {

// Byte stream over a connected, blocking socket. read(): > 0 bytes,
// 0 = nothing right now, -1 = closed / failed.
class Stream {
public:
    explicit Stream(int fd) : fd_(fd) {}
    virtual ~Stream() = default;
    virtual bool handshake(const std::string& host, std::string& error)
    {
        (void)host;
        (void)error;
        return true;
    }
    virtual long read(void* data, std::size_t size)
    {
        const ssize_t n = ::recv(fd_, data, size, 0);
        if (n > 0) return static_cast<long>(n);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
    }
    virtual bool writeAll(const std::uint8_t* data, std::size_t size)
    {
        while (size > 0) {
#if defined(MSG_NOSIGNAL)
            const ssize_t n = ::send(fd_, data, size, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(fd_, data, size, 0);
#endif
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (n <= 0) return false;
            data += n;
            size -= static_cast<std::size_t>(n);
        }
        return true;
    }
    virtual bool pending() { return false; }

protected:
    int fd_;
};

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

OSStatus stRead(SSLConnectionRef connection, void* data, size_t* length)
{
    const int fd = *static_cast<const int*>(connection);
    const size_t want = *length;
    size_t got = 0;
    while (got < want) {
        const ssize_t n = ::recv(fd, static_cast<char*>(data) + got, want - got, 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
        } else if (n == 0) {
            *length = got;
            return errSSLClosedGraceful;
        } else if (errno == EINTR) {
            continue;
        } else {
            *length = got;
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? errSSLWouldBlock : errSSLClosedAbort;
        }
    }
    *length = got;
    return noErr;
}

OSStatus stWrite(SSLConnectionRef connection, const void* data, size_t* length)
{
    const int fd = *static_cast<const int*>(connection);
    const size_t want = *length;
    size_t sent = 0;
    while (sent < want) {
        const ssize_t n = ::send(fd, static_cast<const char*>(data) + sent, want - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            *length = sent;
            return (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) ? errSSLWouldBlock : errSSLClosedAbort;
        }
    }
    *length = sent;
    return noErr;
}

class TlsStream : public Stream {
public:
    explicit TlsStream(int fd) : Stream(fd) {}
    ~TlsStream() override
    {
        if (ctx_) {
            SSLClose(ctx_);
            CFRelease(ctx_);
        }
    }
    bool handshake(const std::string& host, std::string& error) override
    {
        ctx_ = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
        if (!ctx_) {
            error = "TLS unavailable";
            return false;
        }
        SSLSetIOFuncs(ctx_, stRead, stWrite);
        SSLSetConnection(ctx_, &fd_);
        SSLSetPeerDomainName(ctx_, host.c_str(), host.size());
        OSStatus st;
        do {
            st = SSLHandshake(ctx_);
        } while (st == errSSLWouldBlock);
        if (st != noErr) {
            error = "secure connection failed (" + std::to_string(static_cast<int>(st)) + ")";
            return false;
        }
        return true;
    }
    long read(void* data, std::size_t size) override
    {
        size_t processed = 0;
        const OSStatus st = SSLRead(ctx_, data, size, &processed);
        if (processed > 0) return static_cast<long>(processed);
        return st == errSSLWouldBlock ? 0 : -1;
    }
    bool writeAll(const std::uint8_t* data, std::size_t size) override
    {
        while (size > 0) {
            size_t processed = 0;
            const OSStatus st = SSLWrite(ctx_, data, size, &processed);
            data += processed;
            size -= processed;
            if (st != noErr && st != errSSLWouldBlock) return false;
        }
        return true;
    }
    bool pending() override
    {
        size_t buffered = 0;
        return SSLGetBufferedReadSize(ctx_, &buffered) == noErr && buffered > 0;
    }

private:
    SSLContextRef ctx_ = nullptr;
};
#pragma clang diagnostic pop

#else // Linux & other POSIX: the system OpenSSL, loaded at run time

struct OpenSsl {
    bool ok = false;
    int (*init)(std::uint64_t, const void*) = nullptr;
    const void* (*clientMethod)() = nullptr;
    void* (*ctxNew)(const void*) = nullptr;
    int (*ctxDefaultPaths)(void*) = nullptr;
    void (*ctxSetVerify)(void*, int, void*) = nullptr;
    void (*ctxFree)(void*) = nullptr;
    void* (*sslNew)(void*) = nullptr;
    int (*setFd)(void*, int) = nullptr;
    long (*ctrl)(void*, int, long, void*) = nullptr;
    int (*set1Host)(void*, const char*) = nullptr;
    int (*connect)(void*) = nullptr;
    int (*read)(void*, void*, int) = nullptr;
    int (*write)(void*, const void*, int) = nullptr;
    int (*getError)(const void*, int) = nullptr;
    int (*pending)(const void*) = nullptr;
    int (*shutdown)(void*) = nullptr;
    void (*free)(void*) = nullptr;

    static OpenSsl& get()
    {
        static OpenSsl s = load();
        return s;
    }

private:
    template <typename F>
    static bool sym(void* lib, const char* name, F& out)
    {
        out = reinterpret_cast<F>(dlsym(lib, name));
        return out != nullptr;
    }

    static OpenSsl load()
    {
        OpenSsl s;
        void* lib = nullptr;
        for (const char* name : {"libssl.so.3", "libssl.so.1.1", "libssl.so"}) {
            if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        }
        if (!lib) return s;
        s.ok = sym(lib, "OPENSSL_init_ssl", s.init) && sym(lib, "TLS_client_method", s.clientMethod) &&
               sym(lib, "SSL_CTX_new", s.ctxNew) && sym(lib, "SSL_CTX_set_default_verify_paths", s.ctxDefaultPaths) &&
               sym(lib, "SSL_CTX_set_verify", s.ctxSetVerify) && sym(lib, "SSL_CTX_free", s.ctxFree) &&
               sym(lib, "SSL_new", s.sslNew) && sym(lib, "SSL_set_fd", s.setFd) && sym(lib, "SSL_ctrl", s.ctrl) &&
               sym(lib, "SSL_set1_host", s.set1Host) && sym(lib, "SSL_connect", s.connect) && sym(lib, "SSL_read", s.read) &&
               sym(lib, "SSL_write", s.write) && sym(lib, "SSL_get_error", s.getError) && sym(lib, "SSL_pending", s.pending) &&
               sym(lib, "SSL_shutdown", s.shutdown) && sym(lib, "SSL_free", s.free);
        if (s.ok) s.init(0, nullptr);
        return s;
    }
};

class TlsStream : public Stream {
public:
    explicit TlsStream(int fd) : Stream(fd), lib_(OpenSsl::get()) {}
    ~TlsStream() override
    {
        if (ssl_) {
            lib_.shutdown(ssl_);
            lib_.free(ssl_);
        }
        if (ctx_) lib_.ctxFree(ctx_);
    }
    bool handshake(const std::string& host, std::string& error) override
    {
        if (!lib_.ok) {
            error = "secure connections need OpenSSL (libssl.so.3 or libssl.so.1.1) - install your distribution's openssl package";
            return false;
        }
        constexpr int kVerifyPeer = 1, kSetHostname = 55, kHostName = 0;
        ctx_ = lib_.ctxNew(lib_.clientMethod());
        if (!ctx_) {
            error = "TLS setup failed";
            return false;
        }
        lib_.ctxDefaultPaths(ctx_);
        lib_.ctxSetVerify(ctx_, kVerifyPeer, nullptr);
        ssl_ = lib_.sslNew(ctx_);
        lib_.setFd(ssl_, fd_);
        lib_.ctrl(ssl_, kSetHostname, kHostName, const_cast<char*>(host.c_str())); // SNI
        lib_.set1Host(ssl_, host.c_str());                                          // certificate name check
        if (lib_.connect(ssl_) != 1) {
            error = "secure connection failed (certificate or network problem)";
            return false;
        }
        return true;
    }
    long read(void* data, std::size_t size) override
    {
        const int n = lib_.read(ssl_, data, static_cast<int>(size));
        if (n > 0) return n;
        const int e = lib_.getError(ssl_, n);
        constexpr int kWantRead = 2, kWantWrite = 3;
        if (e == kWantRead || e == kWantWrite) return 0;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
    }
    bool writeAll(const std::uint8_t* data, std::size_t size) override
    {
        while (size > 0) {
            const int n = lib_.write(ssl_, data, static_cast<int>(size));
            if (n <= 0) {
                const int e = lib_.getError(ssl_, n);
                if (e == 2 || e == 3) continue;
                return false;
            }
            data += n;
            size -= static_cast<std::size_t>(n);
        }
        return true;
    }
    bool pending() override { return lib_.pending(ssl_) > 0; }

private:
    OpenSsl& lib_;
    void* ctx_ = nullptr;
    void* ssl_ = nullptr;
};
#endif

std::string base64(const std::uint8_t* data, std::size_t size)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) | (i + 1 < size ? data[i + 1] << 8 : 0) |
                                (i + 2 < size ? data[i + 2] : 0);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < size ? table[(v >> 6) & 63] : '=';
        out += i + 2 < size ? table[v & 63] : '=';
    }
    return out;
}

int connectTcp(const std::string& host, std::uint16_t port, int timeoutMs, std::string& error)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || !list) {
        error = "cannot resolve " + host + " (offline?)";
        return -1;
    }
    int fd = -1;
    for (addrinfo* a = list; a && fd < 0; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            int soErr = 0;
            socklen_t len = sizeof soErr;
            rc = (::poll(&p, 1, timeoutMs) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len) == 0 && soErr == 0) ? 0 : -1;
        }
        if (rc != 0) {
            ::close(fd);
            fd = -1;
            continue;
        }
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    freeaddrinfo(list);
    if (fd < 0) {
        error = "cannot reach " + host;
        return -1;
    }
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return fd;
}

} // namespace

struct WebSocket::Impl {
    int fd = -1;
    std::unique_ptr<Stream> io;
    std::thread thread;
    std::atomic<bool> open{false}, stop{false};
    std::mutex mutex;
    std::vector<Message> inbox;
    std::vector<std::vector<std::uint8_t>> outbox; // encoded frames
    std::string reason;
    std::vector<std::uint8_t> rx, fragment;
    bool fragmentText = false;
    std::mt19937 rng{std::random_device{}()};

    ~Impl()
    {
        stop = true;
        if (thread.joinable()) thread.join();
        io.reset();
        if (fd >= 0) ::close(fd);
    }

    void fail(const std::string& why)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (reason.empty()) reason = why;
        open = false;
    }

    std::vector<std::uint8_t> frame(std::uint8_t opcode, const std::uint8_t* data, std::size_t size)
    {
        std::vector<std::uint8_t> f;
        f.reserve(size + 14);
        f.push_back(static_cast<std::uint8_t>(0x80 | opcode));
        if (size < 126) {
            f.push_back(static_cast<std::uint8_t>(0x80 | size));
        } else if (size < 65536) {
            f.push_back(0x80 | 126);
            f.push_back(static_cast<std::uint8_t>(size >> 8));
            f.push_back(static_cast<std::uint8_t>(size));
        } else {
            f.push_back(0x80 | 127);
            for (int i = 7; i >= 0; --i) f.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(size) >> (8 * i)));
        }
        std::uint8_t mask[4];
        const std::uint32_t m = rng();
        std::memcpy(mask, &m, 4);
        f.insert(f.end(), mask, mask + 4);
        for (std::size_t i = 0; i < size; ++i) f.push_back(data[i] ^ mask[i & 3]);
        return f;
    }

    void queue(std::uint8_t opcode, const std::vector<std::uint8_t>& data)
    {
        auto f = frame(opcode, data.data(), data.size());
        std::lock_guard<std::mutex> lock(mutex);
        outbox.push_back(std::move(f));
    }

    // Parses complete frames out of `rx`. Returns false when the connection closed.
    bool parse()
    {
        for (;;) {
            if (rx.size() < 2) return true;
            const bool fin = (rx[0] & 0x80) != 0;
            const std::uint8_t op = rx[0] & 0x0F;
            const bool masked = (rx[1] & 0x80) != 0;
            std::uint64_t len = rx[1] & 0x7F;
            std::size_t header = 2;
            if (len == 126) {
                if (rx.size() < 4) return true;
                len = (static_cast<std::uint64_t>(rx[2]) << 8) | rx[3];
                header = 4;
            } else if (len == 127) {
                if (rx.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | rx[2 + i];
                header = 10;
            }
            if (len > (16u << 20)) {
                fail("message too large");
                return false;
            }
            const std::size_t maskAt = header;
            if (masked) header += 4;
            if (rx.size() < header + len) return true;
            std::vector<std::uint8_t> payload(rx.begin() + static_cast<long>(header), rx.begin() + static_cast<long>(header + len));
            if (masked) {
                for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= rx[maskAt + (i & 3)];
            }
            rx.erase(rx.begin(), rx.begin() + static_cast<long>(header + len));

            if (op == 0x8) { // close
                std::string why = "closed by the server";
                if (payload.size() > 2) why.assign(payload.begin() + 2, payload.end());
                const std::vector<std::uint8_t> echo(payload.begin(), payload.begin() + std::min<std::size_t>(2, payload.size()));
                auto f = frame(0x8, echo.data(), echo.size());
                io->writeAll(f.data(), f.size());
                fail(why);
                return false;
            }
            if (op == 0x9) { // ping
                queue(0xA, payload);
                continue;
            }
            if (op == 0xA) continue; // pong
            if (op == 0x1 || op == 0x2) {
                fragment = std::move(payload);
                fragmentText = op == 0x1;
            } else if (op == 0x0) {
                fragment.insert(fragment.end(), payload.begin(), payload.end());
            } else {
                continue;
            }
            if (fin) {
                std::lock_guard<std::mutex> lock(mutex);
                inbox.push_back({fragmentText, std::move(fragment)});
                fragment.clear();
            }
        }
    }

    void loop()
    {
        std::uint8_t buffer[16 * 1024];
        while (!stop && open) {
            std::vector<std::vector<std::uint8_t>> pendingOut;
            {
                std::lock_guard<std::mutex> lock(mutex);
                pendingOut.swap(outbox);
            }
            for (const auto& f : pendingOut) {
                if (!io->writeAll(f.data(), f.size())) {
                    fail("connection lost");
                    return;
                }
            }
            bool readable = io->pending();
            if (!readable) {
                pollfd p{fd, POLLIN, 0};
                readable = ::poll(&p, 1, 4) > 0;
            }
            if (!readable) continue;
            const long n = io->read(buffer, sizeof buffer);
            if (n < 0) {
                fail("connection lost");
                return;
            }
            rx.insert(rx.end(), buffer, buffer + n);
            if (!parse()) return;
        }
        if (stop && open) { // polite close
            const std::uint8_t code[2] = {0x03, 0xE8};
            auto f = frame(0x8, code, 2);
            io->writeAll(f.data(), f.size());
            open = false;
        }
    }
};

bool WebSocket::connect(const std::string& url, int timeoutMs, std::string& error, int* httpStatus)
{
    close();
    impl_ = std::make_unique<Impl>();
    Impl& s = *impl_;
    if (httpStatus) *httpStatus = 0;
    ParsedUrl u;
    if (!parseUrl(url, u)) {
        error = "bad URL '" + url + "'";
        return false;
    }
    s.fd = connectTcp(u.host, u.port, timeoutMs, error);
    if (s.fd < 0) return false;
    s.io = u.secure ? std::unique_ptr<Stream>(new TlsStream(s.fd)) : std::make_unique<Stream>(s.fd);
    if (!s.io->handshake(u.host, error)) return false;

    std::uint8_t key[16];
    for (auto& b : key) b = static_cast<std::uint8_t>(s.rng());
    const bool defaultPort = u.port == (u.secure ? 443 : 80);
    const std::string request = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + (defaultPort ? "" : ":" + std::to_string(u.port)) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + base64(key, 16) +
                                "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: gtascii/1.0\r\n\r\n";
    if (!s.io->writeAll(reinterpret_cast<const std::uint8_t*>(request.data()), request.size())) {
        error = "connection lost during handshake";
        return false;
    }
    std::string response;
    char buffer[4096];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::size_t end;
    while ((end = response.find("\r\n\r\n")) == std::string::npos) {
        const long n = s.io->read(buffer, sizeof buffer);
        if (n < 0 || std::chrono::steady_clock::now() > deadline || response.size() > 64 * 1024) {
            error = n < 0 ? "connection closed during handshake" : "handshake timed out";
            return false;
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }
    int status = 0;
    std::sscanf(response.c_str(), "HTTP/%*s %d", &status);
    if (status != 101) {
        if (httpStatus) *httpStatus = status;
        std::string body = response.substr(end + 4);
        if (body.empty()) {
            const long n = s.io->read(buffer, sizeof buffer);
            if (n > 0) body.assign(buffer, static_cast<std::size_t>(n));
        }
        error = body.empty() ? "server answered HTTP " + std::to_string(status) : body.substr(0, 200);
        return false;
    }
    s.rx.assign(response.begin() + static_cast<long>(end + 4), response.end());
    // From here on the I/O thread polls before reading; keep a short timeout as a safety net.
    timeval tv{1, 0};
    setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    s.open = true;
    if (!s.rx.empty() && !s.parse()) return false;
    s.thread = std::thread([&s] { s.loop(); });
    return true;
}

bool WebSocket::send(const std::vector<std::uint8_t>& data)
{
    if (!impl_ || !impl_->open) return false;
    impl_->queue(0x2, data);
    return true;
}

bool WebSocket::sendText(const std::string& text)
{
    if (!impl_ || !impl_->open) return false;
    impl_->queue(0x1, bytesOf(text));
    return true;
}

#endif

// ---- shared -------------------------------------------------------------------

WebSocket::WebSocket() = default;

WebSocket::~WebSocket()
{
    close();
}

bool WebSocket::poll(std::vector<Message>& out)
{
    if (!impl_) return false;
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(impl_->inboxMutex);
#else
    std::lock_guard<std::mutex> lock(impl_->mutex);
#endif
    for (auto& m : impl_->inbox) out.push_back(std::move(m));
    impl_->inbox.clear();
    return impl_->open;
}

bool WebSocket::isOpen() const
{
    return impl_ && impl_->open;
}

std::string WebSocket::closeReason() const
{
    if (!impl_) return "not connected";
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(impl_->inboxMutex);
#else
    std::lock_guard<std::mutex> lock(impl_->mutex);
#endif
    return impl_->reason.empty() ? std::string("closed") : impl_->reason;
}

void WebSocket::close()
{
    impl_.reset();
}

} // namespace av
