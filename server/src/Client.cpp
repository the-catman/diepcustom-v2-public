#include "Client.h"
#include "Coder/Reader.h"
#include "Native/Camera.h"
#include "Native/Arena.h"
#include "Native/Manager.h"
#include "Physics/CollisionManager.h"
#include "Entity/Object.h"
#include "Entity/Tank/TankBody.h"
#include "Entity/Boss/AbstractBoss.h"
#include "Const/TankDefinitions.h"
#include "Const/Commands.h"
#include "Const/Enums.h"
#include "Crypto.h"
#include "Game.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

std::unordered_set<std::string> bannedClients;

// Lazy: magicNum and TankCount aren't initialized yet during this TU's static init.
static int tankXor() { return config::magicNum % TankCount; }
static int statXor() { return config::magicNum % StatCount; }
static const uint8_t PING_PACKET[1] = {ClientBound::Ping};
static constexpr double DEFAULT_FOV = 0.35;
static const int INIT_TIMEOUT_TICKS = (int)(config::tps * 12);
static constexpr int POSSESSION_TANK_ID = Tank::MountedTurret;

WSWriterStream::WSWriterStream(Client* client) : client(client) {
    buffer.swap(client->writeScratch);
    buffer.clear();
}

WSWriterStream::~WSWriterStream() { client->writeScratch.swap(buffer); }

void WSWriterStream::send() { client->send(data(), size()); }

Client::Client(GameServer* game) : game(game) {
    inputs = std::make_shared<ClientInputs>(this);
    game->clients.add(this);
    lastPingTick = connectTick = game->tick;
}

void Client::acceptClient() {
    write().u8(ClientBound::ServerInfo).stringNT(game->gamemode).stringNT(config::host).send();
    write().u8(ClientBound::PlayerCount).vu(GameServer::globalPlayerCount).send();
    write().u8(ClientBound::Accept).vi(accessLevel).send();
    camera = new ClientCamera(game, this);

    // Every mode offers a party link: team modes pick the team now and send its code, a sandbox
    // instance sends its own, and the rest send an empty one (the link is just the server id).
    std::string party = game->partyCode.empty() ? game->arena->partyCode(this) : game->partyCode;
    write().u8(ClientBound::PartyCode).bytes(std::vector<uint8_t>(party.begin(), party.end())).send();
}

// Before Init a client holds no entities, so it can move to another game outright.
void Client::switchGame(GameServer* target) {
    game->clients.erase(this);
    game = target;
    target->clients.add(this);
    lastPingTick = connectTick = target->tick;
}

// Init's party field: two hex digits per byte, low nibble first.
static std::string decodePartyHex(const std::string& hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    std::string out;
    for (size_t i = 0; i < hex.size(); i += 2) {
        int b = nibble(hex[i]);
        if (i + 1 < hex.size()) b |= nibble(hex[i + 1]) << 4;
        out += (char)b;
    }
    return out;
}

void Client::send(const uint8_t* data, size_t len) {
    if (!connected) return;
    sendRaw(data, len);
}

void Client::onClose() {
    connected = false;
    terminate();
}

void Client::terminate() {
    if (terminated) return;
    if (connected) { closeWs(); return; }

    terminated = true;
    game->clients.erase(this);
    if (game->arena) game->arena->onClientRemoved(this);
    inputs->deleted = true;
    inputs->movement.setMagnitude(0);
    inputs->client = nullptr; // a possessed AI may still hold this ClientInputs; don't let it dangle
    if (Entity::exists(camera)) camera->delete_();
}

void Client::onMessage(const uint8_t* buffer, size_t len, bool isBinary) {
    if (!isBinary) return terminate();
    if (len == 0) return;
    if (buffer[0] == 0x00 && len == 1) return terminate();
    int header = buffer[0];

    std::vector<uint8_t> data(buffer, buffer + len);

    if (header == ServerBound::Ping) {
        lastPingTick = game->tick;
        send(PING_PACKET, 1);
        return;
    }

    auto& cache = incomingCache[header];
    if (!cache.empty()) {
        if (header == ServerBound::Input) {
            try {
                Reader r(data.data(), data.size());
                r.at = 1;
                uint32_t flags = r.vu();
                inputs->cachedFlags |= flags & 0b110111100001;
            } catch (std::exception&) { return terminate(); }
        } else if (header == ServerBound::StatUpgrade) {
            cache.push_back(std::move(data));
        }
        return;
    }
    cache.push_back(std::move(data));
}

