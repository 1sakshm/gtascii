# Hosting online rooms for free

Friends anywhere can create and join rooms by code ("Create online room" and
"Join with a room code" in the game) once one **room server** is running
somewhere public. The best free place for it is **Oracle Cloud's Always Free
tier**:

- **Cost:** free with no time limit (an Ampere ARM VM with up to 4 cores and 24 GB).
- **Traffic:** 10 TB of outbound data per month, far more than this game uses.
  Each player takes about 20–25 KB/s.
- **UDP support**, which the game needs. Many free hosts (Render, Vercel,
  Cloudflare, Heroku-style platforms) only allow HTTP.

Oracle asks for a card to verify your identity but doesn't charge an
Always Free VM. Any other Linux VPS works the same way.

## 1. Create the VM

1. Sign up at <https://www.oracle.com/cloud/free/>.
2. Go to **Compute → Instances → Create instance**.
   - **Image:** Canonical Ubuntu 24.04.
   - **Shape:** Ampere `VM.Standard.A1.Flex` with 1 OCPU and 6 GB (Always Free eligible).
   - Add your SSH key and create the instance.
3. Open the game port in Oracle's firewall:
   - Go to **Networking → Virtual cloud networks → your VCN → Security lists → Default**.
   - Add two ingress rules from source `0.0.0.0/0`: **TCP 41986** and **UDP 41986**.

## 2. Install the server (one command)

SSH into the VM and run:

```bash
curl -fsSL https://raw.githubusercontent.com/1sakshm/gtascii/main/deploy/install-server.sh | sudo bash
```

If you'd rather not pipe a script into a shell, copy `deploy/install-server.sh`
to the VM and run `sudo bash install-server.sh`. The script:

- installs Node.js and `npm install -g gtascii`, which pulls in the
  prebuilt Linux ARM or x64 server
- opens port 41986 (TCP and UDP) in the VM's own iptables firewall, which
  Oracle's Ubuntu images block by default
- installs and starts the `gtascii-server` systemd service, which runs as an
  unprivileged user, restarts on failure and starts on boot

Check that it's running:

```bash
systemctl status gtascii-server
journalctl -u gtascii-server -f      # live log: rooms created, players joining, chat
```

## 3. Point the game at it

In the game, go to **Multiplayer → Room server** and type your VM's public
IP, or a domain name pointing at it. Then everyone can use **Create online
room**, **Join with a room code** and **Browse rooms**.

To make your server the default for everyone who installs your build, set
`"lobby"` in `config/client.json` before you publish.

## Updating

```bash
sudo npm install -g gtascii@latest && sudo systemctl restart gtascii-server
```

## Running elsewhere

- **Your own PC as a server:** `gtascii-server --rooms`. You'll need to
  forward TCP and UDP 41986 on your router.
- **Docker / other VPS:** any Linux machine with Node.js 18+ works. Run
  `npx gtascii-server --rooms`.
