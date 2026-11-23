import json
import os
import signal
import subprocess
import sys
import time

import torch

TRAIN = os.path.join(os.path.dirname(__file__), "..", "train")
# Tiny everything, with a self-play pool that grows every other iteration.
SMALL = ["--duels", "8", "--threads", "2", "--rollout", "16", "--lag", "4", "--minibatch", "64", "--micro", "32",
         "--width", "32", "--layers", "1", "--mix", "fighter:1,self:1", "--pool-every", "2",
         "--eval-every", "1000", "--eval-episodes", "8"]


def command(out, *extra):
    return [sys.executable, "ppo.py", "--out", str(out), *SMALL, *extra]


def run(out, *extra):
    return subprocess.run(command(out, *extra), cwd=TRAIN, capture_output=True, text=True)


def rows(out):
    with open(out / "log.jsonl") as f:
        return [json.loads(line) for line in f]


def adam_steps(state):
    return next(iter(state["opt"]["state"].values()))["step"].item()


def test_resume_carries_on_where_the_run_stopped(tmp_path):
    out = tmp_path / "run"
    r = run(out, "--iters", "3")
    assert r.returncode == 0, r.stderr
    first = torch.load(out / "resume.pt", weights_only=False)
    assert first["iter"] == 3

    # Starting over in the same folder would mix two runs' logs.
    assert run(out, "--iters", "3").returncode != 0

    r = run(out, "--resume", "--iters", "5")
    assert r.returncode == 0, r.stderr
    log = rows(out)
    assert [row["iter"] for row in log] == [1, 2, 3, 4, 5]
    done = torch.load(out / "resume.pt", weights_only=False)
    assert done["iter"] == 5
    # Adam kept counting from where it was, and the pool kept the snapshots it had.
    assert adam_steps(first) == sum(row["updates"] for row in log[:3])
    assert adam_steps(done) == sum(row["updates"] for row in log)
    assert len(done["pool"]) == len(first["pool"]) + 1
    assert done["total_steps"] == log[-1]["steps"]


def test_a_signal_stops_after_the_iteration_and_saves(tmp_path):
    out = tmp_path / "run"
    p = subprocess.Popen(command(out, "--iters", "1000", "--save-every", "1000"), cwd=TRAIN,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 120
    while not (out / "log.jsonl").exists() or not rows(out):
        assert p.poll() is None, p.stderr.read()
        assert time.time() < deadline
        time.sleep(0.2)
    p.send_signal(signal.SIGTERM)
    assert p.wait(120) == 128 + signal.SIGTERM
    saved = torch.load(out / "resume.pt", weights_only=False)
    assert saved["iter"] == rows(out)[-1]["iter"] < 1000


def test_two_rivals_train_in_step(tmp_path):
    anchors = {}
    for tank in ("basic", "twin"):
        r = run(tmp_path / f"{tank}-start", "--iters", "1", "--tank", tank)
        assert r.returncode == 0, r.stderr
        anchors[tank] = str(tmp_path / f"{tank}-start" / "it00001.pt")
    procs = {}
    for tank, other in (("basic", "twin"), ("twin", "basic")):
        procs[tank] = subprocess.Popen(
            command(tmp_path / tank, "--iters", "5", "--tank", tank, "--eval-every", "2", "--frozen-eval-every", "4",
                    "--frozen-eval-episodes", "4", "--mix", "self:1,rival:1", "--rival", str(tmp_path / other),
                    "--rival-anchor", anchors[other], "--rival-wait", "120"),
            cwd=TRAIN, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for tank, p in procs.items():
        out, err = p.communicate(timeout=300)
        assert p.returncode == 0, err
    for tank in ("basic", "twin"):
        log = rows(tmp_path / tank)
        assert [row["iter"] for row in log] == [1, 2, 3, 4, 5]
        # Iterations 1-2 only have the anchor; from 3 on the rival's iteration-2 save, from 5 its 4.
        assert [row["rival_pool"]["size"] for row in log] == [1, 1, 2, 2, 3]
        assert set(log[3]["rival_eval"]) == {"anchor", "newest"}
        assert "rival" in log[-1]
