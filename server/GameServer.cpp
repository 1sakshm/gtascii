#include "GameServer.h"

#include <chrono>

namespace server {

using namespace av;
using namespace av::proto;

namespace {
constexpr double kClientTimeout = 20.0; // seconds without UDP or TCP traffic

std::string sanitize(const std::string& s, std::size_t maxLen)
{
    std::string out;
    for (const char ch : s) {
        if (static_cast<unsigned char>(ch) >= 0x20 && ch != 0x7F) {
            out += ch;
        }
        if (out.size() >= maxLen) {
            break;
        }
    }
    return out;
}

std::string upper(std::string s)
{
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}
} // namespace

GameServer::GameServer(const ServerConfig& config, const std::filesystem::path& dataRoot)
    : config_(config)
    , dataRoot_(dataRoot)
    , rng_(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
    if (config_.mainRoom) {
        createRoom(config_.roomName, config_.mode, true, config_.maxPlayers, true);
    }
}

GameServer::~GameServer()
{
    for (auto& [code, room] : rooms_) {
        room->world->save();
    }
}

ServerWorld* GameServer::mainWorld()
{
    for (auto& [code, room] : rooms_) {
        if (room->permanent) {
            return room->world.get();
        }
    }
    return nullptr;
}

std::string GameServer::newRoomCode()
{
    // Unambiguous characters only (no 0/O, 1/I).
    static const char kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (;;) {
        std::string code;
        for (int i = 0; i < 4; ++i) {
            code += kChars[rng_.next() % (sizeof kChars - 1)];
        }
        if (rooms_.count(code) == 0) {
            return code;
        }
    }
}

GameServer::Room* GameServer::createRoom(const std::string& name, GameMode mode, bool isPublic, int maxPlayers, bool permanent)
{
    auto room = std::make_unique<Room>();
    room->code = permanent ? config_.mainRoomCode : newRoomCode();
    room->name = sanitize(name.empty() ? "Room" : name, 24);
    room->mode = mode;
    room->isPublic = isPublic;
    room->permanent = permanent;
    room->maxPlayers = std::clamp(maxPlayers, 1, 16);
    room->emptySince = clock_;
    ServerConfig rc = config_;
    rc.mode = mode;
    rc.maxPlayers = room->maxPlayers;
    if (!permanent) {
        rc.savePath.clear(); // online rooms are sessions; nothing is written to disk
    }
    room->world = std::make_unique<ServerWorld>(rc, dataRoot_);
    Room* raw = room.get();
    rooms_[room->code] = std::move(room);
    emit("room " + raw->code + " \"" + raw->name + "\" created (" + toString(mode) + ")");
    return raw;
}

GameServer::Room* GameServer::findRoom(const std::string& code)
{
    const auto it = rooms_.find(upper(code));
    return it == rooms_.end() ? nullptr : it->second.get();
}

std::vector<RoomInfo> GameServer::roomList(bool publicOnly) const
{
    std::vector<RoomInfo> out;
    for (const auto& [code, room] : rooms_) {
        if (publicOnly && !room->isPublic) {
            continue;
        }
        RoomInfo ri;
        ri.code = code;
        ri.name = room->name;
        ri.mode = room->mode;
        ri.players = static_cast<std::uint8_t>(room->world->playerCount());
        ri.maxPlayers = static_cast<std::uint8_t>(room->maxPlayers);
        ri.isPublic = room->isPublic;
        out.push_back(ri);
    }
    return out;
}

bool GameServer::start(std::string& error)
{
    if (!listener_.listen(config_.port, config_.loopbackOnly)) {
        error = "cannot listen on TCP port " + std::to_string(config_.port) + " (already in use?)";
        return false;
    }
    if (!udp_.open(listener_.localPort(), config_.loopbackOnly) && !udp_.open(0, config_.loopbackOnly)) {
        error = "cannot open a UDP socket";
        return false;
    }
    if (config_.lanDiscovery && !config_.loopbackOnly) {
        discovery_.open(kDiscoveryPort, false, true); // best effort: another server may own it
    }
    emit("listening on TCP " + std::to_string(port()) + ", UDP " + std::to_string(udpPort()) +
         (config_.loopbackOnly ? " (local only)" : "") + (config_.roomServer ? " - room server" : ""));
    return true;
}

std::uint16_t GameServer::port() const { return listener_.localPort(); }
std::uint16_t GameServer::udpPort() const { return udp_.localPort(); }

void GameServer::emit(const std::string& line)
{
    if (log) {
        log(line);
    }
}

void GameServer::run(const std::atomic<bool>& stop)
{
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / config_.tickRate));
    auto next = Clock::now();
    while (!stop.load()) {
        step(1.0f / static_cast<float>(config_.tickRate));
        next += period;
        const auto now = Clock::now();
        if (now > next + period * 5) {
            next = now;
        }
        std::this_thread::sleep_until(next);
    }
    for (auto& [code, room] : rooms_) {
        room->world->save();
    }
}

