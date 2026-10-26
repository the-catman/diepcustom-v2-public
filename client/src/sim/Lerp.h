#pragma once

#include <cmath>

struct LerpVal {
    float target = 0;
    float from = 0;
    double changeTime = -1e18;
    // While owned (the own tank's position) the client drives `target` and packets land in
    // `stash`, which the network lerp from `from` heads for instead.
    float stash = 0;
    bool owned = false;

    void snap(float v) { target = from = stash = v; changeTime = -1e18; owned = false; }
    void set(float v, double now) {
        if (v == target) return;
        from = getSmooth(now);
        target = v;
        changeTime = now;
    }
    void setLinear(float v, double now) {
        if (v == netTarget()) return;
        from = netLinear(now);
        (owned ? stash : target) = v;
        changeTime = now;
    }
    // Take `target` over from the network; giving it back resets it to `from`.
    void own() {
        if (owned) return;
        owned = true;
        stash = target;
    }
    void disown() {
        if (!owned) return;
        owned = false;
        target = from;
    }
    // Capture via the shortest arc; the encoding hops between +pi and -pi.
    void setAngle(float v, double now) {
        if (v == target) return;
        from = getAngle(now);
        target = v;
        changeTime = now;
    }

    double t(double now) const {
        double x = (now - changeTime) / 100.0;
        return x < 0 ? 0 : (x > 1 ? 1 : x);
    }
    float netTarget() const { return owned ? stash : target; }
    float netLinear(double now) const { return (float)(t(now) * (netTarget() - from) + from); }
    float getLinear(double now) const { return owned ? target : netLinear(now); }
    float getSmooth(double now) const {
        double x = t(now);
        return from + (float)(x * x * (3 - 2 * x) * (target - from));
    }
    float getAngle(double now) const {
        double x = t(now);
        double delta = std::fmod((double)target - from, 2 * M_PI);
        delta = std::fmod(delta + 3 * M_PI, 2 * M_PI) - M_PI;
        return from + (float)(x * x * (3 - 2 * x) * delta);
    }
};
