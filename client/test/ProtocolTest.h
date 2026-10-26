#pragma once

#include <cstdint>
#include <vector>

struct InjectedPacket {
    int tick;
    std::vector<uint8_t> data;
};

struct ProtocolFixtures {
    std::vector<uint8_t> creationPacket;
    std::vector<uint8_t> updatePacket;
};

std::vector<std::vector<uint8_t>> runServerSession(const std::vector<InjectedPacket>& injected, int ticks);
ProtocolFixtures compileProtocolFixtures();
