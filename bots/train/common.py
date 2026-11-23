import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.environ.get("DIEPENV_BUILD") or os.path.join(ROOT, "bots", "env", "build")
sys.path.insert(0, BUILD)

import diepenv  # noqa: E402

diepenv.init(os.path.join(ROOT, "server"))

SELF_DIM = diepenv.SELF_DIM
ENT_DIM = diepenv.ENT_DIM
# How much of each the bots before the class features were shown.
OLD_SELF_DIM = diepenv.OLD_SELF_DIM
OLD_ENT_DIM = diepenv.OLD_ENT_DIM
ENT_MAX = diepenv.ENT_MAX
MOVES = diepenv.MOVES

with open(os.path.join(ROOT, "server", "data", "TankDefinitions.json")) as f:
    _TANKS = [t for t in json.load(f) if t]
TANK_NAMES = {t["id"]: t["name"] for t in _TANKS}
TANK_NAMES.update({45: "Destroyer Dominator", 46: "Gunner Dominator", 47: "Trapper Dominator"})
# Classes whose drones follow the mouse while clicking; they need the repel and mouse distance controls.
MOUSE_DRONES = {t["id"] for t in _TANKS if any(b.get("canControlDrones") for b in t["barrels"])}


def tank_id(name):
    """'machine-gun', 'Machine Gun' or '7' -> 7. The basic tank is 'basic' or 'tank'."""
    if str(name).isdigit():
        return int(name)
    key = str(name).replace("-", " ").replace("_", " ").lower()
    if key == "basic":
        key = "tank"
    for i, n in TANK_NAMES.items():
        if n.lower() == key:
            return i
    raise ValueError(f"unknown tank {name}")


def tanks_of(env_args):
    """The classes a checkpoint trained as and against."""
    return list(env_args.get("tanks", [0, 0]))


# Builds are written in diep's order, regen first and movement last; the server keeps them the other way round.
DEFAULT_BUILD = "0/4/0/5/6/6/7/5"


def parse_build(text):
    """'0/0/0/7/7/7/7/5' -> the server's 8 stat points."""
    points = [int(x) for x in str(text).split("/")]
    if len(points) != 8 or not all(0 <= x <= 7 for x in points) or sum(points) > 33:
        raise ValueError(f"a build is 8 numbers from 0 to 7 joined by /, at most 33 in all: {text}")
    return points[::-1]


# Each class's usual build, in diep's order; any class not named plays GLASS.
GLASS = "0/0/0/5/7/7/7/7"
SNIPER = "0/0/0/7/7/7/5/7"
BUILDS = {
    "Sniper": SNIPER, "Assassin": SNIPER, "Ranger": SNIPER, "Hunter": SNIPER, "Predator": SNIPER,
    "Overseer": SNIPER, "Overlord": SNIPER, "Manager": SNIPER, "Necromancer": SNIPER, "Overtrapper": SNIPER,
    "Mothership": SNIPER, "Destroyer": SNIPER, "Hybrid": SNIPER, "Annihilator": SNIPER,
    "Factory": "0/0/0/7/7/7/7/5", "Battleship": "0/0/0/7/7/7/7/5",
    "Arena Closer": "0/5/0/7/0/7/7/7",
}
# Room in a class embedding for every tank id.
NUM_CLASSES = max(TANK_NAMES) + 1


def build_for(tank):
    """The server's stat points for a class's usual build."""
    return parse_build(BUILDS.get(TANK_NAMES[tank], GLASS))


def parse_classes(text):
    """'fighter:3,penta-shot' -> [(24, 3.0), (14, 1.0)]"""
    out = []
    for part in filter(None, (text or "").split(",")):
        cls, _, weight = part.partition(":")
        out.append((tank_id(cls), float(weight or 1)))
    return out


def build_text(stats):
    return "/".join(str(x) for x in stats[::-1])


def builds_of(env_args):
    """The builds a checkpoint's two slots had; the first bots all had the default."""
    stats = env_args.get("stats")
    if stats is None:
        return [parse_build(DEFAULT_BUILD), parse_build(DEFAULT_BUILD)]
    return [list(x) for x in stats]


def versus(env_args, tank, stats):
    """env_args with slot 1 played by this class and build."""
    return dict(env_args, tanks=[tanks_of(env_args)[0], tank], stats=[builds_of(env_args)[0], list(stats)])


KINDS = {
    "still": diepenv.STILL,
    "linear": diepenv.LINEAR,
    "strafe": diepenv.STRAFE,
    "fighter": diepenv.FIGHTER,
    "self": diepenv.EXTERNAL,
}


def make_config(opponent="fighter", **overrides):
    c = diepenv.Config()
    c.kinds = [diepenv.EXTERNAL, KINDS[opponent]]
    for k, v in overrides.items():
        if not hasattr(c, k):
            raise AttributeError(f"Config has no field {k}")
        setattr(c, k, v)
    return c
