"""Replays that play through the game's own client: an HTML page, or a video of the whole arena.

The duel records what server clients watching it would be sent, and the page feeds that to the
client in place of a server.
"""
import base64
import gzip
import json
import os
import struct
import subprocess

from common import ROOT, diepenv

TEMPLATE = os.path.join(os.path.dirname(__file__), "replay.html")
CLIENT = os.path.join(ROOT, "client", "dist")
TANK_DEFS = os.path.join(ROOT, "server", "data", "TankDefinitions.json")
VIEWS = ["Arena", "Blue's screen"]
UPDATE = 0


def views(arena_size):
    """(follow, fov) per view: the whole arena with a little room around it, then blue's own screen.

    The client shows 1080 / fov of the world top to bottom in any window from square to 16:9.
    """
    return [(-1, 1080 / (arena_size * 1.16)), (0, 0.0)]


class Recording:
    def __init__(self):
        # games[g][v]: (tick, message) the v-th view was sent in the g-th game.
        self.games = []

    def add(self, packets):
        for g, v, tick, data in packets:
            while len(self.games) <= g:
                self.games.append([[] for _ in VIEWS])
            self.games[g][v].append((tick, data))

    def pack(self, count):
        """The first count games, gzipped, with time counted in ticks from each game's first update."""
        out = [struct.pack("<I", count)]
        for game in self.games[:count]:
            first = min((t for t, d in game[0] if d[0] == UPDATE), default=0)
            out.append(struct.pack("<I", len(game)))
            for msgs in game:
                out.append(struct.pack("<I", len(msgs)))
                for tick, data in msgs:
                    out.append(struct.pack("<II", max(0, tick - first), len(data)))
                    out.append(data)
        return gzip.compress(b"".join(out), 9)


def client_files():
    paths = [os.path.join(CLIENT, f) for f in ("client.js", "client.wasm")]
    if not all(os.path.exists(p) for p in paths):
        raise SystemExit("the replay plays through the client: build it first with cd client && ./build.sh")
    return [open(p, "rb").read() for p in paths]


def html(recording, title, results, arena_size):
    js, wasm = client_files()
    with open(TANK_DEFS) as f:
        tank_defs = f.read()
    meta = {"title": title, "results": results, "views": VIEWS, "arena": arena_size}
    b64 = lambda b: base64.b64encode(b).decode()
    with open(TEMPLATE) as f:
        page = f.read()
    for key, value in (("/*META*/null", json.dumps(meta).replace("</", "<\\/")),
                       ("/*TANKS*/null", tank_defs.replace("</", "<\\/")),
                       ("/*COLORS*/null", json.dumps(diepenv.colors())),
                       ("/*DATA*/", b64(recording.pack(len(results)))),
                       ("/*WASM*/", b64(wasm)),
                       ("/*CLIENT*/", b64(js))):
        page = page.replace(key, value, 1)
    return page


# Encoder sessions a GeForce card runs at once, across every process.
NVENC_SESSIONS = 8
# How many replay.frame() calls the page's game has left, the last included, counted the way frame() counts.
FRAMES_LEFT = """(() => {
    let p = pos, n = 0;
    do {
        p += FRAME_MS;
        n += 1;
    } while (p <= length(game) + HOLD_MS);
    return n;
})()"""


def _encoder_slot():
    """One of the card's encoder sessions, waited for and held until the returned file is closed."""
    import fcntl
    import tempfile
    import time

    while True:
        for i in range(NVENC_SESSIONS):
            f = open(os.path.join(tempfile.gettempdir(), f"replay-nvenc-{i}.lock"), "w")
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return f
            except BlockingIOError:
                f.close()
        time.sleep(0.5)


def _render_part(page_path, g, part, parts, out, size):
    """The part-th of parts equal stretches of one game's arena view into its own mp4, one frame at a time.

    The stretch plays the game from the start without pictures up to where it begins, so its frames are the ones a
    single run through would take. Returns how many it took; none, and no file, when the stretch is empty.
    """
    from playwright.sync_api import sync_playwright

    slot, encoder, frames = _encoder_slot(), None, 0
    with sync_playwright() as p:
        # The GPU where there is one; Chromium falls back to drawing in software.
        browser = p.chromium.launch(args=["--use-angle=gl"])
        page = browser.new_page(viewport={"width": size, "height": size}, device_scale_factor=1)
        page.add_init_script("window.REPLAY_VIDEO = true")
        page.goto("file://" + os.path.abspath(page_path))
        page.wait_for_function("window.replay && window.replay.games() > 0")
        page.evaluate(f"replay.load({g}, 0)")
        page.wait_for_function("replay.connected()")
        page.evaluate("replay.settle(90)")
        total = page.evaluate(FRAMES_LEFT)
        start, end = total * part // parts, total * (part + 1) // parts
        for at in range(0, start, 60):
            page.evaluate(f"for (let i = 0; i < {min(60, start - at)}; ++i) replay.frame()")
        cdp = page.context.new_cdp_session(page)
        for _ in range(start, end):
            page.evaluate("replay.frame()")
            shot = cdp.send("Page.captureScreenshot", {"format": "png", "optimizeForSpeed": True})
            if encoder is None:
                encoder = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "image2pipe", "-framerate",
                                            "60", "-i", "-", "-c:v", "h264_nvenc", "-pix_fmt", "yuv420p", "-preset",
                                            "p5", "-tune", "hq", "-rc", "vbr", "-cq", "19", "-b:v", "0", out],
                                           stdin=subprocess.PIPE)
            encoder.stdin.write(base64.b64decode(shot["data"]))
            frames += 1
        browser.close()
    if encoder is not None:
        encoder.stdin.close()
        if encoder.wait() != 0:
            raise RuntimeError("ffmpeg failed")
    slot.close()
    return frames


def video(page_path, out, games, size=1440, workers=NVENC_SESSIONS):
    """Every game's arena view at 60 frames a second, joined into one mp4.

    A browser takes its pictures no faster than the screen refreshes however quick the drawing, so each game is cut
    into stretches that render side by side.
    """
    import multiprocessing
    import tempfile
    from concurrent.futures import ProcessPoolExecutor

    split = -(-workers // games)
    jobs = [(g, k) for g in range(games) for k in range(split)]
    with tempfile.TemporaryDirectory() as tmp:
        parts = [os.path.join(tmp, f"{g:03d}-{k:03d}.mp4") for g, k in jobs]
        # Spawned rather than forked: the caller has torch's threads running.
        spawn = multiprocessing.get_context("spawn")
        with ProcessPoolExecutor(max(1, min(workers, len(jobs))), mp_context=spawn) as pool:
            frames = sum(pool.map(_render_part, [page_path] * len(jobs), [g for g, _ in jobs],
                                  [k for _, k in jobs], [split] * len(jobs), parts, [size] * len(jobs)))
        listing = os.path.join(tmp, "parts.txt")
        with open(listing, "w") as f:
            f.writelines(f"file '{p}'\n" for p in parts if os.path.exists(p))
        r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", listing,
                            "-c", "copy", out])
        if r.returncode != 0:
            raise SystemExit("ffmpeg failed to join the games")
    return frames
