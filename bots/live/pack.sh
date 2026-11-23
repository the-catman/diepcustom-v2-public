#!/bin/bash
# Packs the bots server to run on another machine under Docker (docker/), reachable through a Cloudflare quick tunnel:
# the source it builds there, the built client, and only the checkpoints bots.json plays.
#   bots/live/pack.sh      writes bots/live/pack/bots-server.zip; build the client (client/build.sh) first
set -e
LIVE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$LIVE/../.." && pwd)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PACK=$STAGE/bots-server
APP=$PACK/app
mkdir -p "$APP/server" "$APP/client" "$APP/bots/env" "$APP/bots/live/tests" "$APP/bots/train" "$APP/bots/weights"
NOGIT=(--exclude=.git --exclude=.gitmodules --exclude=.gitattributes --exclude=.github)
rsync -a "${NOGIT[@]}" "$ROOT/server/CMakeLists.txt" "$ROOT/server/src" "$ROOT/server/host" "$ROOT/server/third_party" \
    "$ROOT/server/data" "$APP/server/"
rsync -a "$ROOT/client/dist" "$APP/client/"
rsync -a "$ROOT/bots/env/CMakeLists.txt" "$ROOT/bots/env/src" "$APP/bots/env/"
rsync -a "$LIVE/CMakeLists.txt" "$LIVE/src" "$LIVE/brain.py" "$APP/bots/live/"
cp "$LIVE/tests/exam.cpp" "$LIVE/tests/rooms_test.cpp" "$APP/bots/live/tests/"
cp "$ROOT"/bots/train/{common,model,evaluate,watch,replay}.py "$APP/bots/train/"
# Each checkpoint a bot plays, its specialists' too, as brain.py picks it, in its own folder under weights/ with
# bots.json pointing there, and of its run's args.json only what brain.py reads: the classes it dealt and whether it
# trained turned.
"$ROOT/bots/.venv/bin/python" - "$LIVE/bots.json" "$APP/bots" <<'EOF'
import glob, json, os, re, shutil, sys
src, dst = sys.argv[1], sys.argv[2]
bots = json.load(open(src))
def copy(checkpoint, name):
    path = os.path.normpath(os.path.join(os.path.dirname(src), checkpoint))
    if os.path.isdir(path):
        path = sorted(glob.glob(os.path.join(path, "it[0-9][0-9][0-9][0-9][0-9].pt")))[-1]
    name = re.sub(r"[^a-z0-9]+", "-", name.lower())
    folder = os.path.join(dst, "weights", name)
    os.makedirs(folder)
    shutil.copy(path, os.path.join(folder, name + ".pt"))
    settings = os.path.join(os.path.dirname(path), "args.json")
    if os.path.exists(settings):
        kept = {k: v for k, v in json.load(open(settings)).items() if k in ("classes", "symmetry")}
        with open(os.path.join(folder, "args.json"), "w") as f:
            json.dump(kept, f)
    return f"../weights/{name}/{name}.pt"
for bot in bots:
    bot["checkpoint"] = copy(bot["checkpoint"], bot["name"])
    for cls, checkpoint in (bot.get("specialists") or {}).items():
        bot["specialists"][cls] = copy(checkpoint, bot["name"] + " " + cls)
with open(os.path.join(dst, "live", "bots.json"), "w") as f:
    f.write("[\n" + ",\n".join("  " + json.dumps(b) for b in bots) + "\n]\n")
EOF
cp "$LIVE"/docker/{Dockerfile,compose.yaml,start.sh,start.bat,README.txt} "$PACK/"
mkdir -p "$LIVE/pack"
rm -f "$LIVE/pack/bots-server.zip"
(cd "$STAGE" && zip -qr "$LIVE/pack/bots-server.zip" bots-server)
echo "$LIVE/pack/bots-server.zip: $(du -h "$LIVE/pack/bots-server.zip" | cut -f1)"
