#include "Lobby.h"

#include <chrono>
#include <thread>

namespace client {

using namespace av;
using namespace av::proto;

namespace {

// Sends one lobby frame and waits for the first reply of an expected type.
std::optional<TcpStream::Frame> request(const NetAddress& server, Tcp type, const std::vector<std::uint8_t>& payload,
                                        Tcp expected, std::string& error, int timeoutMs)
{
    const NetInit init;
    auto tcp = TcpStream::connect(server, timeoutMs);
    if (!tcp) {
        error = "could not reach the room server at " + server.toString();
        return std::nullopt;
    }
    tcp->send(static_cast<std::uint8_t>(type), payload);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<TcpStream::Frame> frames;
    while (std::chrono::steady_clock::now() < deadline) {
        frames.clear();
        const bool alive = tcp->receive(frames);
        for (TcpStream::Frame& f : frames) {
            if (f.type == static_cast<std::uint8_t>(expected)) {
                return std::move(f);
            }
            if (f.type == static_cast<std::uint8_t>(Tcp::Reject)) {
                const auto why = decodeChat(f.payload);
                error = why ? why->text : "request refused";
                return std::nullopt;
            }
        }
        if (!alive) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    error = "the room server did not answer";
    return std::nullopt;
}

} // namespace

std::optional<std::vector<RoomInfo>> listRooms(const NetAddress& server, std::string& error, int timeoutMs)
{
    const auto f = request(server, Tcp::ListRooms, {}, Tcp::RoomList, error, timeoutMs);
    if (!f) {
        return std::nullopt;
    }
    auto rooms = decodeRoomList(f->payload);
    if (!rooms) {
        error = "malformed room list";
        return std::nullopt;
    }
    for (RoomInfo& r : *rooms) {
        r.address = server.toString().substr(0, server.toString().rfind(':'));
        r.port = server.port;
    }
    return rooms;
}

std::optional<std::string> createRoom(const NetAddress& server, const CreateRoomRequest& req, std::string& error, int timeoutMs)
{
    const auto f = request(server, Tcp::CreateRoom, encode(req), Tcp::RoomCreated, error, timeoutMs);
    if (!f) {
        return std::nullopt;
    }
    ByteReader r(f->payload);
    std::string code = r.str();
    if (!r.ok() || code.empty()) {
        error = "malformed reply";
        return std::nullopt;
    }
    return code;
}

std::vector<RoomInfo> discoverLanRooms(int timeoutMs)
{
    const NetInit init;
    std::vector<RoomInfo> found;
    UdpSocket sock;
    if (!sock.open(0, false)) {
        return found;
    }
    const auto query = encodeDiscoveryQuery();
    sock.broadcast(kDiscoveryPort, query.data(), query.size());
    NetAddress self;
    self.port = kDiscoveryPort; // servers on this same machine
    sock.sendTo(self, query.data(), query.size());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::uint8_t buf[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        NetAddress from;
        const int n = sock.receiveFrom(from, buf, sizeof buf);
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const auto reply = decodeDiscoveryReply(buf, static_cast<std::size_t>(n));
        if (!reply) {
            continue;
        }
        for (RoomInfo r : reply->second) {
            const std::string host = from.toString().substr(0, from.toString().rfind(':'));
            r.address = host;
            r.port = reply->first;
            bool duplicate = false;
            for (const RoomInfo& known : found) {
                duplicate = duplicate || (known.code == r.code && known.port == r.port &&
                                          (known.address == r.address || known.address == "127.0.0.1" || r.address == "127.0.0.1"));
            }
            if (!duplicate) {
                found.push_back(r);
            }
        }
    }
    return found;
}

std::string normalizeRoomCode(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out += static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
    }
    if (out.size() > 4 && out.rfind("GTA", 0) == 0) {
        out = out.substr(3);
    }
    return out;
}

} // namespace client
