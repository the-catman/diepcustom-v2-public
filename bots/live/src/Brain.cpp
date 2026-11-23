#include "Brain.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "Util.h"

namespace live {

namespace {

// Ticks without an answer before the bots count as offline.
constexpr int SILENT_TICKS = 25;
// A brain this far behind is dropped rather than buffered for.
constexpr size_t MAX_BACKLOG = 64 << 20;

template <class T>
void put(std::vector<uint8_t>& out, T v) {
    uint8_t b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    out.insert(out.end(), b, b + sizeof(T));
}

struct Reader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    template <class T>
    T get() {
        T v{};
        if (left < sizeof(T)) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        left -= sizeof(T);
        return v;
    }
    std::string str(size_t n) {
        if (left < n) {
            ok = false;
            return {};
        }
        std::string s((const char*)p, n);
        p += n;
        left -= n;
        return s;
    }
};

}

BrainBots::BrainBots(const std::string& socketPath) : path(socketPath) {
    listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd < 0) throw std::runtime_error("brain socket: " + std::string(std::strerror(errno)));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) throw std::runtime_error("brain socket path too long: " + path);
    std::strcpy(addr.sun_path, path.c_str());
    unlink(path.c_str());
    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(listenFd, 1) < 0) {
        throw std::runtime_error("brain socket " + path + ": " + std::strerror(errno));
    }
}

BrainBots::~BrainBots() {
    if (fd >= 0) close(fd);
    if (listenFd >= 0) close(listenFd);
    unlink(path.c_str());
}

void BrainBots::accept() {
    int c = ::accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (c < 0) return;
    if (fd >= 0) {
        // One brain at a time; the newest takes over.
        drop();
    }
    fd = c;
    int size = 8 << 20;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    util::log("Brain connected");
}

void BrainBots::drop() {
    if (fd >= 0) {
        close(fd);
        util::log("Brain disconnected");
    }
    fd = -1;
    hello = false;
    in.clear();
    out.clear();
    pending.clear();
    sent.clear();
    waited = 0;
    botLists.bots.clear();
    botLists.playerTanks.clear();
    botLists.version += 1;
}

bool BrainBots::readSome() {
    uint8_t buf[1 << 16];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r > 0) {
            in.insert(in.end(), buf, buf + r);
            continue;
        }
        if (r == 0) return false;
        if (errno == EINTR) continue;
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}

