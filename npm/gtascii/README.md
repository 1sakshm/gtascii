# Grand Theft ASCII

A multiplayer open-world 3D game that runs in your terminal. Walk, drive,
race and run from the cops through a neon 1980s coastal city. Everything is
real 3D, software-rendered into coloured text.

```bash
npx gtascii            # play right now
npm install -g gtascii # or install it, then run: gtascii
```

**Best in** Windows Terminal, iTerm2, kitty, WezTerm, GNOME Terminal or any
terminal with 24-bit colour. Make the window big, or the font small.

## Multiplayer

- **Online rooms**: in the main menu, go to Multiplayer → Create online room
  and share the 4-letter code. Friends pick Join with a room code.
  This needs a room server (see below).
- **LAN**: Multiplayer → Host a LAN room. Players on the same network see it
  under Browse rooms.
- **Direct**: `gtascii --connect host[:port]`.

### Run a room server (free)

On any Linux machine or VPS (Oracle Cloud's Always Free ARM VM works well),
run `npx gtascii-server --rooms`, then open TCP and UDP port 41986. Players
set the address under Multiplayer → Room server.

## Controls

| Key | Action |
|---|---|
| WASD | move |
| Shift | walk |
| Space | jump |
| C | crouch |
| F | enter or exit a car |
| E | use |
| X | punch |
| G | wave |
| P | phone: jobs, races, cops & robbers, radio |
| Arrows or mouse drag | camera |
| Enter | chat |
| Tab | scoreboard |
| M | map |
| I | items |
| F9 | record your session |
| Q | quit to menu |

Supported platforms: Windows x64, Linux x64 and arm64, macOS x64 and Apple
Silicon.
