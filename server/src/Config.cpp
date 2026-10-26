#include "Config.h"
#include "Const/Enums.h"

#include <cstdlib>

namespace config {
    const std::string buildHash = "6f59094d60f98fafc14371671d3ff31ef4d75d9e";

    int serverPort = 8080;
    std::string host = "unknown";
    std::string mode = "development";

    const std::string apiLocation = "api";
    const std::string clientLocation = "../client/dist";
    std::string devPasswordHash;

    static uint32_t computeMagicNum(const std::string& build) {
        int64_t res = 0, seed = 1, timer = 0;
        for (int i = 0; i < 40; ++i) {
            int c = 0;
            char ch = build[i];
            if (ch >= '0' && ch <= '9') c = ch - '0';
            else if (ch >= 'a' && ch <= 'f') c = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') c = ch - 'A' + 10;
            res ^= ((int64_t)c << ((seed & 1) << 2)) << (timer << 3);
            res &= 0xffffffff;
            timer = (timer + 1) & 3;
            seed ^= (timer == 0) ? 1 : 0;
        }
        return (uint32_t)(res & 0xffffffff);
    }

    const uint32_t magicNum = computeMagicNum(buildHash);

    void init() {
        if (const char* p = std::getenv("PORT")) serverPort = std::atoi(p);
        if (const char* h = std::getenv("SERVER_INFO")) host = h;
        if (const char* m = std::getenv("NODE_ENV")) mode = m;
        if (const char* d = std::getenv("DEV_PASSWORD_HASH")) devPasswordHash = d;
        initLevelToScoreTable();
    }
}