bool BrainBots::writeSome() {
    size_t done = 0;
    while (done < out.size()) {
        ssize_t w = write(fd, out.data() + done, out.size() - done);
        if (w > 0) {
            done += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        return false;
    }
    out.erase(out.begin(), out.begin() + (long)done);
    return out.size() < MAX_BACKLOG;
}

void BrainBots::poll() {
    if (fd < 0 || !hello) accept();
    if (fd < 0) return;
    if (!readSome() || !writeSome()) return drop();
    size_t at = 0;
    while (in.size() - at >= 5) {
        uint32_t len;
        std::memcpy(&len, in.data() + at, 4);
        if (in.size() - at - 5 < len) break;
        handle(in[at + 4], in.data() + at + 5, len);
        if (fd < 0) return;
        at += 5 + len;
    }
    in.erase(in.begin(), in.begin() + (long)at);
    if (!sent.empty()) waited += 1;
}

void BrainBots::handle(uint8_t type, const uint8_t* body, size_t len) {
    if (type == frame::Hello) onHello(body, len);
    else if (type == frame::Act) onAct(body, len);
}

void BrainBots::onHello(const uint8_t* body, size_t len) {
    Reader r{body, len};
    uint32_t version = r.get<uint32_t>();
    int selfDim = r.get<uint16_t>(), entDim = r.get<uint16_t>();
    if (version != frame::Version || selfDim != bots::SELF_DIM || entDim != bots::ENT_DIM) {
        util::log("Brain speaks another version or shape; dropped");
        return drop();
    }
    Lists lists;
    auto tanks = [&](std::vector<std::array<int, 9>>& to) {
        int n = r.get<uint8_t>();
        for (int i = 0; i < n && r.ok; ++i) {
            std::array<int, 9> t{};
            for (int& x : t) x = r.get<uint8_t>();
            to.push_back(t);
        }
    };
    int bots = r.get<uint8_t>();
    for (int i = 0; i < bots && r.ok; ++i) {
        Lists::Bot b;
        b.name = r.str(r.get<uint8_t>());
        uint8_t flags = r.get<uint8_t>();
        b.remembers = flags & botflag::Remembers;
        tanks(b.tanks);
        lists.bots.push_back(std::move(b));
    }
    tanks(lists.playerTanks);
    if (!r.ok) {
        util::log("Brain's hello was cut short; dropped");
        return drop();
    }
    lists.version = botLists.version + 1;
    botLists = std::move(lists);
    hello = true;
    util::log("Brain offers ", botLists.bots.size(), " bots");
}

void BrainBots::onAct(const uint8_t* body, size_t len) {
    Reader r{body, len};
    uint32_t got = r.get<uint32_t>();
    int rows = r.get<uint16_t>();
    // Answers come in order; anything older was skipped.
    while (!sent.empty() && sent.front().seq != got) sent.pop_front();
    if (sent.empty()) return;
    Sent s = std::move(sent.front());
    sent.pop_front();
    waited = 0;
    for (int i = 0; i < rows && i < (int)s.rows.size() && r.ok; ++i) {
        bots::Action a;
        a.move = r.get<uint8_t>();
        a.fire = r.get<uint8_t>() != 0;
        a.repel = r.get<uint8_t>() != 0;
        r.get<uint8_t>();
        a.target = r.get<int16_t>();
        r.get<uint16_t>();
        a.aim = r.get<float>();
        a.reach = r.get<float>();
        if (!r.ok) break;
        a.move = std::clamp(a.move, 0, bots::MOVES - 1);
        auto m = matches.find(s.rows[i].first);
        if (m != matches.end()) m->second->deliver(s.rows[i].second, a, got);
    }
}

bool BrainBots::online() const { return fd >= 0 && hello && waited < SILENT_TICKS; }

uint32_t BrainBots::queue(uint32_t match, int slot, int bot, int tank, uint8_t flags, const float* self,
                          const float* ents, const uint8_t* mask, const uint8_t* targetable, int rows) {
    if (fd < 0 || !hello) return seq + 1;
    Row row{match, (uint8_t)slot, (uint8_t)bot, (uint8_t)tank, flags, rows, {}, {}, {}, {}};
    row.self.assign(self, self + bots::SELF_DIM);
    row.ents.assign(ents, ents + (size_t)rows * bots::ENT_DIM);
    row.mask.assign(mask, mask + rows);
    row.targetable.assign(targetable, targetable + rows);
    pending.push_back(std::move(row));
    return seq + 1;
}

void BrainBots::flush() {
    if (fd < 0 || !hello || pending.empty()) {
        pending.clear();
        if (fd >= 0 && !writeSome()) drop();
        return;
    }
    seq += 1;
    int width = 1;
    for (const Row& r : pending) width = std::max(width, r.rows);
    std::vector<uint8_t> body;
    size_t n = pending.size();
    body.reserve(8 + n * (8 + 4 * bots::SELF_DIM + (size_t)width * (4 * bots::ENT_DIM + 2)));
    put<uint32_t>(body, seq);
    put<uint16_t>(body, (uint16_t)n);
    put<uint16_t>(body, (uint16_t)width);
    Sent s{seq, {}};
    for (const Row& r : pending) {
        put<uint32_t>(body, r.match);
        body.insert(body.end(), {r.slot, r.bot, r.tank, r.flags});
        s.rows.push_back({r.match, r.slot});
    }
    auto bytes = [&](const void* p, size_t len) {
        const uint8_t* b = (const uint8_t*)p;
        body.insert(body.end(), b, b + len);
    };
    for (const Row& r : pending) bytes(r.self.data(), r.self.size() * 4);
    std::vector<float> zero((size_t)width * bots::ENT_DIM, 0.0f);
    for (const Row& r : pending) {
        bytes(r.ents.data(), r.ents.size() * 4);
        bytes(zero.data(), (size_t)(width - r.rows) * bots::ENT_DIM * 4);
    }
    std::vector<uint8_t> none(width, 0);
    for (const Row& r : pending) {
        bytes(r.mask.data(), r.mask.size());
        bytes(none.data(), width - r.rows);
    }
    for (const Row& r : pending) {
        bytes(r.targetable.data(), r.targetable.size());
        bytes(none.data(), width - r.rows);
    }
    pending.clear();
    put<uint32_t>(out, (uint32_t)body.size());
    out.push_back(frame::Obs);
    out.insert(out.end(), body.begin(), body.end());
    if (sent.empty()) waited = 0;
    sent.push_back(std::move(s));
    if (!writeSome()) drop();
}

bool BrainBots::waitForBrain(int ms) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!(fd >= 0 && hello)) {
        if (std::chrono::steady_clock::now() > until) return false;
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

bool BrainBots::sync() {
    flush();
    while (!sent.empty()) {
        if (fd < 0) return false;
        pollfd p{fd, (short)(POLLIN | (out.empty() ? 0 : POLLOUT)), 0};
        ::poll(&p, 1, 1000);
        int before = waited;
        poll();
        waited = before;
    }
    return fd >= 0;
}

uint32_t BrainBots::addMatch(LiveMatch* match) {
    uint32_t id = nextMatch++;
    matches[id] = match;
    return id;
}

void BrainBots::forget(uint32_t match) { matches.erase(match); }

std::unique_ptr<Match> BrainBots::start(const Spec& spec) {
    if (!online()) return nullptr;
    int n = spec.n0 + spec.n1;
    if (spec.n0 < 1 || spec.n1 < 1 || n > bots::MAX_PLAYERS || (int)spec.seats.size() != n) return nullptr;
    bots::Config c;
    c.players = n;
    bool teams = n > 2, player = false;
    for (int i = 0; i < n; ++i) {
        const Seat& seat = spec.seats[i];
        const std::vector<std::array<int, 9>>* tanks = &botLists.playerTanks;
        if (seat.bot >= 0) {
            if (seat.bot >= (int)botLists.bots.size()) return nullptr;
            const Lists::Bot& b = botLists.bots[seat.bot];
            tanks = &b.tanks;
            c.remember[i] = b.remembers;
        } else if (!seat.client) {
            return nullptr;
        } else {
            player = true;
        }
        const std::array<int, 9>* pick = nullptr;
        for (const auto& t : *tanks)
            if (t[0] == seat.tank) pick = &t;
        if (!pick) return nullptr;
        c.tanks[i] = seat.tank;
        for (int k = 0; k < 8; ++k) c.stats[i][k] = seat.bot < 0 && seat.build ? (*seat.build)[k] : (*pick)[1 + k];
        c.kinds[i] = seat.bot >= 0 ? bots::External : bots::Human;
        c.teams[i] = teams ? (i < spec.n0 ? 0 : 1) : -1;
        c.learner[i] = true;
    }
    // As the bots trained: duels on a 4000 arena, mirrored 1000 to 3000 apart; teams on a 6000 arena, 2000 to 4500
    // apart, teammates 300 to 600. With a player the sides start out of sight: duels 2800 to 3400 apart, teams 3500 to
    // 4500.
    c.symmetry = true;
    c.endless = true;
    if (teams) {
        c.arenaSize = 6000;
        c.maxDecisions = 900;
        c.spawnMin = player ? 3500 : 2000;
        c.spawnMax = 4500;
        c.mateMin = 300;
        c.mateMax = 600;
    } else {
        c.arenaSize = 4000;
        c.maxDecisions = 375;
        c.spawnMin = player ? 2800 : 1000;
        c.spawnMax = player ? 3400 : 3000;
        c.mirrorSpawn = true;
    }
    return std::make_unique<LiveMatch>(*this, spec, c);
}

std::unique_ptr<Bots> connectBrain(const std::string& socketPath) { return std::make_unique<BrainBots>(socketPath); }

}
