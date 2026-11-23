"""Plays a running bots server in headless Chromium: a 1v1 against the bots, then a 2v2 with a second page joining
by the party link. Start bots/live/run.sh first; screenshots go to bots/live/tests/out/.

    bots/.venv/bin/python bots/live/tests/play.py [port or address, e.g. https://example.com]
"""
import math
import random
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "client" / "test"))
import bots_menu as bm  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

TARGET = sys.argv[1] if len(sys.argv) > 1 else "8091"
BASE = TARGET.rstrip("/") + "/" if "://" in TARGET else f"http://127.0.0.1:{TARGET}/"
OUT = Path(__file__).resolve().parent / "out"
OUT.mkdir(parents=True, exist_ok=True)
FIGHTER = 24
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def read_vu(b, i):
    v = s = 0
    while True:
        x = b[i]
        i += 1
        v |= (x & 0x7F) << s
        s += 7
        if not x & 0x80:
            return v, i


def read_nt(b, i):
    j = b.index(0, i)
    return b[i:j].decode(), j + 1


class Wire:
    """Every frame one page's socket carries, decoded where it's a bots packet."""

    def __init__(self, page, name):
        self.name, self.sent, self.phases, self.results, self.lists = name, [], [], [], None
        self.notes = []
        page.on("websocket", self.attach)

    def attach(self, ws):
        ws.on("framesent", lambda p: self.sent.append(bytes(p) if not isinstance(p, str) else p.encode()))
        ws.on("framereceived", self.received)

    def received(self, p):
        b = bytes(p) if not isinstance(p, str) else p.encode()
        if not b:
            return
        if b[0] == 0x21 and len(b) > 1:
            if not self.phases or self.phases[-1] != b[1]:
                self.phases.append(b[1])
        elif b[0] == 0x22:
            self.results.append(b)
        elif b[0] == 0x20 and len(b) > 2:
            self.lists = b
        elif b[0] == 0x03:
            self.notes.append(b)

    def player_tanks(self):
        b = self.lists
        i = 2
        n, i = read_vu(b, i)
        bots = []
        for _ in range(n):
            name, i = read_nt(b, i)
            k, i = read_vu(b, i)
            tanks = []
            for _ in range(k):
                t, i = read_vu(b, i)
                tanks.append(t)
            bots.append((name, tanks))
        k, i = read_vu(b, i)
        tanks = []
        for _ in range(k):
            t, i = read_vu(b, i)
            tanks.append(t)
            i += 8
        return bots, tanks

    def creates(self):
        return [s for s in self.sent if s and s[0] == 0x20]


def until(page, cond, seconds, step=250):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        page.wait_for_timeout(step)
    return cond()


def play(pages, seconds, stop):
    """Wander and fire on every page until stop() or the time is up."""
    keys = "wasd"
    for p in pages:
        p.mouse.move(bm.CX + 200, bm.CY)
        p.mouse.down()
    held = {p: None for p in pages}
    end = time.time() + seconds
    while time.time() < end and not stop():
        for p in pages:
            if held[p]:
                p.keyboard.up(held[p])
            held[p] = random.choice(keys)
            p.keyboard.down(held[p])
            a = random.uniform(0, 6.28)
            p.mouse.move(bm.CX + 300 * math.cos(a), bm.CY + 300 * math.sin(a))
        pages[0].wait_for_timeout(1200)
    for p in pages:
        if held[p]:
            p.keyboard.up(held[p])
        p.mouse.up()


def new_page(context, name, errors):
    page = context.new_page()
    page.on("pageerror", lambda e: errors.append(f"{name}: {e}"))
    return page, Wire(page, name)