void Client::handleIncoming(int header, const std::vector<uint8_t>& data) {
    if (terminated) return;
    try {
        parsePacket(header, data);
    } catch (std::exception& e) {
        util::log("Malformed packet (header=", header, ") from ", toString(), ": ", e.what());
        terminate();
    }
}

void Client::parsePacket(int header, const std::vector<uint8_t>& data) {
    Reader r(data.data(), data.size());
    r.at = 1;

    ClientCamera* camera = this->camera;

    if (header == ServerBound::Init) {
        if (camera) return terminate();

        std::string buildHash = r.stringNT();
        std::string pw = r.stringNT();
        std::string party;
        if (r.at < data.size()) party = decodePartyHex(r.stringNT());

        if (buildHash != config::buildHash) {
            util::log("Kicking client. Invalid build hash ", buildHash);
            write().u8(ClientBound::OutdatedClient).stringNT(config::buildHash).send();
            kickAtTick = game->tick + 3;
            return;
        }

        if (!config::devPasswordHash.empty() && crypto::sha256Hex(pw) == config::devPasswordHash) {
            accessLevel = config::AccessLevel::FullAccess;
            util::saveToLog("Developer Connected", "A client connected with full access.", 0x5a65ea);
        } else {
            accessLevel = config::defaultAccessLevel;
        }

        if (accessLevel == config::AccessLevel::NoAccess) return terminate();

        if (!party.empty()) {
            // On a private instance the code names the sandbox to join; elsewhere, a team.
            GameServer* target = nullptr;
            if (!game->partyCode.empty()) {
                for (GameServer* g : GameServer::instances) {
                    if (g->gamemode == game->gamemode && g->partyCode == party) target = g;
                }
            }
            if (target) {
                switchGame(target);
            } else if (!game->arena->joinParty(this, party)) {
                write().u8(ClientBound::InvalidParty).send();
                kickAtTick = game->tick + 3;
                return;
            }
        }

        acceptClient();
        return;
    }

    if (!Entity::exists(camera)) return;

    switch (header) {
        case ServerBound::Input: {
            uint32_t previousFlags = inputs->flags;
            uint32_t flags = inputs->flags = r.vu() | inputs->cachedFlags;
            inputs->cachedFlags = 0;

            double fov = camera->cameraData.FOV;
            double width = (1920 / fov) / 2;
            double height = (1080 / fov) / 2;

            double minX = camera->cameraData.cameraX - width;
            double maxX = camera->cameraData.cameraX + width;
            double minY = camera->cameraData.cameraY - height;
            double maxY = camera->cameraData.cameraY + height;

            double mouseX = r.vf();
            double mouseY = r.vf();

            if (!std::isfinite(mouseX) || !std::isfinite(mouseY)) break;

            inputs->mouse.x = util::constrain(mouseX, minX, maxX);
            inputs->mouse.y = util::constrain(mouseY, minY, maxY);

            inputs->gamepad = {0, 0};
            if (flags & InputFlags::gamepad) {
                double padX = r.vf();
                double padY = r.vf();
                double length = std::sqrt(padX * padX + padY * padY);
                if (std::isfinite(length) && length > 0) {
                    if (length > 1) { padX /= length; padY /= length; }
                    inputs->gamepad = {padX, padY};
                }
            }

            Entity* playerE = camera->cameraData.player;
            if (!Entity::exists(playerE) || !TankBody::isTank(playerE)) return;
            TankBody* player = (TankBody*)playerE;
            if (inputs->isPossessing && accessLevel != config::AccessLevel::FullAccess) return;

            if (flags & InputFlags::invincibility) {
                if (game->arena->arenaData.flags & ArenaFlags::canUseCheats || accessLevel >= config::AccessLevel::BetaAccess) {
                    if (game->clients.size() == 1 && game->arena->isOpen()) {
                        setHasCheated(true);
                        player->setInvulnerability(!player->isInvulnerable);
                        notify(std::string("Invincibility: ") + (player->isInvulnerable ? "ON" : "OFF"), 0x000000, 5000, "invincibility_toggle");
                    }
                }
            }

            if ((flags & InputFlags::switchtank) && !(previousFlags & InputFlags::switchtank)) {
                if (accessLevel >= config::AccessLevel::BetaAccess || (game->arena->arenaData.flags & ArenaFlags::canUseCheats)) {
                    setHasCheated(true);
                    int count = (int)TankDefinitions.size();
                    int tank = player->currentTank;
                    do tank = (tank + count - 1) % count;
                    while (!TankDefinitions[tank] || (TankDefinitions[tank]->flags.devOnly && accessLevel < config::AccessLevel::FullAccess));
                    player->setTank(tank);
                }
            }

            if (flags & InputFlags::levelup) {
                if ((accessLevel == config::AccessLevel::FullAccess) ||
                    (camera->cameraData.level < config::maxPlayerLevel &&
                     ((game->arena->arenaData.flags & ArenaFlags::canUseCheats) || (accessLevel == config::AccessLevel::BetaAccess)))) {
                    setHasCheated(true);
                    camera->setLevel(camera->cameraData.level + 1);
                }
            }

            if ((flags & InputFlags::suicide) && !player->deletionAnimation) {
                if (accessLevel >= config::AccessLevel::BetaAccess || (game->arena->arenaData.flags & ArenaFlags::canUseCheats)) {
                    setHasCheated(true);
                    player->destroy();
                    player->onDeath(player);
                    player->onKill(player, player);
                }
            }
            return;
        }
        case ServerBound::Spawn: {
            if (game->arena->arenaData.flags & ArenaFlags::noJoining) return;
            if (Entity::exists(camera->cameraData.player)) return;

            std::string name = util::truncateUtf8(util::trim(r.stringNT()), 16);
            auto& q = game->clientsAwaitingSpawn;
            auto it = std::find_if(q.begin(), q.end(), [&](auto& p) { return p.first == this; });
            if (it != q.end()) it->second = name;
            else q.push_back({this, name});
            return;
        }
        case ServerBound::StatUpgrade: {
            if (camera->cameraData.statsAvailable <= 0) return;

            Entity* playerE = camera->cameraData.player;
            if (!TankBody::isTank(playerE)) return;
            TankBody* player = (TankBody*)playerE;

            TankDefinition* definition = getTankById(player->currentTank);
            if (!definition || definition->stats.empty()) return;

            int statId = r.vi() ^ statXor();
            if (statId < 0 || statId >= StatCount) return;

            double statLimit = camera->cameraData.statLimits.values[statId];
            if (camera->cameraData.statLevels.values[statId] >= statLimit) return;

            camera->addStat(statId, 1);
            camera->cameraData.setStatsAvailable(camera->cameraData.statsAvailable - 1);
            return;
        }
        case ServerBound::TankUpgrade: {
            Entity* playerE = camera->cameraData.player;
            if (!TankBody::isTank(playerE)) return;
            TankBody* player = (TankBody*)playerE;

            TankDefinition* definition = getTankById(player->currentTank);
            int tankId = r.vi() ^ tankXor();
            TankDefinition* tankDefinition = getTankById(tankId);
            bool canUpgrade = definition &&
                std::find(definition->upgrades.begin(), definition->upgrades.end(), tankId) != definition->upgrades.end();
            if (!canUpgrade || !tankDefinition || tankDefinition->levelRequirement > camera->cameraData.level) return;

            player->setTank(tankId);
            return;
        }
        case ServerBound::ExtensionFound: {
            util::log("Someone is cheating");
            ban();
            return;
        }
        case ServerBound::ToRespawn: {
            camera->cameraData.setFlags(camera->cameraData.flags & ~CameraFlags::showingDeathStats);
            return;
        }
        case ServerBound::TakeTank: {
            if (!Entity::exists(camera->cameraData.player)) return;
            if (game->entities->AIs.empty()) return notify("Someone has already taken this tank", 0x000000, 5000, "cant_claim_info");
            if (!inputs->isPossessing) {
                ObjectEntity* playerObj = (ObjectEntity*)camera->cameraData.player.get();
                double x = playerObj->getPositionData() ? playerObj->positionData.x : 0;
                double y = playerObj->getPositionData() ? playerObj->positionData.y : 0;
                std::vector<AI*> AIs(game->entities->AIs.begin(), game->entities->AIs.end());
                std::sort(AIs.begin(), AIs.end(), [&](AI* a, AI* b) {
                    Vector pa = a->owner->getWorldPosition();
                    Vector pb = b->owner->getWorldPosition();
                    return ((pa.x - x) * (pa.x - x) + (pa.y - y) * (pa.y - y)) < ((pb.x - x) * (pb.x - x) + (pb.y - y) * (pb.y - y));
                });

                for (AI* ai : AIs) {
                    if (ai->state != AIState::possessed &&
                        ((ai->owner->relationsData.team == camera->relationsData.team && ai->isClaimable) ||
                         accessLevel == config::AccessLevel::FullAccess)) {
                        if (!possess(ai)) continue;
                        // Only full access can take a tank that isn't a claimable teammate.
                        if (!(ai->owner->relationsData.team == camera->relationsData.team && ai->isClaimable)) {
                            achievementsBlocked = true;
                        }
                        return;
                    }
                }
                notify("Someone has already taken that tank", 0x000000, 5000, "cant_claim_info");
            } else {
                camera->cameraData.setFOV(DEFAULT_FOV);
                inputs->deleted = true;
            }
            return;
        }
        case ServerBound::TCPInit: {
            if (!config::enableCommands) return;
            std::string cmd = r.stringNT();
            int argsLength = r.u8();
            std::vector<std::string> args;
            for (int i = 0; i < argsLength; ++i) args.push_back(r.stringNT());
            executeCommand(this, cmd, args);
            return;
        }
        default:
            util::log("Suspicious activity detected, banning client");
            return ban();
    }
}

