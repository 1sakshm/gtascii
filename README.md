# Grand Theft ASCII (`gtascii`)

An open-world 3D crime game, single and multiplayer, that runs in your
terminal. It's set in a fictional 1980s Miami-style coastal city. Players,
pedestrians, cars, buildings and palm trees are all real 3D geometry,
software-rasterised and lit, then drawn as terminal characters in 24-bit
colour.

The engine, server and client are written from scratch in C++17, with no
third-party dependencies.

## Install and play

```sh
npx gtascii               # try it without installing
npm install -g gtascii    # or install it, then just run: gtascii
```

The package ships prebuilt native binaries for:

- Windows x64
- Linux x64 and arm64
- macOS Intel and Apple Silicon

**Maximise the terminal window** (or shrink the font), because every cell is a pixel.

## Main menu

- **Single Player**: your own city, saved automatically.
- **Multiplayer**:
  - **Create Room**: pick a name, a mode (Free Roam, Races, or Cops & Robbers) and whether the room is public. You get a 4-letter code to give to friends.
  - **Join with Code**: type the code.
  - **Browse Rooms**: public rooms online, plus rooms on your LAN, which are found automatically.
  - **Direct Connect**: an IP address and port.
- **Character**: skin, hair, clothes, hat and backpack, with a live 3D preview.
- **Settings**:
  - name, password, volume and music
  - quality, FOV, draw distance and mouse
  - the room server address
- **Controls**: the key reference.

Use the arrow keys and Enter, or the mouse. Settings and saves live in
`%APPDATA%\gtascii` (Windows) or `~/.config/gtascii` (Linux and macOS). Set
`GTASCII_HOME` to change that.

### Controls

| On foot | | Driving | |
|---|---|---|---|
| `W A S D` | move (camera-relative) | `W` / `S` | throttle / brake, reverse |
| `Shift` | walk (`R` swaps run/walk) | `A` / `D` | steer |
| `Space` | jump (jetpack: fly up) | `Space` | handbrake (helicopter: up) |
| `C` | crouch | `Z` | helicopter: down |
| `F` | get in, carjack, or ride along when a friend is driving | `F` | get out |
| `E` | use: missions, doors, slot machines, save, buy a car | | |
| `X` | attack / shoot where the camera looks (hold for automatic weapons) | | |
| `Q`, `1`-`9`, `0`, `` ` `` | next weapon, pick a weapon, fists | | |
| `J` | put the jetpack on / take it off | | |
| `G` | wave | | |

Both modes share these keys:

| Key | Action |
|---|---|
| Mouse / trackpad | look around: just move it, no clicking. Rest the pointer near the left or right edge to keep turning. Turn this off in Settings, under *Mouse look* |
| Left click / right click | shoot / next weapon |
| Arrow keys | orbit the camera (optional) |
| `+` / `-` / wheel | zoom |
| `V` | recenter the camera |
| `P` | phone: jobs with GPS, start a race or cops & robbers, radio, recording, stats |
| `Tab` | scoreboard |
| `Enter` | chat (`/players`, `/time`) |
| `M` | minimap |
| `I` | inventory |
| `F9` | start or stop recording to an asciinema `.cast` file |
| `H` | HUD |
| `Esc` | back to the menu |

`gtascii --help` lists every command-line flag. The main ones are:

- `--play`, `--host`, `--connect HOST[:PORT]`, `--room CODE`
- `--name`, `--no-audio`, `--size WxH`

Replay a recording with `asciinema play file.cast`, or turn it into a GIF with
[agg](https://github.com/asciinema/agg).

### Weapons

| Key | Weapon | |
|---|---|---|
| `` ` `` | Fists | always with you |
| `1` | Baseball bat | melee |
| `2` | Knife | melee |
| `3` | Pistol | |
| `4` | Uzi | automatic |
| `5` | Shotgun | 8 pellets, knocks people down |
| `6` | Assault rifle | automatic |
| `7` | Sniper rifle | one shot, long range |
| `8` | Minigun | automatic, 400 rounds |
| `9` | Rocket launcher | explodes on contact; wrecks cars |
| `0` | Grenades | thrown in an arc, 2 s fuse, they bounce |

