#include "Protocol.h"

namespace av::proto {

namespace {

void vec(ByteWriter& w, const Vec3& v)
{
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
}

Vec3 vec(ByteReader& r)
{
    const float x = r.f32(), y = r.f32(), z = r.f32();
    return {x, y, z};
}

template <typename T>
std::optional<T> finish(ByteReader& r, T value)
{
    if (!r.ok()) {
        return std::nullopt;
    }
    return value;
}

void header(ByteWriter& w, Udp type, std::uint32_t session)
{
    w.u16(kUdpMagic);
    w.u8(kProtocolVersion);
    w.u8(static_cast<std::uint8_t>(type));
    w.u32(session);
}

} // namespace

const char* toString(GameMode m)
{
    switch (m) {
    case GameMode::FreeRoam: return "Free Roam";
    case GameMode::Races: return "Street Races";
    case GameMode::CopsAndRobbers: return "Cops & Robbers";
    }
    return "?";
}

bool PlayerStatus::operator==(const PlayerStatus& o) const
{
    if (inventory.size() != o.inventory.size()) {
        return false;
    }
    for (std::size_t i = 0; i < inventory.size(); ++i) {
        if (inventory[i].name != o.inventory[i].name || inventory[i].count != o.inventory[i].count) {
            return false;
        }
    }
    return money == o.money && health == o.health && wanted == o.wanted && missionTitle == o.missionTitle &&
           objective == o.objective && static_cast<int>(missionTimer) == static_cast<int>(o.missionTimer) &&
           marker == o.marker && markerPosition == o.markerPosition && prompt == o.prompt &&
           pickupsFound == o.pickupsFound && pickupsTotal == o.pickupsTotal && missionsDone == o.missionsDone &&
           frozen == o.frozen && playSeconds / 10 == o.playSeconds / 10;
}

std::vector<std::uint8_t> encode(const Hello& m)
{
    ByteWriter w;
    w.u8(m.version);
    w.str(m.name);
    w.str(m.password);
    w.u32(m.appearanceSeed);
    w.str(m.room);
    return w.data();
}

std::optional<Hello> decodeHello(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    Hello m;
    m.version = r.u8();
    m.name = r.str();
    m.password = r.str();
    m.appearanceSeed = r.u32();
    m.room = r.str();
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const Welcome& m)
{
    ByteWriter w;
    w.u32(m.session);
    w.u16(m.netId);
    w.u32(m.worldSeed);
    w.u16(m.udpPort);
    w.u8(m.tickRate);
    w.f32(m.hour);
    w.str(m.motd);
    w.str(m.roomCode);
    w.str(m.roomName);
    w.u8(static_cast<std::uint8_t>(m.mode));
    return w.data();
}

std::optional<Welcome> decodeWelcome(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    Welcome m;
    m.session = r.u32();
    m.netId = r.u16();
    m.worldSeed = r.u32();
    m.udpPort = r.u16();
    m.tickRate = r.u8();
    m.hour = r.f32();
    m.motd = r.str();
    m.roomCode = r.str();
    m.roomName = r.str();
    m.mode = static_cast<GameMode>(std::min<int>(r.u8(), 2));
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const RosterEntry& m)
{
    ByteWriter w;
    w.u16(m.netId);
    w.str(m.name);
    w.u8(m.joined ? 1 : 0);
    return w.data();
}

std::optional<RosterEntry> decodeRoster(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    RosterEntry m;
    m.netId = r.u16();
    m.name = r.str();
    m.joined = r.u8() != 0;
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const ChatLine& m)
{
    ByteWriter w;
    w.str(m.from);
    w.str(m.text);
    w.u8(m.system ? 1 : 0);
    return w.data();
}

std::optional<ChatLine> decodeChat(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    ChatLine m;
    m.from = r.str();
    m.text = r.str();
    m.system = r.u8() != 0;
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const Event& m)
{
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(m.kind));
    w.i32(m.value);
    w.str(m.text);
    return w.data();
}

