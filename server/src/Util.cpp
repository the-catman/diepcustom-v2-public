#include "Util.h"
#include "Config.h"
#include "Entity/Object.h"

#include <cstdlib>
#include <chrono>
#include <random>

namespace util {

static uint64_t initialSeed() {
    if (const char* s = std::getenv("DIEP_SEED")) return std::strtoull(s, nullptr, 10); // dev: replayable runs
    return std::random_device{}() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
}

double randomFloat() {
    static thread_local std::mt19937_64 gen(initialSeed());
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

std::string truncateUtf8(const std::string& s, size_t maxUnits) {
    // Cuts like JS .slice(0, maxUnits): UTF-16 code units, a non-BMP codepoint counting 2.
    size_t units = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        size_t u = len == 4 ? 2 : 1;
        if (units + u > maxUnits) break;
        units += u;
        i += len;
    }
    return s.substr(0, i);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1);
}

VectorAbstract getRandomPosition(ObjectEntity* entity) {
    Vector pos = entity->getWorldPosition();

    bool isRect = entity->physicsData.sides == 2;
    if (isRect) {
        pos.x += (randomFloat() - 0.5) * entity->physicsData.size;
        pos.y += (randomFloat() - 0.5) * entity->physicsData.width;
    } else {
        double radius = std::sqrt(randomFloat()) * entity->physicsData.size;
        double angle = randomFloat() * PI2;
        pos.x += std::cos(angle) * radius;
        pos.y += std::sin(angle) * radius;
    }
    return pos;
}

void saveToLog(const std::string& title, const std::string& description, uint32_t color) {
    (void)color;
    std::cout << "[!] " << title << "\n :: " << description << std::endl;
}

void saveToVLog(const std::string& text) {
    if (config::doVerboseLogs) std::cout << "[v] " << text << std::endl;
}

}
