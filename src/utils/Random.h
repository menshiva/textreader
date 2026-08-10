#pragma once

#include <cstdint>
#include <random>

struct XorShift32 {
    uint32_t s;

    XorShift32() {
        std::random_device rd;
        s = rd() + 1; // avoid zero
    }

    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};
