#!/bin/bash
# The bots server and its brain (run.sh) behind a Cloudflare quick tunnel: prints a public trycloudflare.com address
# anyone can play on, with no account or domain. The address is new every start.
#   bots/live/tunnel.sh                                    cloudflared on PATH; Ctrl-C stops everything
#   CLOUDFLARED=path/to/cloudflared bots/live/tunnel.sh    or wherever it is
# Arguments go to brain.py, as with run.sh.
LIVE=$(cd "$(dirname "$0")" && pwd)
CLOUDFLARED=${CLOUDFLARED:-cloudflared}
if ! command -v "$CLOUDFLARED" > /dev/null; then
    echo "cloudflared not found: put it on PATH or set CLOUDFLARED to it" >&2
    exit 1
fi
export PORT=${PORT:-8091} TRUST_PROXY=1
PIDS=()
"$LIVE/run.sh" "$@" &
PIDS+=($!)
"$CLOUDFLARED" tunnel --no-autoupdate --grace-period 1s --url "http://localhost:$PORT" 2> >(
    while IFS= read -r line; do
        if [[ $line =~ (https://[a-z0-9-]+\.trycloudflare\.com) ]]; then
            printf '\n  Your page: %s\n  It can take a minute to answer.\n\n' "${BASH_REMATCH[1]}"
        elif [[ $line == *ERR* ]]; then
            echo "tunnel: $line"
        fi
    done
) &
PIDS+=($!)
trap 'kill "${PIDS[@]}" 2>/dev/null' INT TERM EXIT
wait -n "${PIDS[@]}"
