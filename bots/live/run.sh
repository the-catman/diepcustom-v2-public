#!/bin/bash
# The bots server and the brain that plays its bots. Open the client on the server's port and pick Bots.
#   bots/live/run.sh                            PORT=8091 by default; Ctrl-C stops everything
#   SITE_ADDRESS=example.com bots/live/run.sh   also serves it over HTTPS through Caddy (Caddyfile here), which
#                                               needs ports 80 and 443 reachable and the name pointing here
# Build the server first: cmake -S bots/live -B bots/live/build -G Ninja && cmake --build bots/live/build
# and the client (client/build.sh), which the server hands out from client/dist.
# bots/live/tests/play.py plays a 1v1 and a 2v2 against it in headless Chromium.
LIVE=$(cd "$(dirname "$0")" && pwd)
export PORT=${PORT:-8091}
export BOTS_SOCKET=${BOTS_SOCKET:-$LIVE/../runs/live.sock}
cd "$LIVE/../../server" || exit 1
PIDS=()
if [ -n "$SITE_ADDRESS" ]; then
    export TRUST_PROXY=1
    caddy run --config "$LIVE/Caddyfile" --adapter caddyfile &
    PIDS+=($!)
fi
"$LIVE/build/bots-server" &
PIDS+=($!)
"$LIVE/../.venv/bin/python" "$LIVE/brain.py" --socket "$BOTS_SOCKET" "$@" &
PIDS+=($!)
trap 'kill "${PIDS[@]}" 2>/dev/null' INT TERM EXIT
echo "bots server on http://localhost:$PORT${SITE_ADDRESS:+ and https://$SITE_ADDRESS}"
wait -n
