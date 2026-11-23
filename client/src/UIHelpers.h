#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Shared by the UI passes (GameUI.cpp, BotsUI.cpp).

inline float approach(float value, float target, float dtMs, float rate) {
    float k = 1.0f - std::pow(1.0f - rate, dtMs / 16.6667f);
    return value + (target - value) * k;
}

inline double clamp01(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

// Truncated to an int. The fades settle just under 1, so a resting panel sits at
// int(H/2) - 1.
inline double slideAnchor(float fade, int h, double ui) {
    float u200 = (float)(ui * 200.0);
    return (double)(int)(fade * ((float)(h / 2) + u200) - u200);
}

inline uint32_t blendChannel3(uint32_t rgb, float mul, float add) {
    auto c = [&](int shift) {
        float v = (float)((rgb >> shift) & 0xff) * mul + add;
        return (uint32_t)std::min(255.0f, std::max(0.0f, v)) << shift;
    };
    return c(16) | c(8) | c(0);
}

inline uint32_t pastel(uint32_t rgb) { return blendChannel3(rgb, 0.6f, 102.0f); }

inline std::string formatCommas(long long v) {
    std::string s = std::to_string(v);
    for (int i = (int)s.size() - 3; i > (v < 0 ? 1 : 0); i -= 3) s.insert((size_t)i, ",");
    return s;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1);
}
