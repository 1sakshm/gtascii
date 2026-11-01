#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace av {

// Minimal WebSocket client (RFC 6455) for ws:// and wss:// URLs.
//
// connect() blocks until the handshake finishes; afterwards a background
// thread does the I/O, so send() and poll() never block. TLS comes from the
// operating system: WinHTTP on Windows, Secure Transport on macOS and the
// system OpenSSL (loaded at run time) on Linux.
class WebSocket {
public:
    struct Message {
        bool text = false;
        std::vector<std::uint8_t> data;
    };

    WebSocket();
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    // `httpStatus` receives the server's status when it refused the upgrade
    // (e.g. 404), or 0 when the connection itself failed.
    bool connect(const std::string& url, int timeoutMs, std::string& error, int* httpStatus = nullptr);

    bool send(const std::vector<std::uint8_t>& data); // binary message
    bool sendText(const std::string& text);
    // Appends every message received since the last call. Returns false once
    // the connection has closed (messages that arrived before are still returned).
    bool poll(std::vector<Message>& out);
    bool isOpen() const;
    // Close reason sent by the server, or a description of the failure.
    std::string closeReason() const;
    void close();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

struct ParsedUrl {
    bool secure = false;
    std::string host;
    std::uint16_t port = 0;
    std::string path = "/"; // including the query string
};
// Accepts ws://, wss://, http:// (as ws) and https:// (as wss). false on garbage.
bool parseUrl(const std::string& url, ParsedUrl& out);

} // namespace av