std::optional<Event> decodeEvent(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    Event m;
    m.kind = static_cast<EventKind>(r.u8());
    m.value = r.i32();
    m.text = r.str();
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const PlayerStatus& m)
{
    ByteWriter w;
    w.i32(m.money);
    w.u8(m.health);
    w.u8(m.wanted);
    w.str(m.missionTitle);
    w.str(m.objective);
    w.f32(m.missionTimer);
    w.u8(static_cast<std::uint8_t>(m.marker));
    vec(w, m.markerPosition);
    w.str(m.prompt);
    w.u16(m.pickupsFound);
    w.u16(m.pickupsTotal);
    w.u8(static_cast<std::uint8_t>(std::min<std::size_t>(m.inventory.size(), 32)));
    for (std::size_t i = 0; i < m.inventory.size() && i < 32; ++i) {
        w.str(m.inventory[i].name);
        w.u16(m.inventory[i].count);
    }
    w.u16(m.missionsDone);
    w.u16(m.missionsTotal);
    w.u32(m.playSeconds);
    w.u8(m.frozen);
    return w.data();
}

std::optional<PlayerStatus> decodeStatus(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    PlayerStatus m;
    m.money = r.i32();
    m.health = r.u8();
    m.wanted = r.u8();
    m.missionTitle = r.str();
    m.objective = r.str();
    m.missionTimer = r.f32();
    m.marker = static_cast<MarkerKind>(r.u8());
    m.markerPosition = vec(r);
    m.prompt = r.str();
    m.pickupsFound = r.u16();
    m.pickupsTotal = r.u16();
    const int n = r.u8();
    for (int i = 0; i < n && r.ok(); ++i) {
        InventoryItem item;
        item.name = r.str();
        item.count = r.u16();
        m.inventory.push_back(item);
    }
    m.missionsDone = r.u16();
    m.missionsTotal = r.u16();
    m.playSeconds = r.u32();
    m.frozen = r.u8();
    return finish(r, m);
}

std::vector<std::uint8_t> encode(const CreateRoomRequest& m)
{
    ByteWriter w;
    w.str(m.name);
    w.u8(static_cast<std::uint8_t>(m.mode));
    w.u8(m.isPublic ? 1 : 0);
    w.u8(m.maxPlayers);
    return w.data();
}

std::optional<CreateRoomRequest> decodeCreateRoom(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    CreateRoomRequest m;
    m.name = r.str();
    m.mode = static_cast<GameMode>(std::min<int>(r.u8(), 2));
    m.isPublic = r.u8() != 0;
    m.maxPlayers = r.u8();
    return finish(r, m);
}

namespace {
void writeRooms(ByteWriter& w, const std::vector<RoomInfo>& rooms)
{
    const std::size_t n = std::min<std::size_t>(rooms.size(), 64);
    w.u8(static_cast<std::uint8_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        const RoomInfo& ri = rooms[i];
        w.str(ri.code);
        w.str(ri.name);
        w.u8(static_cast<std::uint8_t>(ri.mode));
        w.u8(ri.players);
        w.u8(ri.maxPlayers);
        w.u8(ri.isPublic ? 1 : 0);
    }
}

bool readRooms(ByteReader& r, std::vector<RoomInfo>& out)
{
    const int n = r.u8();
    for (int i = 0; i < n && r.ok(); ++i) {
        RoomInfo ri;
        ri.code = r.str();
        ri.name = r.str();
        ri.mode = static_cast<GameMode>(std::min<int>(r.u8(), 2));
        ri.players = r.u8();
        ri.maxPlayers = r.u8();
        ri.isPublic = r.u8() != 0;
        out.push_back(ri);
    }
    return r.ok();
}
} // namespace

std::vector<std::uint8_t> encodeRoomList(const std::vector<RoomInfo>& rooms)
{
    ByteWriter w;
    writeRooms(w, rooms);
    return w.data();
}

std::optional<std::vector<RoomInfo>> decodeRoomList(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    std::vector<RoomInfo> rooms;
    if (!readRooms(r, rooms)) {
        return std::nullopt;
    }
    return rooms;
}

std::vector<std::uint8_t> encodeScoreboard(const std::vector<ScoreEntry>& scores)
{
    ByteWriter w;
    const std::size_t n = std::min<std::size_t>(scores.size(), 32);
    w.u8(static_cast<std::uint8_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        w.str(scores[i].name);
        w.i32(scores[i].points);
        w.str(scores[i].role);
    }
    return w.data();
}

