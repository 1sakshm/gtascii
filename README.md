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
| `Space` | jump | `Space` | handbrake |
| `C` | crouch | `F` | get out |
| `F` | get in, carjack, or ride along when a friend is driving | | |
| `E` | use: start a mission or race, save, buy a car | | |
| `X` | punch | | |
| `G` | wave | | |

Both modes share these keys:

| Key | Action |
|---|---|
| Arrows / left-drag | orbit the camera |
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

### Sound

The sound is synthesised live; there are no audio files:

- footsteps, punches, crashes and pickups
- engine pitch that follows your speed
- police sirens
- mission and race stingers
- a procedural synthwave radio in cars

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

There are three ways to play together. They all use one port, **41986**
(TCP and UDP).

1. **Online rooms.** These go through the room server named in Settings, under
   *Room server*. One server hosts any number of rooms, each with its own
   city and its own save. Create a room, then share the 4-letter code.
2. **LAN.** Pick *Create Room* while no room server is set, or run
   `gtascii --host`. Other players on the same network see the room under
   *Browse Rooms*.
3. **Direct connect.** Use `gtascii --connect 1.2.3.4`. Players outside your
   network need TCP and UDP port 41986 forwarded.

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

### Hosting the free room server

The cheapest good option is an **Oracle Cloud "Always Free" VM**. It's free
indefinitely and has a public IP. `deploy/README.md` walks through it. In
short, on the VM:

```sh
sudo bash install-server.sh     # from deploy/
```

The script installs Node and `gtascii`, opens port 41986, and runs
`gtascii server --rooms` as a systemd service.

Then set `network.lobby` in `config/client.json` to the VM's IP before
releasing, so everyone gets it by default. Players can also change it in
Settings.

Any other Linux box works the same way, including a Raspberry Pi with the
port forwarded. For a quick test on one machine:

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
ctest --test-dir build --output-on-failure     # 103 tests
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

## Tests (103)

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
  - a full race from gathering to podium
  - a Cops & Robbers round

## Known limitations

- **Platforms**: Linux and macOS builds are checked by CI. The POSIX terminal and socket code has had less hands-on testing than Windows.
- **Audio**: sound only plays on Windows. Other platforms run silently.
- **Snapshots**: a snapshot carries the 40-45 nearest entities, so in very crowded areas the farthest ones drop out.
- **Security**: passwords are hashed on the server but sent in plain text, so don't reuse a real password.
- **Name**: "Grand Theft" is close to a famous trademark. Consider that before publishing widely.

## License

MIT, see [LICENSE](LICENSE).
