"""Drives the bots menu in headless Chromium against a scripted server.

client/dist is served on the first free port from 8093 to 8099. page.route answers /api/* and
page.route_web_socket plays the bots server through every phase; nothing else leaves the
machine. The test checks the bytes the client sends and screenshots each screen into
client/test/out/.

    bots/.venv/bin/python client/test/bots_menu.py
"""

import functools
import http.server
import io
import json
import socket
import sys
import threading
from pathlib import Path

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

TEST = Path(__file__).resolve().parent
DIST = TEST.parent / "dist"
OUT = TEST / "out"
TANK_DEFS = TEST.parents[1] / "server" / "data" / "TankDefinitions.json"

W, H = 1920, 1080
# At 1920x1080 a ui unit is a pixel; resting screens sit at int(H / 2) - 1.
CX, CY = W // 2, H // 2 - 1

GOLDEN = bytes.fromhex("20 00 01 01 00 18 01 34 6d 65 00 00")
# Every player tank's usual build in these lists, in server order: glass.
USUAL = [7, 7, 7, 7, 5, 0, 0, 0]
FIGHTER, FACTORY, SNIPER = 24, 52, 6
CODE = bytes.fromhex("c0ffee42")
SERVER_ID = "aaaa"

failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


# ---- coder ----

def vu(v):
    out = bytearray()
    while True:
        part = v & 0x7F
        v >>= 7
        out.append(part | (0x80 if v else 0))
        if not v:
            return bytes(out)


def vi(v):
    return vu(((v << 1) ^ (-1 if v < 0 else 0)) & 0xFFFFFFFF)


def nt(s):
    return s.encode() + b"\0"


def party_hex(data):
    digits = "0123456789ABCDEF"
    return "".join(digits[b & 15] + digits[b >> 4] for b in data)


def lists(online, bots, player_tanks):
    out = bytes([0x20, 1 if online else 0]) + vu(len(bots))
    for name, tanks in bots:
        out += nt(name) + vu(len(tanks)) + b"".join(vu(t) for t in tanks)
    return out + vu(len(player_tanks)) + b"".join(vu(t) + bytes(USUAL) for t in player_tanks)


FILLED, READY, ALIVE, REMATCH = 1, 2, 4, 8


def room(phase, n0, n1, you, seats, ticks=0, deterministic=False):
    out = bytes([0x21, phase, 1 if deterministic else 0, n0, n1, you])
    for who, tank, state, name in seats:
        out += vu(who) + vu(tank) + bytes([state]) + nt(name)
    return out + vu(ticks)


def result(winner, reason, ticks, stats):
    out = bytes([0x22, winner, reason]) + vu(ticks)
    for kills, dealt in stats:
        out += vu(kills) + vu(dealt)
    return out


def build(b):
    return bytes([1, *b]) if b else b"\0"


def create(deterministic, n0, n1, seats, name, b=None):
    out = bytes([0x20, 1 if deterministic else 0, n0, n1])
    for who, tank in seats:
        out += vu(who) + vu(tank)
    return out + nt(name) + build(b)


def ready(name, b=None):
    return b"\x21" + nt(name) + build(b)


# ---- the scripted server ----

class FakeServer:
    """One socket's worth of server. Accepts Init and records everything but pings."""

    def __init__(self, bot_lists):
        self.ws = None
        self.sent = []
        self.init = None
        self.bot_lists = bot_lists

    def attach(self, ws):
        self.ws = ws
        ws.on_message(self.on_message)

    def on_message(self, message):
        data = message if isinstance(message, bytes) else message.encode()
        if not data or data[0] in (0x01, 0x05):
            return
        if data[0] == 0x00:
            self.init = data
            self.accept(b"")
            self.send(self.bot_lists)
            return
        self.sent.append(data)

    def send(self, data):
        self.ws.send(bytes(data))

    def accept(self, party):
        """What the server sends when a client joins a game (moveTo does the same)."""
        self.send(b"\x04" + nt("bots") + nt("localhost"))
        self.send(b"\x0a" + vu(1))
        self.send(b"\x07" + vi(0))
        self.send(b"\x06" + party)

    def take(self):
        out, self.sent = self.sent, []
        return out


def init_party(init):
    """The party field of an Init packet: build, password, party hex."""
    fields = init[1:].split(b"\0")
    return fields[2].decode()


# ---- page plumbing ----

