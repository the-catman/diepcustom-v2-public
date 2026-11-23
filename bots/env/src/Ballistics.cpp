#include "Ballistics.h"

#include <cmath>

#include "Entity/Tank/Barrel.h"
#include "Entity/Tank/TankBody.h"

namespace bots {

void Ballistics::build(TankBody* tank, Barrel* barrel) {
    reach.clear();
    if (!barrel && tank->barrels.empty()) return;
    if (!barrel) barrel = tank->barrels[0];
    const BulletDefinition& def = barrel->definition->bullet;
    double accel = barrel->bulletAccel;
    double start = barrel->physicsData.size + barrel->definition->distance.value_or(0);
    bulletRadius = barrel->physicsData.width / 2 * def.sizeRatio;

    // Bullet::tick: the launch speed lands the tick after the shot, then it keeps accelerating
    // towards accel against the 0.9 friction.
    int life = (int)(def.lifeLength * 75);
    double pos = start, vel = 0;
    reach.push_back(pos);
    for (int k = 1; k <= life; ++k) {
        pos += vel;
        vel *= 0.9;
        vel += k == 1 ? accel + 30 - 0.5 * def.scatterRate : accel * 0.1;
        reach.push_back(pos);
    }
}

bool Ballistics::intercept(double sx, double sy, double tx, double ty, double vx, double vy,
                           double& ax, double& ay, double& ticks) const {
    ax = tx;
    ay = ty;
    ticks = 0;
    double prevGap = 0;
    for (size_t k = 0; k < reach.size(); ++k) {
        // The shot leaves on the next tick, so the target has moved one tick more than the bullet.
        double px = tx + vx * (k + 1), py = ty + vy * (k + 1);
        double gap = std::hypot(px - sx, py - sy) - reach[k];
        if (gap <= 0) {
            double bt = k == 0 ? 0 : k - 1 + prevGap / (prevGap - gap);
            ax = tx + vx * (bt + 1);
            ay = ty + vy * (bt + 1);
            ticks = bt;
            return true;
        }
        prevGap = gap;
    }
    return false;
}

}
