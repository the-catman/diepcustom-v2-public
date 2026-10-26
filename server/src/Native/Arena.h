#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Native/Entity.h"
#include "Native/FieldGroups.h"
#include "Physics/Vector.h"

class GameServer;
class ShapeManager;
class BossManager;
class TankBody;
class Client;
class TeamEntity;

enum class ArenaState { COUNTDOWN = -1, OPEN = 0, OVER = 1, CLOSING = 2, CLOSED = 3 };

// The arena entity: sent to the client, and used for internal calculations.
class ArenaEntity : public Entity {
public:
    ArenaData arenaData;
    TeamData teamData;

    double width;
    double height;
    ArenaState state = ArenaState::COUNTDOWN;
    double shapeScoreRewardMultiplier = 1;
    BossManager* bossManager;
    Ref<TankBody> leader;
    double ARENA_PADDING = 200;

    explicit ArenaEntity(GameServer* game);
    ~ArenaEntity() override;

    ArenaData* getArenaData() override { return &arenaData; }
    TeamData* getTeamData() override { return &teamData; }

    bool isOpen() const { return state == ArenaState::OPEN; }
    bool isCountingDown() const { return state == ArenaState::COUNTDOWN; }
    bool isGameOver() const { return state == ArenaState::OVER; }
    bool isClosing() const { return state == ArenaState::CLOSING; }
    bool isClosed() const { return state == ArenaState::CLOSED; }

    VectorAbstract findSpawnLocation(double width, double height);
    VectorAbstract findSpawnLocation() { return findSpawnLocation(width, height); }
    VectorAbstract findPlayerSpawnLocation();
    virtual bool isValidSpawnLocation(double x, double y) { return true; }

    virtual void manageCountdown();
    std::vector<TankBody*> getAlivePlayers();
    std::vector<TankBody*> getTeamPlayers(Entity* team);
    void updateBounds(double arenaWidth, double arenaHeight);
    virtual void spawnPlayer(TankBody* tank, Client* client);
    virtual void onClientRemoved(Client* client) { playerTeamMap.erase(client); }

    // Team each client plays on for its whole connection; team modes fill it in decideTeam.
    std::unordered_map<Client*, TeamEntity*> playerTeamMap;
    virtual TeamEntity* decideTeam(Client* client) { return nullptr; }

    // Party links: teams that hand out codes (0x6); empty in modes without parties.
    std::vector<TeamEntity*> partyTeams;
    // Raw party code of the team `client` will play on, or "" when the mode has none.
    std::string partyCode(Client* client);
    // Puts `client` on the team with this party code; false when no team has it.
    bool joinParty(Client* client, const std::string& code);
    bool attemptFactorySpawn(TankBody* tank);
    void close();
    virtual void onGameStarted();
    void tick(int tick) override;

    virtual void updateScoreboard(std::vector<TankBody*>& scoreboardPlayers);
    virtual void updateArenaState();

protected:
    ShapeManager* shapes;
};