def free_port():
    for port in range(8093, 8100):
        with socket.socket() as s:
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    raise SystemExit("no free port in 8093-8099")


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(port):
    handler = functools.partial(QuietHandler, directory=str(DIST))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def wire_page(page, port, server):
    """server["fake"] answers each socket the page opens; swap it to script a reload."""
    tanks = TANK_DEFS.read_text()
    api = {
        "servers": json.dumps([{"gamemode": "bots", "name": "Bots", "id": SERVER_ID}]),
        "tanks": tanks,
        "commands": "[]",
        "colors": "{}",
    }

    def route(r):
        url = r.request.url
        if url.startswith(f"http://127.0.0.1:{port}/api/"):
            name = url.rsplit("/", 1)[1]
            return r.fulfill(status=200, content_type="application/json", body=api.get(name, "{}"))
        if url.startswith(f"http://127.0.0.1:{port}/"):
            return r.continue_()
        if "fonts.googleapis.com" in url:
            return r.fulfill(status=200, content_type="text/css", body="")
        return r.abort()

    page.route("**/*", route)
    page.route_web_socket(lambda url: url.endswith("/bots"), lambda ws: server["fake"].attach(ws))


def wait(page, ms):
    page.wait_for_timeout(ms)


def cursor_at(page, x, y):
    page.mouse.move(x, y)
    wait(page, 80)
    return page.evaluate("document.getElementById('canvas').style.cursor")


def click(page, x, y, what):
    check(cursor_at(page, x, y) == "pointer", f"{what} is a button at ({x:.0f}, {y:.0f})")
    page.mouse.click(x, y)
    wait(page, 80)


def shot(page, name):
    page.mouse.move(5, H - 5)
    wait(page, 60)
    page.screenshot(path=str(OUT / f"{name}.png"))


def region(page):
    png = page.screenshot(clip={"x": 520, "y": 230, "width": 880, "height": 640})
    return np.asarray(Image.open(io.BytesIO(png)).convert("RGB"), dtype=np.int16)


# ---- layout, mirroring BotsUI.cpp ----

def card(side, row):
    left = CX + (205 if side else -205) - 181
    top = CY - 80 + row * 120
    return left, top


def tile(side, row):
    left, top = card(side, row)
    return left + 50, top + 50


def build_button(side, row):
    """The Build button along the bottom of our own seat's tile."""
    left, top = card(side, row)
    return left + 50, top + 78


def build_rows(stats):
    """Each stat's row in the build panel by its server index, regen on top: (y, bar left)."""
    shown = [i for i in range(7, -1, -1) if i in stats]
    top = H / 2 - len(shown) * 46 / 2 + 40
    return {i: top + (k + 0.5) * 46 for k, i in enumerate(shown)}, W / 2 - 7 * 34 / 2, top + len(shown) * 46 + 30


def build_segment(stats, i, seg):
    rows, bar, _ = build_rows(stats)
    return bar + seg * 34 + 17, rows[i]


def build_less(stats, i):
    rows, bar, _ = build_rows(stats)
    return bar - 30, rows[i]


def build_more(stats, i):
    rows, bar, _ = build_rows(stats)
    return bar + 7 * 34 + 30, rows[i]


def build_buttons(stats):
    """Reset to default and Done."""
    _, _, y = build_rows(stats)
    return (W / 2 - 75, y + 18), (W / 2 + 105, y + 18)


def picker_arrow(side, row, line, forward):
    left, top = card(side, row)
    rl = left + 112
    y = top + (58 if line else 8) + 17
    return (rl + 233 if forward else rl + 17), y


def format_button(i):
    return CX + (i - 1.5) * 100, CY - 160


def picker_cell(n, index):
    step, box = 104, 90
    max_cols = max(1, int((W - 120) / step))
    cols = max(1, min(n, max_cols, int(np.ceil(np.sqrt(n * 2.0)))))
    rows = (n + cols - 1) // cols
    grid_h = rows * step - (step - box)
    top = H / 2 - min(grid_h, H - 300) / 2
    left = W / 2 - (cols * step - (step - box)) / 2
    col, row = index % cols, index // cols
    return left + col * step + box / 2, top + row * step + box / 2


def buttons_row(widths, y, gap=20):
    total = sum(widths) + gap * (len(widths) - 1)
    x = CX - total / 2
    centers = []
    for w in widths:
        centers.append((x + w / 2, y))
        x += w + gap
    return centers


def cards_bottom(rows):
    return CY - 80 + (rows - 1) * 120 + 100


