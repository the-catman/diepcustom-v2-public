#pragma once

#include <vector>

class Barrel;
class TankBody;

namespace bots {

// How far a tank's bullet is from the tank's centre k ticks after the shot, from its barrel stats.
struct Ballistics {
    std::vector<double> reach;
    double bulletRadius = 0;

    // From the tank's first barrel unless given another.
    void build(TankBody* tank, Barrel* barrel = nullptr);

    // Where to aim from (sx, sy) to hit a target at (tx, ty) moving at (vx, vy) per tick.
    // False when the bullet dies before reaching it; the aim point is then the target itself.
    bool intercept(double sx, double sy, double tx, double ty, double vx, double vy,
                   double& ax, double& ay, double& ticks) const;
};

}
