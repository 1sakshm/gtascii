#pragma once

#include "Protocol.h"

#include "Networking/Net.h"

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace client {

// Client side of the protocol: TCP session (hello/welcome, chat, events,
// status, roster) plus a UDP socket for inputs and snapshots.
class NetClient {
public:
    NetClient();

    // Blocking handshake (up to timeoutMs). On failure returns false and sets `error`.
    bool connect(const av::NetAddress& server, const av::proto::Hello& hello, std::string& error, int timeoutMs = 5000);
    bool connected() const { return tcp_ && tcp_->connected(); }
    const av::proto::Welcome& welcome() const { return welcome_; }

    // Non-blocking receive of everything pending.
    void poll();
    void sendInputs(const std::vector<av::proto::InputFrame>& recent);
    void sendChat(const std::string& text);
    void sendCommand(av::proto::CommandKind kind, int arg);

    // Received data, drained by the game each frame.
    std::vector<av::proto::Snapshot> takeSnapshots();
    std::vector<av::proto::Event> takeEvents();
    std::vector<av::proto::ChatLine> takeChat();
    const av::proto::PlayerStatus& status() const { return status_; }
    const std::map<std::uint16_t, std::string>& roster() const { return roster_; }
    const std::vector<av::proto::ScoreEntry>& scores() const { return scores_; }
    std::string disconnectReason() const { return reason_; }

    std::size_t bytesIn() const { return bytesIn_; }
    std::size_t bytesOut() const { return bytesOut_; }

private:
    void handleFrame(const av::TcpStream::Frame& f);

    av::NetInit net_;
    std::unique_ptr<av::TcpStream> tcp_;
    av::UdpSocket udp_;
    av::NetAddress udpServer_;
    av::proto::Welcome welcome_;
    av::proto::PlayerStatus status_;
    std::map<std::uint16_t, std::string> roster_;
    std::vector<av::proto::ScoreEntry> scores_;
    std::vector<av::proto::Snapshot> snapshots_;
    std::vector<av::proto::Event> events_;
    std::vector<av::proto::ChatLine> chat_;
    std::uint32_t newestTick_ = 0;
    int udpHelloCountdown_ = 0;
    std::string reason_;
    std::size_t bytesIn_ = 0, bytesOut_ = 0;
};

} // namespace client