- **Auto-aim**: you shoot where the camera faces, and the target you'll hit is marked `[ + ]`.
- **Finding weapons**: one of each lies somewhere in town and comes back 45 s after it's picked up. A jetpack waits on the beach, and there's health outside the hospital and the police station.
- **Loot**: anyone who dies drops their cash and weapon. Walk over a drop to pick it up. Dead police always drop a pistol, and when you die you drop 10% of your cash and the weapon you were holding.
- **Police**: from two stars up, they shoot back.
- **Wrecks**: a car that takes enough damage blows up, along with anyone inside.

### Cheat codes

Type a code into chat (`Enter`). Codes never show up in chat, and the phone (`P`) lists them.

| Code | Effect |
|---|---|
| `FULLCLIP` | every weapon with lots of ammo |
| `HEALME` | full health |
| `RICHBOY` | +$50,000 |
| `CALMDOWN` / `MAXWANTED` | clear your wanted level / five stars |
| `FLYBOY` | a helicopter next to you |
| `ROCKETMAN` | a jetpack |
| `SPEEDY` | a Vortex GT sports car |
| `IAMGOD` | invincible (type it again to turn it off) |
| `SUNRISE` / `MIDNIGHT` | morning / night |
| `BOOMTOWN` | every car nearby explodes |
| `LETSPARTY` / `HIGHROLLER` | straight into the club / the casino |

Servers can turn cheats off with `allowCheats: false` in `config/server.json`.

### Vehicles, flying and swimming

- **Cars**: sedan, coupe, van, cab, police cruiser, and the **Vortex GT** sports car. The Vortex GT turns up in traffic sometimes and is for sale at Sunshine Motors.
- **Helicopters**:
  - They wait on the helipads in Bayfront Park and on the lighthouse beach, or you can call one with `FLYBOY`.
  - `W`/`S` fly forward and back, and `A`/`D` turn.
  - `Space` climbs, `Z` descends, and letting go of both makes it hover.
  - It lands on the ground, on rooftops and on water.
- **Jetpack**:
  - `Space` flies up, `Shift` goes fast, and letting go of `Space` makes you drift down slowly.
  - The flight ceiling is 140 m for both the jetpack and the helicopter.
- **Swimming**:
  - Deep water in the canal and out at sea makes you swim. `Shift` swims faster.
  - Out at sea, an invisible wall marks the edge of the map.

### Buildings you can enter

Glowing orange rings mark the doors. Press `E` to go in and `E` at the ring inside to leave.

| Building | Where | What's inside |
|---|---|---|
| Your Apartment | the safehouse | sleep until 8 AM, heal and save (`E` at the bed) |
| Vice Tower Offices | downtown | trading terminals: stake $1000 for a −25% to +45% swing |
| Club Malibu | Ocean Drive | a neon dance floor and a crowd |
| Flamingo Casino | Hotel Flamingo, Ocean Drive | slot machines ($100 a spin, $5,000 jackpot) and card tables |

You can't go inside while the police are after you. Door positions are in
`assets/maps/interiors.json`.

### Secrets

Grand Theft ASCII hides golden eggs around the map, and some odd things you'll
only notice if you go looking. The phone's *Stats* screen counts the eggs
you've found, and finding all of them earns a reward. No spoilers here.

### Sound

The sound is synthesised live; there are no audio files:

- **Footsteps**: soft and a little different every step.
- **Punches, gunfire, explosions and crashes**: filtered, so they stay easy on the ears.
- **Engine**: a four-cylinder rumble whose revs climb through the gears.
- **Helicopters and the jetpack**: a rotor chop and a jetpack hiss.
- **Police sirens**.
- **Mission and race stingers**.
- **Radio**: a procedural synthwave station in cars.

Everything goes through a gentle low-pass filter and a short room echo.

Set the volume in Settings, or turn sound off with `--no-audio`.

