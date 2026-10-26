#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Entity;

class Writer {
public:
    virtual ~Writer() = default;

    Writer& u8(int val) {
        buffer.push_back((uint8_t)(val & 0xff));
        return *this;
    }
    Writer& u16(int val) {
        uint16_t v = (uint16_t)(val & 0xffff);
        appendRaw(&v, 2);
        return *this;
    }
    Writer& u32(uint32_t val) {
        appendRaw(&val, 4);
        return *this;
    }
    Writer& floatLE(float val) {
        appendRaw(&val, 4);
        return *this;
    }
    Writer& vf(double val) {
        float f = (float)val;
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return vi((int32_t)endianSwap(bits));
    }
    Writer& vu(int32_t val) {
        uint32_t u = (uint32_t)val;
        do {
            uint8_t part = (uint8_t)(u & 0x7f);
            u >>= 7;
            if (u) part |= 0x80;
            buffer.push_back(part);
        } while (u);
        return *this;
    }
    Writer& vi(int32_t val) {
        uint32_t sign = (uint32_t)(0 - (val < 0 ? 1 : 0));
        return vu((int32_t)(sign ^ ((uint32_t)val << 1)));
    }
    Writer& bytes(const std::vector<uint8_t>& b) {
        buffer.insert(buffer.end(), b.begin(), b.end());
        return *this;
    }
    Writer& bytes(const uint8_t* b, size_t n) {
        buffer.insert(buffer.end(), b, b + n);
        return *this;
    }
    Writer& float64Precision(double val) { return vi((int32_t)(val * 64)); }
    Writer& stringNT(const std::string& str) {
        buffer.insert(buffer.end(), str.begin(), str.end());
        buffer.push_back(0);
        return *this;
    }
    Writer& entid(Entity* entity);
    Writer& entidRaw(uint32_t hash, uint32_t id) {
        if (hash == 0) return u8(0);
        return vu((int32_t)hash).vu((int32_t)id);
    }

    size_t mark() const { return buffer.size(); }

    const uint8_t* data() const { return buffer.data(); }
    size_t size() const { return buffer.size(); }

    virtual void send() {}

protected:
    std::vector<uint8_t> buffer;

private:
    void appendRaw(const void* src, size_t n) {
        const uint8_t* p = (const uint8_t*)src;
        buffer.insert(buffer.end(), p, p + n);
    }
    static uint32_t endianSwap(uint32_t num) {
        return ((num & 0xff) << 24) | ((num & 0xff00) << 8) | ((num >> 8) & 0xff00) | ((num >> 24) & 0xff);
    }
};
