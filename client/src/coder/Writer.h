#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Writer {
public:
    Writer& u8(int val) { buffer.push_back((uint8_t)(val & 0xff)); return *this; }
    Writer& vu(uint32_t val) {
        do {
            uint8_t part = (uint8_t)(val & 0x7f);
            val >>= 7;
            if (val) part |= 0x80;
            buffer.push_back(part);
        } while (val);
        return *this;
    }
    Writer& vi(int32_t val) {
        uint32_t sign = (uint32_t)(0 - (val < 0 ? 1 : 0));
        return vu(sign ^ ((uint32_t)val << 1));
    }
    Writer& vf(double val) {
        float f = (float)val;
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return vi((int32_t)endianSwap(bits));
    }
    Writer& stringNT(const std::string& str) {
        buffer.insert(buffer.end(), str.begin(), str.end());
        buffer.push_back(0);
        return *this;
    }

    const uint8_t* data() const { return buffer.data(); }
    size_t size() const { return buffer.size(); }

private:
    std::vector<uint8_t> buffer;

    static uint32_t endianSwap(uint32_t num) {
        return ((num & 0xff) << 24) | ((num & 0xff00) << 8) | ((num >> 8) & 0xff00) | ((num >> 24) & 0xff);
    }
};
