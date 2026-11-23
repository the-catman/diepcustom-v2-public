#pragma once

#include <vector>

#include "Misc/ShapeManager.h"

class ObjectEntity;

namespace bots {

// Shapes in FFA: 2000 on a 22300 wide map, per square unit.
constexpr double FFA_SHAPE_DENSITY = 2000.0 / (22300.0 * 22300.0);

// How many shapes a game has and what they are. Squares and triangles come 5 to 1, as outside the nest.
struct ShapeMix {
    int count = 0;
    double pentagons = 0.04;
};

// The field's shapes, laid out over the whole arena before the first decision. The game's own spawner
// keeps them 1000 away from every tank and fills only 10 a tick, so fights would start in the clear.
// Shapes broken mid-game come back the game's way.
class DuelShapes : public ShapeManager {
public:
    DuelShapes(ArenaEntity* arena, const ShapeMix& mix);

    // Fills every slot, each shape at least margin clear of every one of these.
    void prefill(const std::vector<ObjectEntity*>& clearOf, double margin);
    // One shape of a kind (3 triangle, 4 square, 5 pentagon, 6 alpha pentagon) at (x, y), outside the slots.
    AbstractShape* place(int kind, double x, double y);

protected:
    AbstractShape* spawnShape() override;
    int wantedShapes() const override { return mix.count; }

private:
    ShapeMix mix;

    AbstractShape* pick();
    void put(AbstractShape* shape, double x, double y);
};

}
