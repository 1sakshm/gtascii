#!/usr/bin/env bash
# Installs a Grand Theft ASCII room server as a systemd service.
# Tested target: Ubuntu 22.04 / 24.04 (x64 or ARM, e.g. Oracle Cloud Always Free).
set -euo pipefail

PORT="${GTASCII_PORT:-41986}"
USER_NAME=gtascii

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root (sudo bash install-server.sh)" >&2
    exit 1
fi

echo "==> Installing Node.js"
if ! command -v node >/dev/null 2>&1 || [ "$(node -p 'process.versions.node.split(".")[0]')" -lt 18 ]; then
    apt-get update -y
    apt-get install -y ca-certificates curl gnupg
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
fi

echo "==> Installing gtascii"
npm install -g gtascii@latest
BIN="$(command -v gtascii-server)"

echo "==> Creating service user"
id -u "$USER_NAME" >/dev/null 2>&1 || useradd --system --create-home --home-dir /var/lib/gtascii --shell /usr/sbin/nologin "$USER_NAME"

echo "==> Opening port $PORT (TCP + UDP)"
if command -v iptables >/dev/null 2>&1; then
    for proto in tcp udp; do
        iptables -C INPUT -p "$proto" --dport "$PORT" -j ACCEPT 2>/dev/null || iptables -I INPUT 1 -p "$proto" --dport "$PORT" -j ACCEPT
    done
    if command -v netfilter-persistent >/dev/null 2>&1; then
        netfilter-persistent save || true
    fi
fi
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q active; then
    ufw allow "$PORT"/tcp
    ufw allow "$PORT"/udp
fi

echo "==> Installing systemd service"
cat > /etc/systemd/system/gtascii-server.service <<EOF
[Unit]
Description=Grand Theft ASCII room server
After=network-online.target
Wants=network-online.target

[Service]
User=$USER_NAME
Environment=GTASCII_HOME=/var/lib/gtascii
ExecStart=$BIN --rooms --port $PORT
Restart=always
RestartSec=3
NoNewPrivileges=true
ProtectSystem=full
ProtectHome=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now gtascii-server

IP="$(curl -fsS https://api.ipify.org 2>/dev/null || hostname -I | awk '{print $1}')"
echo
echo "Room server running on port $PORT."
echo "In the game: Multiplayer > Room server > $IP"
echo "Remember to allow TCP and UDP $PORT in your cloud provider's firewall too."