### What's in the city

- **Districts:**
  - Downtown, with glass towers and the 140 m Vice Tower
  - The Neon Mile art-deco hotels on Ocean Drive
  - Seabreeze Beach and the Point Lighthouse
  - Residential neighbourhoods: Sunset Flats, Mango Grove, Palmetto Heights, and Coral Shores across the canal
  - Bayfront Park
  - Foundry Yards (industrial) and Harbor Docks, with piers and a gantry crane
  - Canal Walk, where the north-south roads bridge the canal
- **Landmarks**, placed from `assets/maps/landmarks.json`:
  - your safehouse, which is your save point and has your garage
  - the Precinct 86 police station (you respawn here after an arrest)
  - Mercy General Hospital (you respawn here after being hospitalised)
  - Sunshine Motors, the car dealership
  - Hotel Flamingo, Vice Tower and the lighthouse
- **Life:**
  - Pedestrians wander the sidewalks, cross at crosswalks, chat with each other, react when bumped and flee from danger.
  - Traffic drives in lanes on the right, turns at intersections, queues behind other cars and waits for busy intersections.
- **Missions** (`assets/missions/missions.json`): couriers, an on-foot checkpoint race, a driving checkpoint race, a car delivery with a damage limit, and an escape where you lose a 3-star wanted level.
  - Pink diamonds on the map and in the world mark where missions start.
  - Rewards are paid on success; replays pay half.
- **Police:**
  - Assault, carjacking, hitting pedestrians, ramming cruisers and stealing police cars all raise your wanted level (up to 5 stars).
  - Cruisers chase you with sirens. Officers get out and chase on foot, and arrest you if they catch you or if you're boxed in.
  - Stay out of sight for about 16 s to drop a star.
  - Being arrested costs 10% of your cash; being hospitalised costs up to $200.
- **Economy:**
  - Cash comes from missions and from 24 hidden flamingo statuettes ($100 each, plus a $5,000 bonus for finding all of them).
  - Cars bought at Sunshine Motors are yours permanently and are waiting at your safehouse every time you join.

## Multiplayer

There are three ways to play together:

1. **Online rooms.** Use *Multiplayer → Create online room*, then share the
   4-letter code. Friends pick *Join with a room code*, or find public rooms
   under *Browse rooms*.
   - The room runs inside the host's game, and a free Cloudflare relay
     forwards the traffic.
   - Nobody needs a public IP, port forwarding or a server.
   - The room closes when the host leaves.
   - From the command line: `gtascii --create` and `gtascii --join K7PX`.
2. **LAN.** Pick *Host a LAN room*, or run `gtascii --host`. Other players on
   the same network see the room under *Browse rooms*. Online rooms are
   visible on the LAN too.
3. **Direct connect.** Use `gtascii --connect 1.2.3.4`, which also connects
   to a dedicated server. Players outside the host's network need TCP and UDP
   port 41986 forwarded.

### Room modes

- **Free Roam**: missions, police, flamingo hunting and the economy, together.
- **Races**:
  - Start a race from the phone or at a race marker. Everyone nearby gets a coupe on the starting grid.
  - The countdown runs, then you race through the checkpoints.
  - Prizes are $1000, $500, $250 and $100, plus scoreboard points.
  - The courses are in `assets/missions/races.json`.
- **Cops & Robbers**:
  - A third of the players are robbers; their name tags show red.
  - Cops are in uniform. A cop catches a robber by staying close for 1.5 s, which freezes the robber.
  - Robbers win if any of them is still free after 3 minutes.

Players block each other physically, and cars carry up to four people. The
scoreboard (`Tab`) ranks everyone in the room.

### The online relay

`relay/` is a Cloudflare Worker, about 250 lines of JavaScript.

- **Durable Objects:** each room is a Durable Object, and a second one keeps
  the list of public rooms.
- **Records:** the host and each guest keep one WebSocket to the relay. Binary
  messages carry `[kind][peer][length][payload]` records, each holding one
  game TCP frame or one UDP datagram.
