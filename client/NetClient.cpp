#include "NetClient.h"

#include <chrono>
#include <thread>

namespace client {

using namespace av;
using namespace av::proto;

NetClient::NetClient() = default;

bool NetClient::connect(const NetAddress& server, const Hello& hello, std::string& error, int timeoutMs)
{
    tcp_ = TcpStream::connect(server, timeoutMs);
    if (!tcp_) {
        error = "could not connect to " + server.toString();
        return false;
    }
    tcp_->send(static_cast<std::uint8_t>(Tcp::Hello), encode(hello));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<TcpStream::Frame> frames;
    while (std::chrono::steady_clock::now() < deadline) {
        frames.clear();
        if (!tcp_->receive(frames) && frames.empty()) {
            error = "connection closed by server";
            return false;
        }
        for (const TcpStream::Frame& f : frames) {
            if (f.type == static_cast<std::uint8_t>(Tcp::Welcome)) {
                const auto w = decodeWelcome(f.payload);
                if (!w) {
                    error = "malformed welcome";
                    return false;
                }
                welcome_ = *w;
            } else if (f.type == static_cast<std::uint8_t>(Tcp::Reject)) {
                const auto why = decodeChat(f.payload);
                error = why ? why->text : "rejected by server";
                return false;
            } else {
                handleFrame(f); // roster entries etc. may follow the welcome immediately
            }
        }
        if (welcome_.session != 0) {
            if (!udp_.open(0, false)) {
                error = "cannot open a UDP socket";
                return false;
            }
            udpServer_ = server;
            udpServer_.port = welcome_.udpPort;
            udpHelloCountdown_ = 0;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    error = "no answer from server";
    return false;
}

void NetClient::handleFrame(const TcpStream::Frame& f)
{
    switch (static_cast<Tcp>(f.type)) {
    case Tcp::Roster:
        if (const auto r = decodeRoster(f.payload)) {
            if (r->joined) roster_[r->netId] = r->name;
            else roster_.erase(r->netId);
        }
        break;
    case Tcp::ChatOut:
        if (const auto c = decodeChat(f.payload)) chat_.push_back(*c);
        break;
    case Tcp::Event:
        if (const auto e = decodeEvent(f.payload)) events_.push_back(*e);
        break;
    case Tcp::Status:
        if (const auto s = decodeStatus(f.payload)) status_ = *s;
        break;
    case Tcp::Reject:
        if (const auto why = decodeChat(f.payload)) reason_ = why->text;
        break;
    case Tcp::Scoreboard:
        if (auto s = decodeScoreboard(f.payload)) scores_ = std::move(*s);
        break;
    default:
        break;
    }
}

void NetClient::poll()
{
    if (!tcp_) {
        return;
    }
    std::vector<TcpStream::Frame> frames;
    if (!tcp_->receive(frames) && reason_.empty()) {
        reason_ = "lost connection to the server";
    }
    for (const TcpStream::Frame& f : frames) {
        bytesIn_ += f.payload.size() + 3;
        handleFrame(f);
    }
    tcp_->flush();

    std::uint8_t buf[2048];
    NetAddress from;
    for (int guard = 0; guard < 64; ++guard) {
        const int n = udp_.receiveFrom(from, buf, sizeof buf);
        if (n <= 0) {
            break;
        }
        bytesIn_ += static_cast<std::size_t>(n);
        ByteReader r(buf, static_cast<std::size_t>(n));
        const auto header = decodeUdpHeader(r);
        if (!header || header->session != welcome_.session || header->type != Udp::Snapshot) {
            continue;
        }
        if (auto s = decodeSnapshot(r)) {
            // UDP may reorder: keep only snapshots newer than what we have.
            if (s->tick > newestTick_) {
                newestTick_ = s->tick;
                snapshots_.push_back(std::move(*s));
            }
        }
    }
}

void NetClient::sendInputs(const std::vector<InputFrame>& recent)
{
    if (!udp_.isOpen()) {
        return;
    }
    // Until snapshots flow, also announce our UDP endpoint explicitly.
    if (newestTick_ == 0 && udpHelloCountdown_-- <= 0) {
        const auto hello = encodeUdpHello(welcome_.session);
        udp_.sendTo(udpServer_, hello.data(), hello.size());
        udpHelloCountdown_ = 10;
    }
    const auto packet = encodeInputs(welcome_.session, recent);
    udp_.sendTo(udpServer_, packet.data(), packet.size());
    bytesOut_ += packet.size();
}

void NetClient::sendChat(const std::string& text)
{
    if (tcp_) {
        tcp_->send(static_cast<std::uint8_t>(Tcp::Chat), encode(ChatLine{"", text, false}));
    }
}

void NetClient::sendCommand(CommandKind kind, int arg)
{
    if (tcp_) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(kind));
        w.i32(arg);
        tcp_->send(static_cast<std::uint8_t>(Tcp::Command), w.data());
    }
}

std::vector<Snapshot> NetClient::takeSnapshots()
{
    std::vector<Snapshot> out;
    out.swap(snapshots_);
    return out;
}

std::vector<Event> NetClient::takeEvents()
{
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

std::vector<ChatLine> NetClient::takeChat()
{
    std::vector<ChatLine> out;
    out.swap(chat_);
    return out;
}

} // namespace client
