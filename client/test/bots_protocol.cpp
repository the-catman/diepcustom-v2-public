#include "BotsProtocol.h"

#include <cstdio>
#include <vector>

namespace {
    int failures = 0;

    void check(bool condition, const char* message) {
        if (condition) return;
        std::printf("FAIL: %s\n", message);
        ++failures;
    }

    std::vector<uint8_t> bytes(const Writer& w) { return std::vector<uint8_t>(w.data(), w.data() + w.size()); }

    // The packet's header, then a reader past it.
    Reader body(const std::vector<uint8_t>& packet, uint8_t header) {
        check(!packet.empty() && packet[0] == header, "header byte");
        return Reader(packet.data() + 1, packet.size() - 1);
    }

    void testGolden() {
        bots::Create create;
        create.deterministic = false;
        create.n0 = 1;
        create.n1 = 1;
        create.seats = {{0, 24, 0, ""}, {1, 52, 0, ""}};
        create.name = "me";
        Writer w;
        bots::encodeCreate(w, create);
        const std::vector<uint8_t> golden = {0x20, 0x00, 0x01, 0x01, 0x00, 0x18, 0x01, 0x34, 0x6d, 0x65, 0x00, 0x00};
        check(bytes(w) == golden, "1v1 create matches the golden bytes");
    }

    void testCreate() {
        bots::Create create;
        create.deterministic = true;
        create.n0 = 2;
        create.n1 = 2;
        create.seats = {{0, 24, 0, ""}, {0, 300, 0, ""}, {1, 52, 0, ""}, {200, 7, 0, ""}};
        create.name = "two words";
        create.build = bots::Build{7, 7, 7, 7, 5, 0, 0, 0};
        Writer w;
        bots::encodeCreate(w, create);
        std::vector<uint8_t> packet = bytes(w);
        Reader r = body(packet, bots::ServerBound::Create);
        bots::Create back = bots::decodeCreate(r);
        check(r.done(), "create fully consumed");
        check(back.deterministic && back.n0 == 2 && back.n1 == 2, "create flags and format");
        check(back.seats.size() == 4, "create seat count");
        for (size_t i = 0; i < back.seats.size() && i < 4; ++i) {
            check(back.seats[i].who == create.seats[i].who && back.seats[i].tank == create.seats[i].tank, "create seat");
        }
        check(back.name == "two words", "create name");
        check(back.build == create.build, "create build");
    }

    void testSmallServerBound() {
        Writer ready;
        bots::encodeReady(ready, {"guest", std::nullopt});
        std::vector<uint8_t> packet = bytes(ready);
        check(packet == std::vector<uint8_t>({0x21, 'g', 'u', 'e', 's', 't', 0, 0}), "ready bytes");
        Reader r = body(packet, bots::ServerBound::Ready);
        bots::Ready back = bots::decodeReady(r);
        check(back.name == "guest" && !back.build && r.done(), "ready decoded");

        Writer built;
        bots::encodeReady(built, {"g", bots::Build{1, 2, 3, 4, 5, 6, 7, 0}});
        check(bytes(built) == std::vector<uint8_t>({0x21, 'g', 0, 1, 1, 2, 3, 4, 5, 6, 7, 0}), "ready with a build");

        Writer rematch, leave;
        bots::encodeRematch(rematch);
        bots::encodeLeave(leave);
        check(bytes(rematch) == std::vector<uint8_t>({0x22}), "rematch is its header alone");
        check(bytes(leave) == std::vector<uint8_t>({0x23}), "leave is its header alone");
    }

    void testLists() {
        bots::Lists lists;
        lists.online = true;
        lists.bots = {{"Team", {24, 52, 130}}, {"Old", {}}};
        for (int t = 0; t < 56; ++t) {
            lists.playerTanks.push_back(t);
            lists.playerBuilds.push_back({t % 8, 7, 7, 7, 5, 0, 0, 0});
        }
        Writer w;
        bots::encodeLists(w, lists);
        std::vector<uint8_t> packet = bytes(w);
        Reader r = body(packet, bots::ClientBound::Lists);
        bots::Lists back = bots::decodeLists(r);
        check(r.done(), "lists fully consumed");
        check(back.online, "lists online flag");
        check(back.bots.size() == 2 && back.bots[0].name == "Team" && back.bots[1].name == "Old", "lists bot names");
        check(back.bots.size() == 2 && back.bots[0].tanks == lists.bots[0].tanks && back.bots[1].tanks.empty(), "lists bot tanks");
        check(back.playerTanks == lists.playerTanks, "lists player tanks");
        check(back.playerBuilds == lists.playerBuilds, "lists player builds");

        Writer offline;
        bots::encodeLists(offline, bots::Lists{});
        check(bytes(offline) == std::vector<uint8_t>({0x20, 0x00, 0x00, 0x00}), "offline lists bytes");
    }

    void testRoom() {
        bots::Room room;
        room.phase = bots::Waiting;
        room.deterministic = true;
        room.n0 = 2;
        room.n1 = 1;
        room.you = 1;
        room.seats = {{0, 24, bots::SeatState::filled | bots::SeatState::ready, "me"},
                      {0, 52, bots::SeatState::filled, ""},
                      {3, 7, bots::SeatState::filled | bots::SeatState::ready | bots::SeatState::alive, ""}};
        room.ticks = 1500;
        Writer w;
        bots::encodeRoom(w, room);
        std::vector<uint8_t> packet = bytes(w);
        Reader r = body(packet, bots::ClientBound::Room);
        bots::Room back = bots::decodeRoom(r);
        check(r.done(), "room fully consumed");
        check(back.phase == bots::Waiting && back.deterministic && back.n0 == 2 && back.n1 == 1 && back.you == 1, "room header");
        check(back.seats.size() == 3, "room seat count");
        for (size_t i = 0; i < back.seats.size() && i < 3; ++i) {
            const bots::Seat& a = back.seats[i];
            const bots::Seat& b = room.seats[i];
            check(a.who == b.who && a.tank == b.tank && a.state == b.state && a.name == b.name, "room seat");
        }
        check(back.ticks == 1500, "room ticks");

        Writer none;
        bots::encodeRoom(none, bots::Room{});
        check(bytes(none) == std::vector<uint8_t>({0x21, 0, 0, 0, 0, 0xff, 0}), "no room bytes");
    }

    void testResult() {
        bots::Result res;
        res.winner = bots::DRAW;
        res.reason = bots::BotsOffline;
        res.ticks = 4321;
        res.seats = {{2, 123456}, {0, 0}, {1, 99}, {0, 7}};
        Writer w;
        bots::encodeResult(w, res);
        std::vector<uint8_t> packet = bytes(w);
        Reader r = body(packet, bots::ClientBound::Result);
        bots::Result back = bots::decodeResult(r);
        check(r.done(), "result fully consumed");
        check(back.winner == bots::DRAW && back.reason == bots::BotsOffline && back.ticks == 4321, "result header");
        check(back.seats.size() == 4, "result seat count");
        for (size_t i = 0; i < back.seats.size() && i < 4; ++i) {
            check(back.seats[i].kills == res.seats[i].kills && back.seats[i].dealt == res.seats[i].dealt, "result seat");
        }
    }
}

int main() {
    testGolden();
    testCreate();
    testSmallServerBound();
    testLists();
    testRoom();
    testResult();
    if (failures) {
        std::printf("%d bots protocol failure(s)\n", failures);
        return 1;
    }
    std::puts("bots protocol tests OK");
    return 0;
}
