#include "Const/TankDefinitions.h"

#include "nlohmann/json.hpp"

#include <fstream>

using nlohmann::json;

std::vector<TankDefinition*> TankDefinitions;
int TankCount = 0;

static BulletDefinition parseBullet(const json& j) {
    BulletDefinition b;
    b.type = j.at("type").get<std::string>();
    b.sizeRatio = j.at("sizeRatio").get<double>();
    b.health = j.at("health").get<double>();
    b.damage = j.at("damage").get<double>();
    b.speed = j.at("speed").get<double>();
    b.scatterRate = j.at("scatterRate").get<double>();
    b.lifeLength = j.at("lifeLength").get<double>();
    b.absorbtionFactor = j.at("absorbtionFactor").get<double>();
    if (j.contains("color") && !j.at("color").is_null()) b.color = j.at("color").get<int>();
    if (j.contains("sides") && !j.at("sides").is_null()) b.sides = j.at("sides").get<double>();
    return b;
}

static BarrelDefinition parseBarrel(const json& j) {
    BarrelDefinition b;
    b.angle = j.at("angle").get<double>();
    b.offset = j.at("offset").get<double>();
    if (j.contains("distance") && !j.at("distance").is_null()) b.distance = j.at("distance").get<double>();
    b.size = j.at("size").get<double>();
    b.width = j.at("width").get<double>();
    b.delay = j.at("delay").get<double>();
    b.reload = j.at("reload").get<double>();
    b.recoil = j.at("recoil").get<double>();
    b.isTrapezoid = j.at("isTrapezoid").get<bool>();
    b.trapezoidDirection = j.at("trapezoidDirection").get<double>();
    b.addon = (j.contains("addon") && !j.at("addon").is_null()) ? j.at("addon").get<std::string>() : "";
    if (j.contains("droneCount") && !j.at("droneCount").is_null()) b.droneCount = j.at("droneCount").get<int64_t>();
    if (j.contains("canControlDrones") && !j.at("canControlDrones").is_null()) b.canControlDrones = j.at("canControlDrones").get<bool>();
    if (j.contains("forceFire") && !j.at("forceFire").is_null()) b.forceFire = j.at("forceFire").get<bool>();
    if (j.contains("color") && !j.at("color").is_null()) b.color = j.at("color").get<int>();
    b.bullet = parseBullet(j.at("bullet"));
    return b;
}

static TankDefinition* parseTank(const json& j) {
    TankDefinition* t = new TankDefinition();
    t->id = j.at("id").get<int>();
    t->name = j.at("name").get<std::string>();
    t->upgradeMessage = j.value("upgradeMessage", "");
    t->levelRequirement = j.at("levelRequirement").get<double>();
    for (auto& u : j.at("upgrades")) t->upgrades.push_back(u.get<int>());

    const json& f = j.at("flags");
    t->flags.invisibility = f.value("invisibility", false);
    t->flags.zoomAbility = f.value("zoomAbility", false);
    t->flags.canClaimSquares = f.value("canClaimSquares", false);
    t->flags.devOnly = f.value("devOnly", false);
    t->flags.displayAsStar = f.value("displayAsStar", false);
    t->flags.displayAsTrapezoid = f.value("displayAsTrapezoid", false);

    t->visibilityRateShooting = j.at("visibilityRateShooting").get<double>();
    t->visibilityRateMoving = j.at("visibilityRateMoving").get<double>();
    t->invisibilityRate = j.at("invisibilityRate").get<double>();
    t->fieldFactor = j.at("fieldFactor").get<double>();
    t->speed = j.at("speed").get<double>();
    t->absorbtionFactor = j.at("absorbtionFactor").get<double>();
    t->maxHealth = j.at("maxHealth").get<double>();
    if (j.contains("bodyDamage") && !j.at("bodyDamage").is_null()) t->bodyDamage = j.at("bodyDamage").get<double>();
    t->preAddon = (j.contains("preAddon") && !j.at("preAddon").is_null()) ? j.at("preAddon").get<std::string>() : "";
    t->postAddon = (j.contains("postAddon") && !j.at("postAddon").is_null()) ? j.at("postAddon").get<std::string>() : "";
    t->sides = j.at("sides").get<double>();
    if (j.contains("widthRatio") && !j.at("widthRatio").is_null()) t->widthRatio = j.at("widthRatio").get<double>();
    t->borderWidth = j.at("borderWidth").get<double>();
    if (j.contains("baseSizeOverride") && !j.at("baseSizeOverride").is_null()) t->baseSizeOverride = j.at("baseSizeOverride").get<double>();

    for (auto& bj : j.at("barrels")) t->barrels.push_back(parseBarrel(bj));
    for (auto& sj : j.at("stats")) t->stats.push_back(StatDefinition{sj.at("name").get<std::string>(), sj.at("max").get<double>()});
    return t;
}

void initTankDefinitions() {
    std::ifstream f("data/TankDefinitions.json");
    json arr;
    f >> arr;

    TankDefinitions.clear();
    TankCount = 0;
    for (auto& entry : arr) {
        if (entry.is_null()) { TankDefinitions.push_back(nullptr); continue; }
        TankDefinitions.push_back(parseTank(entry));
        TankCount += 1;
    }
}

TankDefinition* getTankById(int id) {
    if (id < 0 || id >= (int)TankDefinitions.size()) return nullptr;
    return TankDefinitions[id];
}

TankDefinition* getTankByName(const std::string& name) {
    for (TankDefinition* t : TankDefinitions) if (t && t->name == name) return t;
    return nullptr;
}
