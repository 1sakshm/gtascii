#!/usr/bin/env bash
# End-to-end check of online rooms through a relay: one game creates a room,
# a second one finds it in the room list and joins it.
#
#   scripts/relay-smoke.sh path/to/gtascii [relay-url]
set -euo pipefail

bin="$1"
relay="${2:-wss://gtascii-relay.sakshamsai26.workers.dev}"
work="$(mktemp -d)"
export GTASCII_HOME="$work/home"
mkdir -p "$GTASCII_HOME"
host="ci$RANDOM$RANDOM"

"$bin" --relay "$relay" --list-rooms

"$bin" --relay "$relay" --create --name "$host" --frames 1800 --no-audio --no-mouse --samples 1 < /dev/null > "$work/host.txt" 2>&1 &
host_pid=$!

code=""
for _ in $(seq 1 30); do
  sleep 1
  code="$("$bin" --relay "$relay" --list-rooms | awk -v n="$host's" '$2 == n { print $1; exit }')"
  [ -n "$code" ] && break
done
if [ -z "$code" ]; then
  echo "room of $host never appeared"; tail -c 2000 "$work/host.txt"; kill "$host_pid" || true; exit 1
fi
echo "room $code created by $host"

"$bin" --relay "$relay" --join "$code" --name "${host}g" --frames 300 --no-audio --no-mouse --samples 1 < /dev/null > "$work/guest.txt" 2>&1
summary="$(grep -ao "Grand Theft ASCII session:.*" "$work/guest.txt" | tail -1 || true)"
echo "guest: $summary"
if grep -aq "Disconnected" "$work/guest.txt" || ! grep -aq "2 players" "$work/guest.txt"; then
  echo "guest did not see the host"; tail -c 2000 "$work/guest.txt"; kill "$host_pid" || true; exit 1
fi
kill "$host_pid" 2>/dev/null || true
wait "$host_pid" 2>/dev/null || true
echo "online rooms OK"
