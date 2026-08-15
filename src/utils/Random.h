#pragma once

#include <random>

namespace utils {
    // https://stackoverflow.com/questions/53886131/how-does-xorshift32-works
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
}
