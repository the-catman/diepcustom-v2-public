#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coder/Reader.h"
#include "coder/Writer.h"

// Packets of the play-against-the-bots mode (gamemode "bots"). Seats run team 0's, then team
// 1's; a seat's who is 0 for a player and k for bot k - 1 of the list.
namespace bots {
    namespace ServerBound {
        constexpr uint8_t Create = 0x20;
        constexpr uint8_t Ready = 0x21;
        constexpr uint8_t Rematch = 0x22;
        constexpr uint8_t Leave = 0x23;
    }

    namespace ClientBound {
        constexpr uint8_t Lists = 0x20;
        constexpr uint8_t Room = 0x21;
        constexpr uint8_t Result = 0x22;
    }

    enum Phase { None = 0, Waiting = 1, Countdown = 2, Playing = 3, Over = 4 };

    namespace SeatState {
        constexpr int filled = 1;
        constexpr int ready = 2;
        constexpr int alive = 4;
        constexpr int wantsRematch = 8;
    }

    enum Reason { Eliminated = 0, TimedOut = 1, SomeoneLeft = 2, BotsOffline = 3 };

    constexpr int NO_SEAT = 0xff;
    constexpr int DRAW = 0xff;
    constexpr int MAX_SEATS = 8;
    constexpr int TICKS_PER_SECOND = 25;
    // Stat points a max level tank has.
    constexpr int BUILD_POINTS = 33;

    // A tank's stat levels in server order, movement speed first.
    using Build = std::array<int, 8>;

    struct Seat {
        int who = 0;
        int tank = 0;
        int state = 0;
        std::string name;
    };

    struct Create {
        bool deterministic = false;
        int n0 = 1, n1 = 1;
        std::vector<Seat> seats; // who and tank
        std::string name;
        // The creator's own build, if not their tank's usual one.
        std::optional<Build> build;
    };

    struct Ready {
        std::string name;
        std::optional<Build> build;
    };

    struct Lists {
        struct Bot {
            std::string name;
            std::vector<int> tanks;
        };
        bool online = false;
        std::vector<Bot> bots;
        std::vector<int> playerTanks;
        // Each player tank's usual build.
        std::vector<Build> playerBuilds;
    };

    struct Room {
        int phase = None;
        bool deterministic = false;
        int n0 = 0, n1 = 0;
        int you = NO_SEAT;
        std::vector<Seat> seats;
        // Left in the countdown, elapsed while playing.
        uint32_t ticks = 0;
    };

    struct Result {
        struct Stat {
            uint32_t kills = 0;
            uint32_t dealt = 0; // damage x 100
        };
        int winner = DRAW;
        int reason = Eliminated;
        uint32_t ticks = 0;
        std::vector<Stat> seats;
    };

    // A 1 and the build, or a 0 for the tank's usual one.
    inline void encodeBuild(Writer& w, const std::optional<Build>& b) {
        w.u8(b ? 1 : 0);
        if (b) for (int x : *b) w.u8((uint8_t)x);
    }

    inline std::optional<Build> decodeBuild(Reader& r) {
        if (!(r.u8() & 1)) return std::nullopt;
        Build b;
        for (int& x : b) x = r.u8();
        return b;
    }

    inline void encodeCreate(Writer& w, const Create& c) {
        w.u8(ServerBound::Create).u8(c.deterministic ? 1 : 0).u8(c.n0).u8(c.n1);
        for (const Seat& s : c.seats) w.vu((uint32_t)s.who).vu((uint32_t)s.tank);
        w.stringNT(c.name);
        encodeBuild(w, c.build);
    }

    inline void encodeReady(Writer& w, const Ready& ready) {
        w.u8(ServerBound::Ready).stringNT(ready.name);
        encodeBuild(w, ready.build);
    }
    inline void encodeRematch(Writer& w) { w.u8(ServerBound::Rematch); }
    inline void encodeLeave(Writer& w) { w.u8(ServerBound::Leave); }

    // The decoders start after the header byte.
    inline Create decodeCreate(Reader& r) {
        Create c;
        c.deterministic = r.u8() & 1;
        c.n0 = r.u8();
        c.n1 = r.u8();
        for (int i = 0; i < c.n0 + c.n1; ++i) {
            Seat s;
            s.who = (int)r.vu();
            s.tank = (int)r.vu();
            c.seats.push_back(s);
        }
        c.name = r.stringNT();
        c.build = decodeBuild(r);
        return c;
    }

    inline Ready decodeReady(Reader& r) {
        Ready ready;
        ready.name = r.stringNT();
        ready.build = decodeBuild(r);
        return ready;
    }

    inline void encodeLists(Writer& w, const Lists& l) {
        w.u8(ClientBound::Lists).u8(l.online ? 1 : 0).vu((uint32_t)l.bots.size());
        for (const Lists::Bot& b : l.bots) {
            w.stringNT(b.name).vu((uint32_t)b.tanks.size());
            for (int t : b.tanks) w.vu((uint32_t)t);
        }
        w.vu((uint32_t)l.playerTanks.size());
        for (size_t i = 0; i < l.playerTanks.size(); ++i) {
            w.vu((uint32_t)l.playerTanks[i]);
            for (int x : i < l.playerBuilds.size() ? l.playerBuilds[i] : Build{}) w.u8((uint8_t)x);
        }
    }

    inline Lists decodeLists(Reader& r) {
        Lists l;
        l.online = r.u8() & 1;
        for (uint32_t n = r.vu(); n > 0; --n) {
            Lists::Bot b;
            b.name = r.stringNT();
            for (uint32_t k = r.vu(); k > 0; --k) b.tanks.push_back((int)r.vu());
            l.bots.push_back(std::move(b));
        }
        for (uint32_t k = r.vu(); k > 0; --k) {
            l.playerTanks.push_back((int)r.vu());
            Build b;
            for (int& x : b) x = r.u8();
            l.playerBuilds.push_back(b);
        }
        return l;
    }

    inline void encodeRoom(Writer& w, const Room& room) {
        w.u8(ClientBound::Room).u8(room.phase).u8(room.deterministic ? 1 : 0).u8(room.n0).u8(room.n1).u8(room.you);
        for (const Seat& s : room.seats) w.vu((uint32_t)s.who).vu((uint32_t)s.tank).u8(s.state).stringNT(s.name);
        w.vu(room.ticks);
    }

    inline Room decodeRoom(Reader& r) {
        Room room;
        room.phase = r.u8();
        room.deterministic = r.u8() & 1;
        room.n0 = r.u8();
        room.n1 = r.u8();
        room.you = r.u8();
        for (int i = 0; i < room.n0 + room.n1; ++i) {
            Seat s;
            s.who = (int)r.vu();
            s.tank = (int)r.vu();
            s.state = r.u8();
            s.name = r.stringNT();
            room.seats.push_back(std::move(s));
        }
        room.ticks = r.vu();
        return room;
    }

    inline void encodeResult(Writer& w, const Result& res) {
        w.u8(ClientBound::Result).u8(res.winner).u8(res.reason).vu(res.ticks);
        for (const Result::Stat& s : res.seats) w.vu(s.kills).vu(s.dealt);
    }

    // One entry per seat of the room, up to the end of the packet.
    inline Result decodeResult(Reader& r) {
        Result res;
        res.winner = r.u8();
        res.reason = r.u8();
        res.ticks = r.vu();
        while (!r.done() && (int)res.seats.size() < MAX_SEATS) {
            Result::Stat s;
            s.kills = r.vu();
            s.dealt = r.vu();
            res.seats.push_back(s);
        }
        return res;
    }
}