- **Batching:** both sides batch records every 50 ms. That stays well inside
  the Workers Free plan, which needs no card and stops for the day instead of
  charging. The plan allows roughly 5-7 hours of 4-player games per day.
- **Game-side bridges:** in the game, `client/Relay` turns the WebSocket back
  into loopback TCP and UDP sockets, so the server and `NetClient` don't know
  a relay exists.
- **TLS:** it comes from the operating system. That's WinHTTP on Windows,
  Secure Transport on macOS, and the system OpenSSL on Linux.

To deploy your own relay:

```sh
cd relay
npx wrangler login     # free Cloudflare account; opens the browser
npx wrangler deploy    # prints https://gtascii-relay.<you>.workers.dev
```

Then put `wss://gtascii-relay.<you>.workers.dev` in `config/client.json`
(`network.lobby`). Players can also change it under *Multiplayer → Room
server*. `npx wrangler dev` runs the relay locally, and the game connects to
it with `--relay ws://127.0.0.1:8787`.

### Dedicated servers

`gtascii server --rooms` runs a room server that keeps rooms running without
a host. Room codes work the same way, so set *Room server* to `host:port` to
use one. `deploy/README.md` sets it up as a service on any Linux machine.
For a quick test on one machine:

```sh
gtascii server --rooms --local
gtascii --connect 127.0.0.1 --name Alice    # two terminals
gtascii --connect 127.0.0.1 --name Bob
```

## Architecture

```text
client/  gtascii            input, prediction, interpolation, rendering, HUD
   |  TCP: hello/welcome, chat, events, status      UDP: inputs (60 Hz) / snapshots (20 Hz)
server/  gtascii-server     authoritative simulation (also embedded by the client)
   |  ServerWorld: ECS registry + systems
   |    PlayerSystem   inputs, enter/exit vehicles, melee, damage, knockdowns
   |    NpcSystem      pedestrian + traffic population, AI, vehicle physics, crashes
   |    PoliceSystem   wanted level, pursuit, arrests
   |    MissionSystem  missions, interactions, pickups, economy
   |    ActivitySystem races, cops & robbers, player collision, scoreboard
   |    Persistence    saves/<room>.json
shared/  av_shared          protocol encoding, PlayerSim (shared movement rules), mission data
engine/  av_engine          Core, Math, Graphics, Animation, Physics, AI, Entities (ECS),
                            Networking, World, Gameplay, Platform
```

### Networking

- **TCP** carries the session and reliable messages: handshake, rejection
  reasons, roster, chat, events (mission passed, arrested, and so on) and
  player status (money, health, wanted, mission, prompt, inventory, sent when
  it changes). Messages are length-prefixed frames.
- **UDP** carries real-time traffic:
  - Each client sends a 60 Hz stream of input frames. Each datagram repeats
    the last 4 frames, so one-shot actions like jumping or getting in a car
    survive packet loss.
  - The server deduplicates frames by sequence number and caps the backlog,
    so a client can't speed itself up.
  - Each player receives an interest-managed snapshot at 20 Hz: the nearest
    entities within 170 m, quantised, up to 1,200 bytes per datagram.
  - The server validates every datagram against its session ID.
- **Server authority.** The server simulates everything at 30 Hz: players,
  NPCs, vehicles, police and missions. Clients never report positions, only
  inputs.
- **Client prediction.** The client applies its inputs immediately using
  `shared/PlayerSim`, the same code the server runs. When a snapshot
  acknowledges input N, the client resets to the server's state at N, replays
  the inputs after it, and fades out any difference. On foot the measured
  correction is 0.00 m. Driving and collisions produce small corrections.
- **Interpolation.** Everyone else is drawn 100 ms in the past, between two
  buffered snapshots. An offset tracks the server clock.
- **Animation replication.** Snapshots carry mode, speed, vertical speed,
  grounded and crouch flags, and an action counter. Each client runs the
  procedural animator locally, so remote characters move just like local ones.

### Simulation (ECS)

