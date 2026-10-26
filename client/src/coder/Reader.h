#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

class Reader {
public:
    size_t at = 0;

    Reader(const uint8_t* buf, size_t len) : buffer(buf), length(len) {}

    uint8_t u8() { need(1); return buffer[at++]; }

    uint32_t u32() {
        need(4);
        uint32_t v;
        std::memcpy(&v, buffer + at, 4);
        at += 4;
        return v;
    }

    uint32_t vu() {
        uint32_t out = 0;
        int i = 0;
        for (int n = 0; n < 5; ++n) {
            need(1);
            uint8_t b = buffer[at++];
            out |= (uint32_t)(b & 0x7f) << (i & 31);
            if (!(b & 0x80)) return out;
            i += 7;
        }
        throw std::range_error("varint too long");
    }

    int32_t vi() {
        uint32_t out = vu();
        return (int32_t)((0u - (out & 1)) ^ (out >> 1));
    }

    float vf() {
        uint32_t swapped = endianSwap((uint32_t)vi());
        float f;
        std::memcpy(&f, &swapped, 4);
        return f;
    }

    std::string stringNT() {
        size_t end = at;
        while (end < length && buffer[end] != 0) ++end;
        if (end >= length) throw std::range_error("missing null terminator in packet string");
        std::string s((const char*)(buffer + at), end - at);
        at = end + 1;
        return s;
    }

    float floatLE() {
        need(4);
        float f;
        std::memcpy(&f, buffer + at, 4);
        at += 4;
        return f;
    }

    bool done() const { return at >= length; }

private:
    const uint8_t* buffer;
    size_t length;

    void need(size_t n) const {
        if (at + n > length || at + n < at) throw std::range_error("packet read out of bounds");
    }

    static uint32_t endianSwap(uint32_t num) {
        return ((num & 0xff) << 24) | ((num & 0xff00) << 8) | ((num >> 8) & 0xff00) | ((num >> 24) & 0xff);
    }
};