void Client::setHasCheated(bool value) {
    Entity* playerE = camera ? camera->cameraData.player : nullptr;
    if (playerE && playerE->getNameData()) {
        NameData* n = playerE->getNameData();
        if (value) n->setFlags(n->flags | NameFlags::highlightedName);
        else n->setFlags(n->flags & ~NameFlags::highlightedName);
    }
    devCheatsUsed = value;
    if (value) achievementsBlocked = true;
}

bool Client::hasCheated() const { return devCheatsUsed; }

bool Client::possess(AI* ai) {
    if (!camera || ai->state == AIState::possessed) return false;

    inputs->deleted = true;
    ai->inputs = inputs = std::make_shared<ClientInputs>(this);
    inputs->isPossessing = true;
    ai->state = AIState::possessed;

    if (ObjectEntity::isObject(camera->cameraData.player)) {
        ObjectEntity* p = (ObjectEntity*)camera->cameraData.player.get();
        int color = p->styleData.color;
        p->styleData.color = -1;
        p->styleData.setColor(color);
    }

    camera->cameraData.setTankOverride(ai->owner->getNameData() ? ai->owner->getNameData()->name : "");
    camera->cameraData.setTank(POSSESSION_TANK_ID);

    for (int i = 0; i < StatCount; ++i) {
        camera->cameraData.statLevels.set(i, 0);
        camera->cameraData.statLimits.set(i, 7);
        camera->cameraData.statNames.set(i, "");
    }

    camera->cameraData.setKilledBy("");
    camera->cameraData.setPlayer(ai->owner);
    camera->cameraData.setMovementSpeed(ai->movementSpeed);

    if (TankBody::isTank(ai->owner)) {
        CameraData& srcCam = ((TankBody*)ai->owner.get())->cameraEntity->cameraData;
        camera->cameraData.setTank(srcCam.tank);
        camera->setLevel(srcCam.level);
        for (int i = 0; i < StatCount; ++i) {
            camera->cameraData.statLevels.set(i, srcCam.statLevels.values[i]);
            camera->cameraData.statLimits.set(i, srcCam.statLimits.values[i]);
            camera->cameraData.statNames.set(i, srcCam.statNames.values[i]);
        }
        camera->cameraData.setFOV(srcCam.FOV);
    } else if (AbstractBoss::isBoss(ai->owner)) {
        camera->setLevel(75);
        camera->cameraData.setFOV(DEFAULT_FOV);
    } else {
        camera->setLevel(30);
        camera->cameraData.setFOV(DEFAULT_FOV);
    }

    camera->cameraData.setStatsAvailable(0);
    camera->cameraData.setScore(0);
    camera->entityState = EntityStateFlags::needsCreate | EntityStateFlags::needsDelete;
    notify("Press H to surrender control of the tank", 0x000000, 15000);
    return true;
}