def start(rows):
    return CX, cards_bottom(rows) + 110


def best(rows):
    return CX, cards_bottom(rows) + 45


def room_buttons(rows, naming=False):
    return cards_bottom(rows) + (140 if naming else 75)


def result_buttons(n0, n1):
    """Rematch and Change setup under the stats rows (46 apart, 14 more between the teams)."""
    y = CY - 130 + 44 + (n0 + n1) * 46 + 14 + 40
    return (CX - 110, y), (CX + 110, y)


def main():
    OUT.mkdir(exist_ok=True)
    if not (DIST / "client.wasm").exists():
        raise SystemExit("build the client first (client/build.sh)")
    defs = json.loads(TANK_DEFS.read_text())
    player_tanks = [t["id"] for t in defs if t and t["name"] and t["name"] != "Dominator"]
    bots = [("Team", [FACTORY, FIGHTER, SNIPER]), ("FFA", [7, 1])]
    online = lists(True, bots, player_tanks)
    port = free_port()
    httpd = serve(port)
    base = f"http://127.0.0.1:{port}/"

    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        context = browser.new_context(viewport={"width": W, "height": H}, device_scale_factor=1)
        context.grant_permissions(["clipboard-read", "clipboard-write"], origin=base.rstrip("/"))
        context.add_init_script("""
            if (!sessionStorage.getItem("seeded")) {
                sessionStorage.setItem("seeded", "1");
                localStorage.clear();
                localStorage.setItem("gamemode", "bots");
                localStorage.setItem("name", "me");
            }
        """)

        # ---- creator ----
        page = context.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        fake = FakeServer(lists(False, [], []))
        server = {"fake": fake}
        wire_page(page, port, server)
        page.goto(base)
        page.wait_for_function("() => !document.getElementById('loading')")
        wait(page, 1500)
        check(fake.init is not None and fake.init[0] == 0, "Init sent")
        check(fake.init is not None and init_party(fake.init) == "", "creator's Init has no party")
        shot(page, "01-offline")
        page.mouse.click(*start(1))
        wait(page, 200)
        check(cursor_at(page, *start(1)) != "pointer", "Start is off while the bots are offline")
        check(fake.take() == [], "nothing sent while offline")

        fake.send(online)
        wait(page, 300)
        shot(page, "02-setup")

        # A 2v2 with two players: blue's second seat goes from bot to Player.
        click(page, *format_button(3), "2v2")
        click(page, *picker_arrow(0, 1, 0, False), "blue 2 who <")
        click(page, *best(2), "best move toggle")
        page.fill("#ui-textfield", "me")
        wait(page, 100)
        shot(page, "03-setup-2v2")
        click(page, *start(2), "Start")
        # The seat keeps its tank: a player can take Factory too.
        expected = create(True, 2, 2, [(0, player_tanks[0]), (0, FACTORY), (1, FACTORY), (1, FACTORY)], "me")
        check(fake.take() == [expected], "2v2 BotsCreate bytes")

        fake.send(b"\x06" + CODE)
        seats = [(0, player_tanks[0], FILLED | READY, "me"), (0, player_tanks[0], 0, ""),
                 (1, FACTORY, FILLED | READY, ""), (1, FACTORY, FILLED | READY, "")]
        fake.send(room(1, 2, 2, 0, seats, deterministic=True))
        wait(page, 1500)
        shot(page, "04-waiting")

        link, cancel = buttons_row([180, 140], room_buttons(2))
        click(page, *link, "Copy party link")
        shot(page, "05-link-copied")
        copied = page.evaluate("navigator.clipboard.readText()")
        want = f"127.0.0.1:{port}/#" + party_hex(SERVER_ID.encode() + b"\0" + CODE)
        check(copied == want, f"party link copied ({copied})")
        wait(page, 1600)

        # A match start or end sends Accept, which resets the simulation; the menu stays put.
        page.mouse.move(5, H - 5)
        wait(page, 100)
        before = region(page)
        fake.accept(CODE)
        diffs = []
        for _ in range(4):
            wait(page, 40)
            diffs.append(int(np.abs(region(page) - before).max()))
        check(max(diffs) <= 8, f"menu unchanged across Accept (max pixel diff {diffs})")

        click(page, *cancel, "Cancel")
        check(fake.take() == [b"\x23"], "Cancel sends BotsLeave")
        fake.send(room(0, 0, 0, 0xFF, []))
        wait(page, 1500)

        # The golden 1v1: Player on Fighter (from the picker) against Team on Factory.
        click(page, *format_button(0), "1v1")
        click(page, *best(1), "best move toggle")
        click(page, *tile(0, 0), "blue tank tile")
        wait(page, 600)
        shot(page, "06-picker")
        click(page, *picker_cell(len(player_tanks), player_tanks.index(FIGHTER)), "Fighter in the picker")
        wait(page, 400)
        click(page, *start(1), "Start")
        check(fake.take() == [GOLDEN], "1v1 BotsCreate is the golden bytes")

        one = [(0, FIGHTER, FILLED | READY, "me"), (1, FACTORY, FILLED | READY, "")]
        fake.send(b"\x06" + CODE)
        fake.send(room(1, 1, 1, 0, one))
        wait(page, 300)
        fake.send(room(2, 1, 1, 0, one, ticks=75))
        wait(page, 1300)
        shot(page, "07-countdown")

        # The match starts: the client moves games (Accept) and the countdown fades out as it was.
        wait(page, 1800)
        fake.accept(b"")
        alive = [(w, t, s | ALIVE, n) for w, t, s, n in one]
        fake.send(room(3, 1, 1, 0, alive, ticks=0))
        wait(page, 60)
        shot(page, "08-match-start")
        wait(page, 1500)
        shot(page, "09-playing")

        dead_me = [(0, FIGHTER, FILLED | READY, "me"), (1, FACTORY, FILLED | READY | ALIVE, "")]
        fake.send(room(3, 1, 1, 0, dead_me, ticks=250))
        wait(page, 1200)
        shot(page, "10-spectating")

        fake.send(result(1, 0, 1075, [(0, 81234), (1, 15000)]))
        fake.send(room(4, 1, 1, 0, dead_me, ticks=1075))
        wait(page, 1500)
        shot(page, "11-result")
        rematch, change = result_buttons(1, 1)
        click(page, *rematch, "Rematch")
        check(fake.take() == [b"\x22"], "Rematch sends BotsRematch")
        fake.send(room(4, 1, 1, 0, [(0, FIGHTER, FILLED | READY | REMATCH, "me"), dead_me[1]], ticks=1075))
        wait(page, 300)
        check(cursor_at(page, *rematch) != "pointer", "Rematch is off once voted")

        # The rematch, then the bots go offline mid-match.
        fake.send(room(2, 1, 1, 0, one, ticks=75))
        wait(page, 400)
        fake.send(room(3, 1, 1, 0, alive, ticks=0))
        wait(page, 300)
        fake.send(result(0xFF, 3, 400, [(0, 120), (0, 90)]))
        fake.send(room(4, 1, 1, 0, alive, ticks=400))
        wait(page, 1500)
        shot(page, "12-result-offline")
        click(page, *change, "Change setup")
        check(fake.take() == [b"\x23"], "Change setup sends BotsLeave")
        fake.send(room(0, 0, 0, 0xFF, []))
        wait(page, 1500)
        shot(page, "13-back-to-setup")

        # The setup survives a reload.
        saved = page.evaluate("localStorage.getItem('bots_setup')")
        check(saved is not None and saved.startswith("0 0\n0 24 \n"), f"setup saved ({saved!r})")
        fake = server["fake"] = FakeServer(online)
        page.reload()
        page.wait_for_function("() => !document.getElementById('loading')")
        wait(page, 1500)
        shot(page, "14-restored")
        page.keyboard.press("Enter")
        wait(page, 200)
        check(fake.take() == [GOLDEN], "restored setup starts the same 1v1 (Enter)")

        # Our own Fighter's build: two off reload, then as much max health as the points left allow.
        stats = range(8)
        click(page, *build_button(0, 0), "Build on our own tile")
        wait(page, 500)
        shot(page, "15-build")
        less, more = build_less(stats, 1), build_more(stats, 6)
        check(cursor_at(page, *more) != "pointer", "+ is off with every point spent")
        click(page, *less, "reload -")
        click(page, *less, "reload - again")
        click(page, *build_segment(stats, 6, 2), "max health's third segment")
        page.mouse.move(*build_segment(stats, 3, 6))
        wait(page, 100)
        page.screenshot(path=str(OUT / "16-build-edited.png"))
        mine = [7, 5, 7, 7, 5, 0, 2, 0]
        saved = page.evaluate("localStorage.getItem('bots_builds')")
        check(saved == "24 7 5 7 7 5 0 2 0\n", f"build saved ({saved!r})")
        reset, done = build_buttons(stats)
        click(page, *done, "Done")
        wait(page, 3000)
        page.keyboard.press("Enter")
        wait(page, 200)
        check(fake.take() == [create(False, 1, 1, [(0, FIGHTER), (1, FACTORY)], "me", mine)],
              "Start sends our own build")
        click(page, *build_button(0, 0), "Build again")
        wait(page, 500)
        click(page, *reset, "Reset to default")
        check(page.evaluate("localStorage.getItem('bots_builds')") == "", "reset forgets the build")
        page.keyboard.press("Escape")
        wait(page, 500)
        check(cursor_at(page, *tile(0, 0)) == "pointer", "Escape closes the build")

        # ---- guest, by party link ----
        guest = context.new_page()
        guest.on("pageerror", lambda e: errors.append(str(e)))
        host = FakeServer(online)
        wire_page(guest, port, {"fake": host})
        guest.goto(base + "#" + party_hex(SERVER_ID.encode() + b"\0" + CODE))
        guest.wait_for_function("() => !document.getElementById('loading')")
        wait(guest, 1000)
        check(host.init is not None and init_party(host.init) == party_hex(CODE), "guest's Init carries the party code")
        two = [(0, 0, FILLED | READY, "me"), (0, SNIPER, FILLED, ""), (1, FACTORY, FILLED | READY, "")]
        host.send(b"\x06" + CODE)
        host.send(room(1, 2, 1, 1, two))
        wait(guest, 1500)
        shot(guest, "17-guest-waiting")

        # The guest takes Sniper with one point less movement speed.
        click(guest, *build_button(0, 1), "guest's Build")
        wait(guest, 500)
        click(guest, *build_less(range(8), 0), "movement speed -")
        theirs = [6, 7, 7, 7, 5, 0, 0, 0]
        click(guest, *build_buttons(range(8))[1], "guest's Done")
        check(host.take() == [], "closing the build before Ready sends nothing")
        wait(guest, 500)
        ready_button = buttons_row([160, 140], room_buttons(2, naming=True))[0]
        check(cursor_at(guest, *ready_button) == "pointer", "guest's Ready is a button")
        guest.fill("#ui-textfield", "guest")
        wait(guest, 100)
        guest.keyboard.press("Enter")
        wait(guest, 200)
        check(host.take() == [ready("guest", theirs)], "guest's Enter sends BotsReady with the name and build")
        two[1] = (0, SNIPER, FILLED | READY, "guest")
        host.send(room(1, 2, 1, 1, two))
        wait(guest, 1500)
        shot(guest, "18-guest-ready")

        # Once ready, a changed build goes out again as the guest closes it.
        click(guest, *build_button(0, 1), "guest's Build once ready")
        wait(guest, 500)
        click(guest, *build_more(range(8), 0), "movement speed +")
        guest.keyboard.press("Escape")
        wait(guest, 200)
        check(host.take() == [ready("guest")], "a ready guest's closed build is sent again")

        # A 2v1 played through: the guest won, and the other player wants a rematch.
        host.send(room(2, 2, 1, 1, two, ticks=75))
        wait(guest, 300)
        host.accept(b"")
        host.send(room(3, 2, 1, 1, [(w, t, s | ALIVE, n) for w, t, s, n in two], ticks=0))
        wait(guest, 1200)
        shot(guest, "19-guest-playing")
        over = [(0, 0, FILLED | READY | REMATCH, "me"), (0, SNIPER, FILLED | READY | ALIVE, "guest"),
                (1, FACTORY, FILLED | READY, "")]
        host.send(result(0, 0, 3020, [(0, 41000), (1, 99950), (1, 120000)]))
        host.send(room(4, 2, 1, 1, over, ticks=3020))
        wait(guest, 1500)
        shot(guest, "20-guest-result")
        click(guest, *result_buttons(2, 1)[1], "guest's Change setup")
        check(host.take() == [b"\x23"], "guest's Change setup sends BotsLeave")
        check(guest.evaluate("location.hash") == "", "leaving wipes the party hash")

        check(errors == [], f"no page errors {errors}")
        browser.close()
    httpd.shutdown()
    print(f"screenshots in {OUT}")
    if failures:
        print(f"{len(failures)} failure(s)")
        sys.exit(1)
    print("bots menu OK")


if __name__ == "__main__":
    main()
