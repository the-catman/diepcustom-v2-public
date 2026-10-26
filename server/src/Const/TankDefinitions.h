#pragma once

#include <optional>
#include <string>
#include <vector>

constexpr double visibilityRateDamage = 0.2;

struct BulletDefinition {
    std::string type;
    double sizeRatio;
    double health;
    double damage;
    double speed;
    double scatterRate;
    double lifeLength;
    double absorbtionFactor;
    std::optional<int> color;
    std::optional<double> sides;
};

struct BarrelDefinition {
    double angle;
    double offset;
    std::optional<double> distance;
    double size;
    double width;
    double delay;
    double reload;
    double recoil;
    bool isTrapezoid;
    double trapezoidDirection;
    std::string addon; // "" = null
    std::optional<int64_t> droneCount;
    std::optional<bool> canControlDrones;
    std::optional<bool> forceFire;
    std::optional<int> color;
    BulletDefinition bullet;
};

struct StatDefinition {
    std::string name;
    double max;
};

struct TankFlags {
    bool invisibility = false;
    bool zoomAbility = false;
    bool canClaimSquares = false;
    bool devOnly = false;
    bool displayAsStar = false;
    bool displayAsTrapezoid = false;
};

struct TankDefinition {
    int id;
    std::string name;
    std::string upgradeMessage;
    double levelRequirement;
    std::vector<int> upgrades;
    TankFlags flags;
    double visibilityRateShooting;
    double visibilityRateMoving;
    double invisibilityRate;
    double fieldFactor;
    double speed;
    double absorbtionFactor;
    double maxHealth;
    std::optional<double> bodyDamage;
    std::string preAddon; // "" = null
    std::string postAddon; // "" = null
    double sides;
    std::optional<double> widthRatio;
    double borderWidth;
    std::optional<double> baseSizeOverride;
    std::vector<BarrelDefinition> barrels;
    std::vector<StatDefinition> stats;
};

// Nullable entries, indexed by tank id.
extern std::vector<TankDefinition*> TankDefinitions;
extern int TankCount;

void initTankDefinitions();
TankDefinition* getTankById(int id);
TankDefinition* getTankByName(const std::string& name);
