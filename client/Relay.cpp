#include "Relay.h"

#include "Core/Json.h"
#include "Networking/Net.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <random>

namespace client {

using namespace av;
using namespace av::proto;
using Clock = std::chrono::steady_clock;

namespace {

enum : std::uint8_t { kTcp = 1, kUdp = 2, kOpen = 3, kClose = 4, kReady = 5 };

// Batching keeps the relay's per-message cost down (and inside the free
// tier); snapshots only arrive at 20 Hz anyway.
constexpr auto kFlushInterval = std::chrono::milliseconds(50);
constexpr auto kKeepAlive = std::chrono::seconds(20);
constexpr std::size_t kFlushBytes = 16 * 1024;

void putRecord(std::vector<std::uint8_t>& out, std::uint8_t kind, std::uint16_t peer, const std::uint8_t* data, std::size_t size)
{
    out.push_back(kind);
    out.push_back(static_cast<std::uint8_t>(peer));
    out.push_back(static_cast<std::uint8_t>(peer >> 8));
    out.push_back(static_cast<std::uint8_t>(size));
    out.push_back(static_cast<std::uint8_t>(size >> 8));
    out.insert(out.end(), data, data + size);
}

void putTcp(std::vector<std::uint8_t>& out, std::uint16_t peer, const TcpStream::Frame& f)
{
    std::vector<std::uint8_t> body;
    body.reserve(f.payload.size() + 1);
    body.push_back(f.type);
    body.insert(body.end(), f.payload.begin(), f.payload.end());
    putRecord(out, kTcp, peer, body.data(), body.size());
}

struct Record {
    std::uint8_t kind;
    std::uint16_t peer;
    const std::uint8_t* data;
    std::size_t size;
};

std::vector<Record> parseRecords(const std::vector<std::uint8_t>& bytes)
{
    std::vector<Record> out;
    std::size_t at = 0;
    while (at + 5 <= bytes.size()) {
        const std::uint16_t peer = static_cast<std::uint16_t>(bytes[at + 1] | (bytes[at + 2] << 8));
        const std::size_t len = static_cast<std::size_t>(bytes[at + 3] | (bytes[at + 4] << 8));
        if (at + 5 + len > bytes.size()) break;
        out.push_back({bytes[at], peer, bytes.data() + at + 5, len});
        at += 5 + len;
    }
    return out;
}

std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string endpoint(std::string base, const std::string& pathAndQuery)
{
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + pathAndQuery;
}

// Waits for the relay's READY record after the upgrade.
bool awaitReady(WebSocket& ws, int timeoutMs, std::string& error)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<WebSocket::Message> msgs;
    while (Clock::now() < deadline) {
        msgs.clear();
        const bool open = ws.poll(msgs);
        for (const auto& m : msgs) {
            if (m.text) continue;
            for (const Record& r : parseRecords(m.data)) {
                if (r.kind == kReady) return true;
            }
        }
        if (!open) {
            error = ws.closeReason();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    error = "the online relay did not answer";
    return false;
}

std::string describeHttp(int status, const std::string& body)
{
    if (status == 0) return "cannot reach the online relay: " + body;
    return body;
}

} // namespace

bool isRelayUrl(const std::string& address)
{
    ParsedUrl u;
    return parseUrl(address, u);
}

std::string randomRoomCode()
{
    static const char kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::string code;
    for (int i = 0; i < 4; ++i) code += kChars[rd() % (sizeof kChars - 1)];
    return code;
}

std::optional<std::vector<RoomInfo>> listRelayRooms(const std::string& relayUrl, std::string& error, int timeoutMs)
{
    WebSocket ws;
    int status = 0;
    if (!ws.connect(endpoint(relayUrl, "/rooms"), timeoutMs, error, &status)) {
        error = describeHttp(status, error);
        return std::nullopt;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<WebSocket::Message> msgs;
    while (Clock::now() < deadline) {
        const bool open = ws.poll(msgs);
        for (const auto& m : msgs) {
            if (!m.text) continue;
            try {
                const JsonValue list = parseJson(std::string(m.data.begin(), m.data.end()));
                std::vector<RoomInfo> rooms;
                if (list.isArray()) {
                    for (const JsonValue& v : list.asArray()) {
                        RoomInfo r;
                        r.code = v.stringOr("code", "");
                        r.name = v.stringOr("name", "Room");
                        r.mode = static_cast<GameMode>(std::clamp(static_cast<int>(v.numberOr("mode", 0)), 0, 2));
                        r.players = static_cast<std::uint8_t>(v.numberOr("players", 1));
                        r.maxPlayers = static_cast<std::uint8_t>(v.numberOr("max", 8));
                        r.relay = true;
                        if (!r.code.empty()) rooms.push_back(r);
                    }
                }
                return rooms;
            } catch (const std::exception&) {
                error = "the online relay sent a bad room list";
                return std::nullopt;
            }
        }
        if (!open) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    error = "the online relay did not send the room list";
    return std::nullopt;
}

// ---- host ---------------------------------------------------------------------

RelayHost::~RelayHost()
{
    stop();
}

bool RelayHost::start(const std::string& relayUrl, const std::string& code, const CreateRoomRequest& room, std::string& error,
                      int* httpStatus)
{
    const std::string url = endpoint(relayUrl, "/host?code=" + code + "&name=" + urlEncode(room.name) +
                                                   "&mode=" + std::to_string(static_cast<int>(room.mode)) +
                                                   "&max=" + std::to_string(room.maxPlayers) + "&public=" + (room.isPublic ? "1" : "0"));
    int status = 0;
    if (!ws_.connect(url, 6000, error, &status)) {
        if (httpStatus) *httpStatus = status;
        error = describeHttp(status, error);
        return false;
    }
    if (!awaitReady(ws_, 6000, error)) return false;
    alive_ = true;
    return true;
}

void RelayHost::serve(std::uint16_t serverPort)
{
    serverPort_ = serverPort;
    stop_ = false;
    thread_ = std::thread([this] { loop(); });
}

void RelayHost::stop()
{
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    ws_.close();
    alive_ = false;
}

std::string RelayHost::problem() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return problem_;
}

void RelayHost::loop()
{
    NetInit net;
    struct Peer {
        std::unique_ptr<TcpStream> tcp;
        UdpSocket udp;
    };
    std::map<std::uint16_t, std::unique_ptr<Peer>> peers;
    NetAddress server;
    server.port = serverPort_;
    std::vector<std::uint8_t> out;
    std::vector<WebSocket::Message> msgs;
    std::vector<TcpStream::Frame> frames;
    auto lastSend = Clock::now();
    std::uint8_t buf[2048];

    while (!stop_) {
        msgs.clear();
        const bool open = ws_.poll(msgs);
        for (const auto& m : msgs) {
            if (m.text) continue;
            for (const Record& r : parseRecords(m.data)) {
                if (r.kind == kOpen) {
                    auto p = std::make_unique<Peer>();
                    p->tcp = TcpStream::connect(server, 2000);
                    if (!p->tcp || !p->udp.open(0, true)) {
                        putRecord(out, kClose, r.peer, nullptr, 0);
                        continue;
                    }
                    peers[r.peer] = std::move(p);
                    continue;
                }
                auto it = peers.find(r.peer);
                if (it == peers.end()) continue;
                if (r.kind == kTcp && r.size >= 1) {
                    it->second->tcp->send(r.data[0], std::vector<std::uint8_t>(r.data + 1, r.data + r.size));
                } else if (r.kind == kUdp) {
                    it->second->udp.sendTo(server, r.data, r.size);
                } else if (r.kind == kClose) {
                    peers.erase(it);
                }
            }
        }
        for (auto it = peers.begin(); it != peers.end();) {
            Peer& p = *it->second;
            frames.clear();
            p.tcp->receive(frames);
            for (const auto& f : frames) putTcp(out, it->first, f);
            p.tcp->flush();
            NetAddress from;
            int n;
            while ((n = p.udp.receiveFrom(from, buf, sizeof buf)) > 0) {
                putRecord(out, kUdp, it->first, buf, static_cast<std::size_t>(n));
            }
            if (!p.tcp->connected()) { // the server dropped this player
                putRecord(out, kClose, it->first, nullptr, 0);
                it = peers.erase(it);
            } else {
                ++it;
            }
        }
        guests_ = static_cast<int>(peers.size());

        const auto now = Clock::now();
        if (!out.empty() && (now - lastSend >= kFlushInterval || out.size() >= kFlushBytes)) {
            ws_.send(out);
            out.clear();
            lastSend = now;
        } else if (out.empty() && now - lastSend >= kKeepAlive) {
            ws_.sendText("ping");
            lastSend = now;
        }
        if (!open) {
            std::lock_guard<std::mutex> lock(mutex_);
            problem_ = ws_.closeReason();
            alive_ = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// ---- guest --------------------------------------------------------------------

struct RelayGuest::Sockets {
    TcpListener listener;
    UdpSocket udp;
};

RelayGuest::~RelayGuest()
{
    stop();
}

bool RelayGuest::start(const std::string& relayUrl, const std::string& code, std::string& error, int* httpStatus)
{
    int status = 0;
    if (!ws_.connect(endpoint(relayUrl, "/join?code=" + code), 6000, error, &status)) {
        if (httpStatus) *httpStatus = status;
        error = describeHttp(status, error);
        return false;
    }
    if (!awaitReady(ws_, 6000, error)) return false;

    // NetClient talks to one port for TCP and UDP: bind both on the same number.
    sockets_ = new Sockets;
    for (int attempt = 0; attempt < 20 && port_ == 0; ++attempt) {
        sockets_->udp.close();
        sockets_->listener.close();
        if (!sockets_->udp.open(0, true)) continue;
        const std::uint16_t p = sockets_->udp.localPort();
        if (sockets_->listener.listen(p, true)) port_ = p;
    }
    if (port_ == 0) {
        error = "could not open a local port for the relay";
        return false;
    }
    alive_ = true;
    stop_ = false;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void RelayGuest::stop()
{
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    ws_.close();
    delete sockets_;
    sockets_ = nullptr;
    alive_ = false;
}

std::string RelayGuest::problem() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return problem_;
}

void RelayGuest::loop()
{
    NetInit net;
    std::unique_ptr<TcpStream> game; // the local NetClient
    std::optional<NetAddress> gameUdp;
    std::vector<TcpStream::Frame> pendingTcp; // relay -> game before it connected
    std::vector<std::uint8_t> out;
    std::vector<WebSocket::Message> msgs;
    std::vector<TcpStream::Frame> frames;
    auto lastSend = Clock::now();
    std::uint8_t buf[2048];

    auto deliver = [&](TcpStream::Frame f) {
        // The game must send its UDP to us, not to the host's real port.
        if (f.type == static_cast<std::uint8_t>(Tcp::Welcome)) {
            if (auto w = decodeWelcome(f.payload)) {
                w->udpPort = port_;
                f.payload = encode(*w);
            }
        }
        if (game) {
            game->send(f.type, f.payload);
        } else {
            pendingTcp.push_back(std::move(f));
        }
    };

    while (!stop_) {
        if (!game) {
            game = sockets_->listener.accept();
            if (game) {
                for (auto& f : pendingTcp) game->send(f.type, f.payload);
                pendingTcp.clear();
            }
        }
        msgs.clear();
        const bool open = ws_.poll(msgs);
        for (const auto& m : msgs) {
            if (m.text) continue;
            for (const Record& r : parseRecords(m.data)) {
                if (r.kind == kTcp && r.size >= 1) {
                    deliver({r.data[0], std::vector<std::uint8_t>(r.data + 1, r.data + r.size)});
                } else if (r.kind == kUdp && gameUdp) {
                    sockets_->udp.sendTo(*gameUdp, r.data, r.size);
                }
            }
        }
        if (game) {
            frames.clear();
            game->receive(frames);
            for (const auto& f : frames) putTcp(out, 0, f);
            game->flush();
        }
        NetAddress from;
        int n;
        while ((n = sockets_->udp.receiveFrom(from, buf, sizeof buf)) > 0) {
            gameUdp = from;
            putRecord(out, kUdp, 0, buf, static_cast<std::size_t>(n));
        }

        const auto now = Clock::now();
        if (!out.empty() && (now - lastSend >= kFlushInterval || out.size() >= kFlushBytes)) {
            ws_.send(out);
            out.clear();
            lastSend = now;
        } else if (out.empty() && now - lastSend >= kKeepAlive) {
            ws_.sendText("ping");
            lastSend = now;
        }
        if (!open) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                problem_ = ws_.closeReason();
            }
            alive_ = false;
            if (game) game->close(); // NetClient notices the disconnect
            break;
        }
        if (game && !game->connected()) break; // player left
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace client
