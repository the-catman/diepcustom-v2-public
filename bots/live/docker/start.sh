#!/bin/bash
# The server, its brain and a quick tunnel to them; prints the tunnel's address once it's up.
cd /app/server || exit 1
export PORT=8091 BOTS_SOCKET=/tmp/live.sock TRUST_PROXY=1
/app/bots/live/build/bots-server &
/venv/bin/python /app/bots/live/brain.py --socket "$BOTS_SOCKET" &
cloudflared tunnel --no-autoupdate --url "http://localhost:$PORT" 2>&1 | while IFS= read -r line; do
    if [[ $line =~ (https://[a-z0-9-]+\.trycloudflare\.com) ]]; then
        printf '\n  Your page: %s\n  Send it to anyone who wants to play the bots.\n\n' "${BASH_REMATCH[1]}"
    elif [[ $line == *ERR* ]]; then
        echo "tunnel: $line"
    fi
done &
wait -n
