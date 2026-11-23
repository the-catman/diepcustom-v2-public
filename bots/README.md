# Bots

Bots that play the game, and a server mode to play against them. One policy plays every class. It's trained with
PPO by self-play, in a simulator that runs the server's own game code many games at a time.

- `env/`: the simulator, a Python module (`diepenv`) built from `server/src`.
- `train/`: training (`ppo.py`), exams (`evaluate.py`, `league.py`) and replays (`watch.py`).
- `live/`: the bots server, which hosts the Bots mode, and the brain (`brain.py`) that plays its bots.
- `weights/`: the bots the live server plays, listed in `live/bots.json`.
- `tests/`: tests for the simulator and training.

Training writes to `runs/`, which isn't tracked.

## Setup

Needs Python 3, a C++17 compiler, CMake, Ninja and OpenMP. From `bots/`:
```
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cmake -S env -B env/build -G Ninja -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
    -Dpybind11_DIR="$(.venv/bin/python -m pybind11 --cmakedir)"
cmake --build env/build
```

## Playing against the bots

Build the client (`client/build.sh`) and the bots server, then start it with its brain:
```
cmake -S bots/live -B bots/live/build -G Ninja
cmake --build bots/live/build
bots/live/run.sh
```
Open http://localhost:8091 and pick Bots. Choose 1v1, 2v1, 1v2 or 2v2, a tank for each seat, and who plays it: you, a
friend by party link, or a bot. Your own seat's Build button sets your stat build.

- `bots/live/tunnel.sh` does the same behind a Cloudflare quick tunnel (needs `cloudflared`) and prints a public
  address anyone can play on.
- `bots/live/pack.sh` packs everything into a zip that runs under Docker on another machine, tunnel included. See
  `live/docker/README.txt`.

The brain runs on the CPU unless started with `--device cuda`.

## Training

From `bots/train`:
```
../.venv/bin/python ppo.py --out ../runs/mine --classes fighter,overlord --symmetry --remember --iters 1000
```
This trains one policy for Fighter and Overlord against a scripted fighter. `--mix` sets the opponents (scripted
ones, the policy's own past versions with `self`), `--frozen` adds saved bots, and `--ffa` and `--team` add
free-for-all and team games. `ppo.py --help` and the top of `ppo.py` explain the rest.

Ctrl+C finishes the iteration and saves; `--resume` carries on. A numbered checkpoint is saved every
`--eval-every` iterations.

To watch one:
```
../.venv/bin/python watch.py ../runs/mine/it01000.pt --tank overlord --opponent fighter --out replay.html
```

The live server plays checkpoints trained with `--classes` and `--symmetry`. To play yours, point
`live/bots.json` at its run folder (the newest numbered checkpoint is used) or at a checkpoint.

Nothing in `runs/` gets committed. To add a bot to the repo, copy its checkpoint into a folder under `weights/` with
its run's `args.json` (the brain only reads `classes` and `symmetry` from it), and point `live/bots.json` there.

## Tests

```
bots/.venv/bin/python -m pytest bots/tests
bots/live/build/rooms-test
client/test/run.sh
```
`bots/live/tests/play.py` plays a running bots server in a headless browser.