void GameServer::step(float dt)
{
    clock_ += dt;
    acceptClients();
    pollTcp();
    pollUdp();
    pollDiscovery();
    serviceRooms(dt);
    for (auto& c : clients_) {
        c->tcp->flush();
    }
    for (std::size_t i = clients_.size(); i-- > 0;) {
        if (!clients_[i]->tcp->connected()) {
            dropClient(i, "disconnected");
        } else if (clock_ - clients_[i]->lastHeard > kClientTimeout) {
            dropClient(i, "timed out");
        }
    }
    // Close created rooms that have stayed empty.
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        Room& r = *it->second;
        if (r.world->playerCount() > 0) {
            r.emptySince = clock_;
        }
        if (!r.permanent && clock_ - r.emptySince > config_.emptyRoomSeconds) {
            emit("room " + r.code + " closed (empty)");
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

void GameServer::serviceRooms(float dt)
{
    for (auto& [code, roomPtr] : rooms_) {
        Room* room = roomPtr.get();
        if (room->world->playerCount() == 0 && !room->permanent) {
            continue; // idle rooms don't simulate
        }
        room->world->tick(dt);

        for (const OutgoingEvent& ev : room->world->takeEvents()) {
            const auto payload = encode(ev.event);
            for (auto& c : clients_) {
                if (c->room == room && c->player != kNullEntity && (ev.target == kNullEntity || ev.target == c->player)) {
                    c->tcp->send(static_cast<std::uint8_t>(Tcp::Event), payload);
                }
            }
        }

        room->statusAccumulator += dt;
        if (room->statusAccumulator >= 0.2f) {
            room->statusAccumulator = 0.0f;
            for (auto& c : clients_) {
                if (c->room != room || c->player == kNullEntity) continue;
                const PlayerStatus st = room->world->buildStatus(c->player);
                if (!c->statusSent || !(st == c->lastStatus)) {
                    c->tcp->send(static_cast<std::uint8_t>(Tcp::Status), encode(st));
                    c->lastStatus = st;
                    c->statusSent = true;
                }
            }
        }
        room->scoreAccumulator += dt;
        if (room->scoreAccumulator >= 1.0f) {
            room->scoreAccumulator = 0.0f;
            const std::vector<ScoreEntry> scores = room->world->scoreboard();
            const auto payload = encodeScoreboard(scores);
            for (auto& c : clients_) {
                if (c->room == room && c->player != kNullEntity) {
                    c->tcp->send(static_cast<std::uint8_t>(Tcp::Scoreboard), payload);
                }
            }
        }

        room->snapshotAccumulator += dt;
        const float period = 1.0f / static_cast<float>(config_.snapshotRate);
        if (room->snapshotAccumulator >= period) {
            room->snapshotAccumulator = std::fmod(room->snapshotAccumulator, period);
            for (auto& c : clients_) {
                if (c->room != room || c->player == kNullEntity || !c->hasUdp) continue;
                const auto packet = encodeSnapshot(c->session, room->world->buildSnapshot(c->player));
                udp_.sendTo(c->udp, packet.data(), packet.size());
            }
        }
    }
}

void GameServer::acceptClients()
{
    while (auto stream = listener_.accept()) {
        auto c = std::make_unique<Client>();
        c->tcp = std::move(stream);
        c->lastHeard = clock_;
        do {
            c->session = rng_.next();
        } while (c->session == 0);
        clients_.push_back(std::move(c));
    }
}

void GameServer::sendToRoom(Room* room, Tcp type, const std::vector<std::uint8_t>& payload)
{
    for (auto& c : clients_) {
        if (c->room == room && c->player != kNullEntity) {
            c->tcp->send(static_cast<std::uint8_t>(type), payload);
        }
    }
}

void GameServer::handleHello(Client& c, const std::vector<std::uint8_t>& payload)
{
    const auto hello = decodeHello(payload);
    auto reject = [&](const std::string& why) {
        c.tcp->send(static_cast<std::uint8_t>(Tcp::Reject), encode(ChatLine{"", why, true}));
        c.tcp->flush();
        c.tcp->close();
    };
    if (!hello || hello->version != kProtocolVersion) {
        reject("Incompatible game version - please update Grand Theft ASCII.");
        return;
    }
    Room* room = hello->room.empty() ? findRoom(config_.mainRoomCode) : findRoom(hello->room);
    if (!room) {
        reject(hello->room.empty() ? "This server only hosts rooms - join one with a room code."
                                   : "No room with code " + upper(hello->room) + ".");
        return;
    }
    if (static_cast<int>(room->world->playerCount()) >= room->maxPlayers) {
        reject("Room " + room->code + " is full.");
        return;
    }
    std::string error;
    const Entity player = room->world->joinPlayer(sanitize(hello->name, 16), hello->password, hello->appearanceSeed, c.session, error);
    if (player == kNullEntity) {
        reject(error);
        return;
    }
    c.room = room;
    c.player = player;
    c.name = room->world->playerName(player);

    Welcome w;
    w.session = c.session;
    w.netId = room->world->netIdOf(player);
    w.worldSeed = room->world->seed();
    w.udpPort = udpPort();
    w.tickRate = static_cast<std::uint8_t>(config_.tickRate);
    w.hour = room->world->hour();
    w.motd = config_.motd;
    w.roomCode = room->code;
    w.roomName = room->name;
    w.mode = room->mode;
    c.tcp->send(static_cast<std::uint8_t>(Tcp::Welcome), encode(w));

    for (auto& other : clients_) {
        if (other->room == room && other->player != kNullEntity && other.get() != &c) {
            c.tcp->send(static_cast<std::uint8_t>(Tcp::Roster), encode(RosterEntry{room->world->netIdOf(other->player), other->name, true}));
        }
    }
    sendToRoom(room, Tcp::Roster, encode(RosterEntry{w.netId, c.name, true}));
    emit(c.name + " joined room " + room->code + " from " + c.tcp->peer().toString());
}

void GameServer::handleChat(Client& c, const std::string& raw)
{
    const std::string text = sanitize(raw, 120);
    if (text.empty() || !c.room) {
        return;
    }
    if (text[0] == '/') {
        std::string reply;
        if (text == "/players") {
            reply = std::to_string(c.room->world->playerCount()) + " in room " + c.room->code + ":";
            for (auto& o : clients_) {
                if (o->room == c.room && o->player != kNullEntity) reply += " " + o->name;
            }
        } else if (text == "/time") {
            c.room->world->command(c.player, CommandKind::SkipHour, 1);
            reply = "Time is now " + std::to_string(static_cast<int>(c.room->world->hour())) + ":00";
        } else if (text == "/race") {
            c.room->world->command(c.player, CommandKind::StartRace, 0);
            return;
        } else if (text == "/cops") {
            c.room->world->command(c.player, CommandKind::StartCopsAndRobbers, 0);
            return;
        } else if (text == "/room") {
            reply = "Room " + c.room->code + " \"" + c.room->name + "\" - " + toString(c.room->mode) +
                    " - share the code so friends can join";
        } else {
            reply = "Commands: /players, /room, /race, /cops, /time";
        }
        c.tcp->send(static_cast<std::uint8_t>(Tcp::ChatOut), encode(ChatLine{"server", reply, true}));
        return;
    }
    sendToRoom(c.room, Tcp::ChatOut, encode(ChatLine{c.name, text, false}));
    emit("[" + c.room->code + "] <" + c.name + "> " + text);
}

void GameServer::pollTcp()
{
    std::vector<TcpStream::Frame> frames;
    for (auto& cp : clients_) {
        Client& c = *cp;
        frames.clear();
        c.tcp->receive(frames);
        for (const TcpStream::Frame& f : frames) {
            c.lastHeard = clock_;
            const auto type = static_cast<Tcp>(f.type);
            if (c.player == kNullEntity) {
                // Lobby requests and the join handshake.
                if (type == Tcp::Hello) {
                    handleHello(c, f.payload);
                } else if (type == Tcp::ListRooms) {
                    c.tcp->send(static_cast<std::uint8_t>(Tcp::RoomList), encodeRoomList(roomList(true)));
                } else if (type == Tcp::CreateRoom) {
                    const auto req = decodeCreateRoom(f.payload);
                    const int created = static_cast<int>(rooms_.size()) - (config_.mainRoom ? 1 : 0);
                    if (!req || !config_.roomServer) {
                        c.tcp->send(static_cast<std::uint8_t>(Tcp::Reject), encode(ChatLine{"", "This server does not host rooms.", true}));
                    } else if (created >= config_.maxRooms) {
                        c.tcp->send(static_cast<std::uint8_t>(Tcp::Reject), encode(ChatLine{"", "The server is at its room limit - try later.", true}));
                    } else {
                        Room* r = createRoom(req->name, req->mode, req->isPublic, req->maxPlayers, false);
                        ByteWriter w;
                        w.str(r->code);
                        c.tcp->send(static_cast<std::uint8_t>(Tcp::RoomCreated), w.data());
                    }
                }
                continue;
            }
            if (type == Tcp::Chat) {
                if (const auto line = decodeChat(f.payload)) handleChat(c, line->text);
            } else if (type == Tcp::Command) {
                ByteReader r(f.payload);
                const auto kind = static_cast<CommandKind>(r.u8());
                const int arg = r.i32();
                if (r.ok()) c.room->world->command(c.player, kind, arg);
            }
        }
    }
}

void GameServer::pollUdp()
{
    std::uint8_t buf[2048];
    NetAddress from;
    for (int guard = 0; guard < 1024; ++guard) {
        const int n = udp_.receiveFrom(from, buf, sizeof buf);
        if (n <= 0) {
            break;
        }
        ByteReader r(buf, static_cast<std::size_t>(n));
        const auto header = decodeUdpHeader(r);
        if (!header) {
            continue;
        }
        Client* client = nullptr;
        for (auto& c : clients_) {
            if (c->session == header->session && c->player != kNullEntity) {
                client = c.get();
                break;
            }
        }
        if (!client) {
            continue;
        }
        client->hasUdp = true;
        client->udp = from;
        client->lastHeard = clock_;
        if (header->type == Udp::Input) {
            if (const auto frames = decodeInputs(r)) {
                client->room->world->queueInput(client->player, *frames);
            }
        }
    }
}

void GameServer::pollDiscovery()
{
    if (!discovery_.isOpen()) {
        return;
    }
    std::uint8_t buf[256];
    NetAddress from;
    for (int guard = 0; guard < 32; ++guard) {
        const int n = discovery_.receiveFrom(from, buf, sizeof buf);
        if (n <= 0) {
            break;
        }
        if (isDiscoveryQuery(buf, static_cast<std::size_t>(n))) {
            const auto reply = encodeDiscoveryReply(port(), roomList(true));
            discovery_.sendTo(from, reply.data(), reply.size());
        }
    }
}

void GameServer::dropClient(std::size_t index, const std::string& reason)
{
    Client& c = *clients_[index];
    if (c.player != kNullEntity && c.room) {
        const std::uint16_t netId = c.room->world->netIdOf(c.player);
        c.room->world->leavePlayer(c.player);
        Room* room = c.room;
        const std::string name = c.name;
        clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
        sendToRoom(room, Tcp::Roster, encode(RosterEntry{netId, name, false}));
        emit(name + " " + reason + " (room " + room->code + ")");
        return;
    }
    clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
}

EmbeddedServer::EmbeddedServer(const ServerConfig& config, const std::filesystem::path& dataRoot)
    : server_(std::make_unique<GameServer>(config, dataRoot))
{
}

EmbeddedServer::~EmbeddedServer()
{
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool EmbeddedServer::start(std::string& error)
{
    if (!server_->start(error)) {
        return false;
    }
    port_ = server_->port();
    thread_ = std::thread([this] { server_->run(stop_); });
    return true;
}

} // namespace server
