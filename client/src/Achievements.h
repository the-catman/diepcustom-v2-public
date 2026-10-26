#pragma once

#include <string>
#include <vector>

// Client half of the achievements: the definitions (server/data/Achievements.json), one
// localStorage counter per condition ("A::<key>"), and the two ways a counter advances.
namespace achievements {
    int count();
    const char* name(int i);
    const char* desc(int i);
    bool unlocked(int i);
    // Counters with a threshold above 1 show a percentage while locked.
    bool hasProgress(int i);
    double progress(int i);
    // 0x8: the key of each condition a server-side event matched, once per match.
    void onKeys(const std::vector<std::string>& keys);
    // The one client-side event, fed the averaged latency.
    void onLatency(double ms);
}
