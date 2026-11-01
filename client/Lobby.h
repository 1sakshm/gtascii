#pragma once

#include "Protocol.h"

#include "Networking/Net.h"

#include <optional>
#include <string>
#include <vector>

namespace client {

// Short-lived lobby requests against a room server (blocking, with timeouts).
// Each opens its own TCP connection; the game connection is separate.
std::optional<std::vector<av::proto::RoomInfo>> listRooms(const av::NetAddress& server, std::string& error, int timeoutMs = 3000);
std::optional<std::string> createRoom(const av::NetAddress& server, const av::proto::CreateRoomRequest& request,
                                      std::string& error, int timeoutMs = 3000);

// Broadcasts a discovery query on the local network and collects the rooms
// hosted by every server that answers within `timeoutMs`.
std::vector<av::proto::RoomInfo> discoverLanRooms(int timeoutMs = 700);

// Room codes are typed by people: accept "abcd", " ABCD ", "gta-abcd".
std::string normalizeRoomCode(const std::string& text);

} // namespace client
