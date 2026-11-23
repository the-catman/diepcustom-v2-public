import base64
import gzip
import os
import re
import struct
import subprocess
import sys

import numpy as np
import pytest

from common import diepenv, make_config, tank_id
import replay
from test_resume import TRAIN

SERVER_INFO, PLAYER_COUNT, ACCEPT, PARTY_CODE, UPDATE = 4, 10, 7, 6, 0
HAVE_CLIENT = all(os.path.exists(os.path.join(replay.CLIENT, f)) for f in ("client.js", "client.wasm"))


def recorded(games=2, **kw):
    c = make_config("fighter", arena_size=4000, spawn_min=1000, spawn_max=2000, **kw)
    c.tanks = [tank_id("fighter")] * 2
    v = diepenv.Vec(c, 1, 1)
    v.seed(5)
    v.reset_all()
    v.duel_start_replay(0, replay.views(4000), ["blue", "red"])
    packets, done, n = [], 0, v.rows
    while done < games:
        o = v.step(np.zeros(n, np.int32), np.ones(n, bool), np.full(n, -1, np.int32), np.zeros(n, np.float32))
        packets += v.duel_take_packets(0)
        done += int(o["done"][0])
    return packets


def test_each_view_gets_what_a_connecting_client_would():
    packets = recorded(shapes_min=1, shapes_max=1)
    assert {g for g, *_ in packets} == {0, 1, 2}
    for g in (0, 1):
        sizes = []
        for v in (0, 1):
            msgs = [(t, d) for gg, vv, t, d in packets if gg == g and vv == v]
            assert [d[0] for _, d in msgs[:4]] == [SERVER_INFO, PLAYER_COUNT, ACCEPT, PARTY_CODE]
            updates = [t for t, d in msgs[4:] if d[0] == UPDATE]
            assert len(updates) == len(msgs) - 4
            # One a tick, and a second's more after the game ends.
            assert updates == list(range(updates[0], updates[0] + len(updates))) and len(updates) > 25
            sizes.append(len(msgs[4][1]))
        # The arena view starts with every shape in it; blue's screen doesn't reach the far corners.
        assert sizes[0] > sizes[1]
    # The next game has just begun: only its greeting so far.
    assert [d[0] for g, v, t, d in packets if g == 2 and v == 0] == [SERVER_INFO, PLAYER_COUNT, ACCEPT, PARTY_CODE]


def test_a_duel_records_only_once():
    c = make_config("fighter")
    v = diepenv.Vec(c, 1, 1)
    v.reset_all()
    v.duel_start_replay(0, [(-1, 0.3)], ["a", "b"])
    with pytest.raises(RuntimeError):
        v.duel_start_replay(0, [(-1, 0.3)], ["a", "b"])


def unpack(blob):
    raw = gzip.decompress(blob)
    at = 0

    def u32():
        nonlocal at
        at += 4
        return struct.unpack_from("<I", raw, at - 4)[0]

    games = []
    for _ in range(u32()):
        views = []
        for _ in range(u32()):
            msgs = []
            for _ in range(u32()):
                tick, n = u32(), u32()
                msgs.append((tick, raw[at:at + n]))
                at += n
            views.append(msgs)
        games.append(views)
    assert at == len(raw)
    return games


@pytest.mark.skipif(not HAVE_CLIENT, reason="needs the built client")
def test_the_page_carries_the_client_and_each_finished_game():
    packets = recorded()
    rec = replay.Recording()
    rec.add(packets)
    page = replay.html(rec, "a </script> title", ["win", "loss"], 4000)
    assert not re.search(r"/\*(META|TANKS|COLORS|DATA|WASM|CLIENT)\*/", page)
    assert "a </script> title" not in page
    data = re.search(r'<script id="data" type="application/octet-stream">([^<]*)</script>', page).group(1)
    games = unpack(base64.b64decode(data))
    assert len(games) == 2 and all(len(g) == 2 for g in games)
    for g in range(2):
        msgs = [(t, d) for gg, v, t, d in packets if gg == g and v == 0]
        first = min(t for t, d in msgs if d[0] == UPDATE)
        # Time counts from each game's first update.
        assert games[g][0] == [(max(0, t - first), d) for t, d in msgs]
        assert games[g][0][4][0] == 0


@pytest.mark.skipif(not HAVE_CLIENT, reason="needs the built client")
def test_the_client_plays_a_recorded_duel(tmp_path):
    pytest.importorskip("playwright")
    page_path = tmp_path / "replay.html"
    r = subprocess.run([sys.executable, "watch.py", "--scripted", "fighter", "--opponent", "strafe", "--episodes", "1",
                        "--out", str(page_path)], cwd=TRAIN, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    from playwright.sync_api import sync_playwright
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 400, "height": 400})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.add_init_script("window.REPLAY_VIDEO = true")
        page.goto("file://" + str(page_path))
        page.wait_for_function("window.replay && window.replay.games() === 1")
        page.evaluate("replay.load(0, 0)")
        page.wait_for_function("replay.connected()", timeout=10000)
        page.evaluate("replay.settle(10)")
        for _ in range(30):
            page.evaluate("replay.frame()")
        assert page.evaluate("input.get_convar('ren_ui')") == "false"
        assert page.evaluate("getComputedStyle(document.getElementById('world')).display") == "block"
        assert page.evaluate("document.getElementById('caption').textContent").startswith("fighter (Tank, blue)")
        browser.close()
    assert not errors