std::optional<std::vector<ScoreEntry>> decodeScoreboard(const std::vector<std::uint8_t>& p)
{
    ByteReader r(p);
    std::vector<ScoreEntry> out;
    const int n = r.u8();
    for (int i = 0; i < n && r.ok(); ++i) {
        ScoreEntry e;
        e.name = r.str();
        e.points = r.i32();
        e.role = r.str();
        out.push_back(e);
    }
    return finish(r, out);
}

std::vector<std::uint8_t> encodeDiscoveryQuery()
{
    ByteWriter w;
    w.u16(kDiscoveryMagic);
    w.u8(kProtocolVersion);
    w.u8(1);
    return w.data();
}

std::vector<std::uint8_t> encodeDiscoveryReply(std::uint16_t gamePort, const std::vector<RoomInfo>& rooms)
{
    ByteWriter w;
    w.u16(kDiscoveryMagic);
    w.u8(kProtocolVersion);
    w.u8(2);
    w.u16(gamePort);
    writeRooms(w, rooms);
    return w.data();
}

bool isDiscoveryQuery(const std::uint8_t* data, std::size_t size)
{
    ByteReader r(data, size);
    const bool ok = r.u16() == kDiscoveryMagic && r.u8() == kProtocolVersion && r.u8() == 1;
    return ok && r.ok();
}

std::optional<std::pair<std::uint16_t, std::vector<RoomInfo>>> decodeDiscoveryReply(const std::uint8_t* data, std::size_t size)
{
    ByteReader r(data, size);
    if (r.u16() != kDiscoveryMagic || r.u8() != kProtocolVersion || r.u8() != 2) {
        return std::nullopt;
    }
    const std::uint16_t port = r.u16();
    std::vector<RoomInfo> rooms;
    if (!readRooms(r, rooms)) {
        return std::nullopt;
    }
    return std::make_pair(port, rooms);
}

std::vector<std::uint8_t> encodeUdpHello(std::uint32_t session)
{
    ByteWriter w;
    header(w, Udp::Hello, session);
    return w.data();
}

std::vector<std::uint8_t> encodeInputs(std::uint32_t session, const std::vector<InputFrame>& frames)
{
    ByteWriter w;
    header(w, Udp::Input, session);
    const std::size_t n = std::min<std::size_t>(frames.size(), 16);
    w.u8(static_cast<std::uint8_t>(n));
    for (std::size_t i = frames.size() - n; i < frames.size(); ++i) {
        const InputFrame& f = frames[i];
        w.u32(f.seq);
        w.i8(f.moveX);
        w.i8(f.moveZ);
        w.u16(f.buttons);
        w.i8(f.throttle);
        w.i8(f.steer);
    }
    return w.data();
}

std::optional<UdpHeader> decodeUdpHeader(ByteReader& r)
{
    const std::uint16_t magic = r.u16();
    const std::uint8_t version = r.u8();
    const auto type = static_cast<Udp>(r.u8());
    const std::uint32_t session = r.u32();
    if (!r.ok() || magic != kUdpMagic || version != kProtocolVersion) {
        return std::nullopt;
    }
    return UdpHeader{type, session};
}

std::optional<std::vector<InputFrame>> decodeInputs(ByteReader& r)
{
    const int n = r.u8();
    std::vector<InputFrame> frames;
    for (int i = 0; i < n && r.ok(); ++i) {
        InputFrame f;
        f.seq = r.u32();
        f.moveX = r.i8();
        f.moveZ = r.i8();
        f.buttons = r.u16();
        f.throttle = r.i8();
        f.steer = r.i8();
        frames.push_back(f);
    }
    if (!r.ok()) {
        return std::nullopt;
    }
    return frames;
}

