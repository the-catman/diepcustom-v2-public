#include "Recorder.h"

#include "Client.h"
#include "Const/Enums.h"
#include "Entity/Tank/TankBody.h"
#include "Game.h"
#include "Native/Camera.h"

namespace bots {

Recorder::Recorder() = default;
Recorder::~Recorder() = default;

void Recorder::attach(GameServer* g, const std::vector<TankBody*>& tanks) {
    game = g;
    games += 1;
    for (size_t i = 0; i < tanks.size() && i < names.size(); ++i) {
        if (tanks[i] && !names[i].empty()) tanks[i]->nameData.setName(names[i]);
    }
    for (int v = 0; v < (int)views.size(); ++v) {
        auto client = std::make_unique<Client>(game);
        // Out of the game's list, so the game doesn't tick it, time it out or count it as a player.
        game->clients.erase(client.get());
        int index = games;
        client->sendRaw = [this, index, v](const uint8_t* data, size_t len) {
            packets.push_back({index, v, game->tick, std::string((const char*)data, len)});
        };
        client->acceptClient();
        ClientCamera* cam = client->camera.get();
        CameraData& cd = cam->cameraData;
        cd.setPlayer(cam);
        cam->relationsData.setTeam(tanks[0]->relationsData.team.get());
        cd.setFlags(cd.flags | CameraFlags::usesCameraCoords);
        const ReplayView& view = views[v];
        TankBody* followed = view.follow >= 0 ? tanks[view.follow] : nullptr;
        cd.setFOV(followed ? followed->cameraEntity->cameraData.FOV : view.fov);
        cd.setCameraX(followed ? followed->positionData.x : 0);
        cd.setCameraY(followed ? followed->positionData.y : 0);
        clients.push_back(std::move(client));
    }
}

void Recorder::beforeTick(const std::vector<TankBody*>& tanks) {
    for (size_t v = 0; v < clients.size(); ++v) {
        int slot = views[v].follow;
        TankBody* t = slot >= 0 ? tanks[slot] : nullptr;
        if (!Entity::exists(t)) continue;
        CameraData& cd = clients[v]->camera->cameraData;
        cd.setCameraX(t->positionData.x);
        cd.setCameraY(t->positionData.y);
    }
}

void Recorder::release() {
    for (auto& c : clients) c->camera = nullptr;
}

void Recorder::finish() {
    clients.clear();
    game = nullptr;
}

}