def main():
    random.seed(1)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        context = browser.new_context(viewport={"width": bm.W, "height": bm.H}, device_scale_factor=1)
        context.grant_permissions(["clipboard-read", "clipboard-write"], origin=BASE.rstrip("/"))
        context.add_init_script("""
            if (!sessionStorage.getItem("seeded")) {
                sessionStorage.setItem("seeded", "1");
                localStorage.removeItem("bots_setup");
                localStorage.setItem("gamemode", "bots");
                localStorage.setItem("name", "me");
            }
        """)
        errors = []

        # ---- 1v1: me as Fighter against Bot as Fighter ----
        page, wire = new_page(context, "me", errors)
        page.goto(BASE)
        page.wait_for_function("() => !document.getElementById('loading')")
        check(until(page, lambda: wire.lists is not None, 10), "BotsLists arrived")
        bots, player_tanks = wire.player_tanks()
        print("bots:", [b[0] for b in bots], "player tanks:", len(player_tanks))
        check(wire.lists[1] & 1 == 1, "lists say the brain is online")
        page.wait_for_timeout(800)
        page.screenshot(path=str(OUT / "01-setup.png"))
        check(bots[0][1][0] == FIGHTER, f"Bot's seat starts on Fighter ({bots[0][1][:3]})")
        bm.click(page, *bm.tile(0, 0), "blue tank tile")
        page.wait_for_timeout(500)
        bm.click(page, *bm.picker_cell(len(player_tanks), player_tanks.index(FIGHTER)), "Fighter")
        page.wait_for_timeout(300)
        page.screenshot(path=str(OUT / "02-setup-1v1.png"))
        bm.click(page, *bm.start(1), "Start")
        sent = wire.creates()
        check(len(sent) == 1, f"one BotsCreate ({sent})")
        if sent:
            b = sent[-1]
            check(b[2:4] == bytes([1, 1]), "1v1")
            seats, i = [], 4
            for _ in range(2):
                who, i = read_vu(b, i)
                tank, i = read_vu(b, i)
                seats.append((who, tank))
            check(seats == [(0, FIGHTER), (1, FIGHTER)], f"seats {seats}")
        check(until(page, lambda: 3 in wire.phases, 15), f"match started (phases {wire.phases})")
        page.wait_for_timeout(1500)
        page.screenshot(path=str(OUT / "03-1v1-playing.png"))
        t0 = time.time()
        play([page], 180, lambda: wire.results)
        page.screenshot(path=str(OUT / "04-1v1-late.png"))
        check(until(page, lambda: wire.results, 30), f"result arrived after {time.time() - t0:.0f} s")
        page.wait_for_timeout(1500)
        page.screenshot(path=str(OUT / "05-1v1-result.png"))
        if wire.results:
            r = wire.results[-1]
            ticks, _ = read_vu(r, 3)
            print(f"1v1 result: winner {r[1]} reason {r[2]} ticks {ticks}")
        check(4 in wire.phases, f"phase 4 reached ({wire.phases})")

        # Back to setup for the 2v2.
        bm.click(page, *bm.result_buttons(1, 1)[1], "Change setup")
        check(until(page, lambda: wire.phases[-1] == 0, 10), "back in the lobby")
        page.wait_for_timeout(1000)

        # ---- 2v2: me and a guest against two bots ----
        bm.click(page, *bm.format_button(3), "2v2")
        bm.click(page, *bm.picker_arrow(0, 1, 0, False), "blue 2 who <")
        page.screenshot(path=str(OUT / "06-setup-2v2.png"))
        bm.click(page, *bm.start(2), "Start")
        check(until(page, lambda: wire.phases[-1] == 1, 10), f"waiting room ({wire.phases})")
        page.wait_for_timeout(1200)
        page.screenshot(path=str(OUT / "07-waiting.png"))
        link, _ = bm.buttons_row([180, 140], bm.room_buttons(2))
        bm.click(page, *link, "Copy party link")
        copied = page.evaluate("navigator.clipboard.readText()")
        print("party link:", copied)
        check("#" in copied, "party link has a code")

        guest, gwire = new_page(context, "guest", errors)
        guest.goto(BASE.split("//")[0] + "//" + copied)
        guest.wait_for_function("() => !document.getElementById('loading')")
        check(until(guest, lambda: 1 in gwire.phases, 10), f"guest is in the waiting room ({gwire.phases})")
        guest.wait_for_timeout(1200)
        guest.screenshot(path=str(OUT / "08-guest-waiting.png"))
        guest.fill("#ui-textfield", "guest")
        guest.keyboard.press("Enter")
        check(until(page, lambda: 3 in wire.phases[wire.phases.index(1):], 20), f"2v2 started ({wire.phases})")
        check(until(guest, lambda: 3 in gwire.phases, 5), f"guest in the match ({gwire.phases})")
        page.wait_for_timeout(1500)
        page.screenshot(path=str(OUT / "09-2v2-me.png"))
        guest.screenshot(path=str(OUT / "10-2v2-guest.png"))
        n = len(wire.results)
        t0 = time.time()
        play([page, guest], 240, lambda: len(wire.results) > n)
        page.screenshot(path=str(OUT / "11-2v2-late-me.png"))
        guest.screenshot(path=str(OUT / "12-2v2-late-guest.png"))
        check(until(page, lambda: len(wire.results) > n, 60), f"2v2 result after {time.time() - t0:.0f} s")
        page.wait_for_timeout(1500)
        page.screenshot(path=str(OUT / "13-2v2-result-me.png"))
        guest.screenshot(path=str(OUT / "14-2v2-result-guest.png"))
        if len(wire.results) > n:
            r = wire.results[-1]
            ticks, _ = read_vu(r, 3)
            print(f"2v2 result: winner {r[1]} reason {r[2]} ticks {ticks}")
        check(gwire.results != [], "guest got the result too")
        print("notifications:", [x[1:60] for x in wire.notes + gwire.notes])
        check(errors == [], f"no page errors {errors}")
        browser.close()
    if fails:
        print("FAILED: " + "; ".join(fails))
        sys.exit(1)
    print("live play OK")


if __name__ == "__main__":
    main()