std::vector<std::uint8_t> encodeSnapshot(std::uint32_t session, const Snapshot& s)
{
    ByteWriter w;
    header(w, Udp::Snapshot, session);
    w.u32(s.tick);
    w.f32(s.serverTime);
    w.f32(s.hour);
    w.u32(s.ackInput);

    const SelfState& me = s.self;
    w.u8(static_cast<std::uint8_t>(me.mode));
    vec(w, me.position);
    vec(w, me.velocity);
    w.f32(me.yaw);
    w.u8(me.grounded ? 1 : 0);
    w.u16(me.vehicle);
    w.u8(me.seat);
    if (me.vehicle != 0 && me.seat == 0) {
        vec(w, me.vehiclePosition);
        w.f32(me.vehicleYaw);
        w.f32(me.vehicleSpeed);
        w.f32(me.vehicleSteer);
        w.f32(me.vehicleDamage);
    }

    // Entities until the datagram is full.
    std::size_t countPos = w.size();
    w.u8(0);
    std::uint8_t count = 0;
    std::vector<std::uint8_t> out = w.data();
    for (const EntityState& e : s.entities) {
        ByteWriter ew;
        ew.u16(e.netId);
        ew.u8(static_cast<std::uint8_t>(e.kind));
        ew.u8(e.flags);
        vec(ew, e.position);
        ew.angle16(e.yaw);
        switch (e.kind) {
        case EntityKind::Character:
            ew.u32(e.appearance);
            ew.u8(static_cast<std::uint8_t>(static_cast<std::uint8_t>(e.mode) | (e.grounded ? 0x10 : 0) | (e.crouch ? 0x20 : 0)));
            ew.u8(e.action);
            ew.unorm8(e.speed, 12.0f);
            ew.snorm8(e.verticalSpeed, 20.0f);
            ew.u16(e.vehicle);
            break;
        case EntityKind::Vehicle:
            ew.u8(e.model);
            ew.u8(e.paint);
            ew.snorm8(e.steer, 0.8f);
            ew.unorm8(e.wheelSpin, 6.2831853f);
            ew.snorm8(e.pitch, 0.3f);
            ew.snorm8(e.roll, 0.3f);
            ew.unorm8(e.damage, 1.0f);
            ew.snorm8(e.speed, 45.0f);
            break;
        case EntityKind::Pickup:
        case EntityKind::Marker:
            ew.u8(e.subtype);
            break;
        }
        if (out.size() + ew.size() > kMaxUdpPayload || count == 255) {
            break;
        }
        out.insert(out.end(), ew.data().begin(), ew.data().end());
        ++count;
    }
    out[countPos] = count;
    return out;
}

std::optional<Snapshot> decodeSnapshot(ByteReader& r)
{
    Snapshot s;
    s.tick = r.u32();
    s.serverTime = r.f32();
    s.hour = r.f32();
    s.ackInput = r.u32();
    SelfState& me = s.self;
    me.mode = static_cast<CharacterMode>(r.u8());
    me.position = vec(r);
    me.velocity = vec(r);
    me.yaw = r.f32();
    me.grounded = r.u8() != 0;
    me.vehicle = r.u16();
    me.seat = r.u8();
    if (me.vehicle != 0 && me.seat == 0) {
        me.vehiclePosition = vec(r);
        me.vehicleYaw = r.f32();
        me.vehicleSpeed = r.f32();
        me.vehicleSteer = r.f32();
        me.vehicleDamage = r.f32();
    }
    const int n = r.u8();
    for (int i = 0; i < n && r.ok(); ++i) {
        EntityState e;
        e.netId = r.u16();
        e.kind = static_cast<EntityKind>(r.u8());
        e.flags = r.u8();
        e.position = vec(r);
        e.yaw = r.angle16();
        switch (e.kind) {
        case EntityKind::Character: {
            e.appearance = r.u32();
            const std::uint8_t m = r.u8();
            e.mode = static_cast<CharacterMode>(m & 0x0F);
            e.grounded = (m & 0x10) != 0;
            e.crouch = (m & 0x20) != 0;
            e.action = r.u8();
            e.speed = r.unorm8(12.0f);
            e.verticalSpeed = r.snorm8(20.0f);
            e.vehicle = r.u16();
            break;
        }
        case EntityKind::Vehicle:
            e.model = r.u8();
            e.paint = r.u8();
            e.steer = r.snorm8(0.8f);
            e.wheelSpin = r.unorm8(6.2831853f);
            e.pitch = r.snorm8(0.3f);
            e.roll = r.snorm8(0.3f);
            e.damage = r.unorm8(1.0f);
            e.speed = r.snorm8(45.0f);
            break;
        case EntityKind::Pickup:
        case EntityKind::Marker:
            e.subtype = r.u8();
            break;
        default:
            return std::nullopt;
        }
        s.entities.push_back(e);
    }
    if (!r.ok()) {
        return std::nullopt;
    }
    return s;
}

} // namespace av::proto