void Client::notify(const std::string& text, uint32_t color, double time, const std::string& id) {
    if (!connected) return;
    write().u8(ClientBound::Notification).stringNT(text).u32(color).floatLE((float)time).stringNT(id).send();
}

void Client::ban() {
    if (!connected) return;

    util::saveToLog("IP Banned", "Banned " + ipAddress + toString(true), 0xee326a);
    if (accessLevel >= config::unbannableLevelMinimum) {
        util::saveToLog("IP Ban Cancelled", "Cancelled ban on " + ipAddress + toString(true), 0x6a32ee);
        return;
    }

    bannedClients.insert(ipAddress);
    for (Client* client : game->clients) {
        if (client->ipAddress == ipAddress) client->terminate();
    }
}

void Client::createAndSpawnPlayer(const std::string& name) {
    ClientCamera* camera = this->camera;
    if (!Entity::exists(camera)) return;

    camera->cameraData.statsAvailable = 0;
    camera->cameraData.level = 1;
    for (int i = 0; i < StatCount; ++i) camera->cameraData.statLevels.values[i] = 0;

    TankBody* tank = new TankBody(game, camera, inputs);
    camera->relationsData.setParent(tank);
    camera->relationsData.setOwner(tank);
    camera->cameraData.setPlayer(tank);
    tank->setTank(Tank::Basic);
    tank->nameData.name = name;
    game->arena->spawnPlayer(tank, this);
    game->entities->collisionManager->insert(tank);
    camera->setLevel(camera->cameraData.respawnLevel);

    if (hasCheated()) setHasCheated(true);

    camera->entityState = EntityStateFlags::needsCreate | EntityStateFlags::needsDelete;
    camera->spectatee = nullptr;
    inputs->isPossessing = false;
    inputs->movement.setMagnitude(0);
}