`engine/Entities/Registry.h` stores components per type, and entities are just
IDs. Server entities are built from components:

| Entity | Components |
|---|---|
| Pedestrian | Character + NPC + Health + Network |
| Traffic car | Vehicle + DriverAI + Network |
| Police officer | Character + NPC + Police + Health + Network |
| Player | Character + Player + Health + Network |
| Marker | Transform + Interaction + Network |
| Pickup | Transform + Pickup + Network |

- **Pedestrian AI** (`engine/AI/PedestrianBrain`) walks a procedural sidewalk
  graph of block corners and crosswalks (`engine/AI/Navigation`). Its states
  are idle, walking, running, talking, reacting, fleeing and interacting, and
  it detects when it's stuck.
- **Traffic AI** (`engine/AI/TrafficDriver`) follows the lanes, drives Bezier
  turns at intersections, steers by pure pursuit, and controls speed for
  turns, obstacles and blocked intersections.
- **Vehicles** (`engine/Physics/Vehicle`) use a kinematic bicycle model with
  steering, an engine, brakes, reverse and drag. Each car collides as three
  circles against boxes. The model also covers ground following, suspension
  tilt from slopes and weight transfer, damage, drowning, and wheel spin and
  steering for the visuals.
- **Level of detail.** Pedestrians more than 90 m from every player think and
  move at a third of the rate. Anything beyond 140–180 m despawns, and the
  population is refilled 40–130 m from each player.

## Build from source

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build --output-on-failure     # 113 tests
.\build\client\gtascii.exe
```

- **Visual Studio 2022**: use `-G "Visual Studio 17 2022"` and build with `--config Release`.
- **Linux and macOS**: run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`.
- **Runtimes**: the release builds link their runtimes statically, so the binaries run on a fresh machine.

## Releasing to npm

The single `gtascii` package holds the launcher, the game data, and the
native binaries for all 5 platforms (`bin/<platform>/`). The launcher picks
the right binary at run time.

To release:

1. Bump `VERSION`.
2. Push a matching tag: `git tag v1.0.1 && git push --tags`.

`.github/workflows/release.yml` builds and tests every platform, publishes the
package with provenance using the `NPM_TOKEN` repository secret, and creates a
GitHub release with a standalone zip for each platform.

npm holds a newly published package in **Staged Packages** on npmjs.com until
a maintainer approves it with 2FA.

To build and test the package locally:

```sh
node scripts/build-npm.mjs main
node scripts/build-npm.mjs bin win32-x64 path/to/binaries   # holds gtascii(.exe) + gtascii-server(.exe)
node scripts/build-npm.mjs pack                             # dist/npm/gtascii-<version>.tgz
```

## Tests (113)

- **Engine**: maths, renderer, characters and animation, world generation, streaming, collision, vehicles, navigation and traffic.
- **Protocol**:
  - round-trips, including malformed-packet rejection
  - LAN discovery
  - appearance codes
- **End to end over real sockets**:
  - two-client sessions with chat, saves and password rejoin
  - multi-room create, join and rejection
- **Gameplay**:
  - missions, police and pickups
  - shooting a pedestrian who drops cash, a rocket wrecking a car, cheat codes
  - swimming and the edge of the map, jetpack flight, helicopter take-off
  - entering the casino, playing the slots and leaving
  - a full race from gathering to podium
  - a Cops & Robbers round

## Known limitations

- **Online rooms**: they last only as long as the host's game. The free relay
  plan also has a daily limit, roughly 5-7 hours of 4-player games.
- **Platforms**: Linux and macOS builds are checked by CI. The POSIX terminal and socket code has had less hands-on testing than Windows.
- **Audio**: sound only plays on Windows. Other platforms run silently.
- **Snapshots**: a snapshot carries the 40-45 nearest entities, so in very crowded areas the farthest ones drop out.
- **Security**: passwords are hashed on the server but sent in plain text, so don't reuse a real password.
- **Name**: "Grand Theft" is close to a famous trademark. Consider that before publishing widely.

## License

MIT, see [LICENSE](LICENSE).