void Client::tick(int tick) {
    if (kickAtTick >= 0 && tick >= kickAtTick) { terminate(); return; }

    VectorAbstract movement{0, 0};
    if (inputs->flags & InputFlags::up) movement.y -= 1;
    if (inputs->flags & InputFlags::down) movement.y += 1;
    if (inputs->flags & InputFlags::right) movement.x += 1;
    if (inputs->flags & InputFlags::left) movement.x -= 1;
    if (movement.x || movement.y) {
        double angle = std::atan2(movement.y, movement.x);
        double magnitude = util::constrain(std::sqrt(movement.x * movement.x + movement.y * movement.y), -1, 1);
        inputs->movement.setMagnitude(magnitude);
        inputs->movement.setAngle(angle);
    } else if ((inputs->flags & InputFlags::gamepad) && (inputs->gamepad.x || inputs->gamepad.y)) {
        inputs->movement.set(inputs->gamepad);
    }

    for (int header = 0; header < (int)incomingCache.size(); ++header) {
        if (header == ServerBound::Ping) continue;
        auto& cache = incomingCache[header];
        if (cache.size() == 1) handleIncoming(header, cache[0]);
        else if (cache.size() > 1) { for (auto& pkt : cache) handleIncoming(header, pkt); }
        else continue;
        cache.clear();
    }

    if (!camera) {
        if (tick == connectTick + INIT_TIMEOUT_TICKS) return terminate();
    } else if (inputs->deleted) {
        inputs = std::make_shared<ClientInputs>(this);
        camera->cameraData.setPlayer(nullptr);
        camera->cameraData.setRespawnLevel(0);
        camera->cameraData.setCameraX(0);
        camera->cameraData.setCameraY(0);
        camera->cameraData.setFlags(camera->cameraData.flags & ~CameraFlags::showingDeathStats);
    }

    if (!pendingAchievements.empty()) {
        auto w = write();
        w.u8(ClientBound::Achievement).vu((int32_t)pendingAchievements.size());
        for (const std::string& key : pendingAchievements) w.stringNT(key);
        w.send();
        pendingAchievements.clear();
    }

    if (tick >= lastPingTick + 60 * (int)config::tps) return terminate();
}

std::string Client::toString(bool verbose) const {
    std::string out = "Client(" + std::to_string(accessLevel) + ")";
    if (verbose && !ipAddress.empty()) out += " ip=" + ipAddress;
    if (terminated) out += " (terminated)";
    return out;
}
