#pragma once

#include <cstdint>
#include <random>

namespace utils {
    // https://stackoverflow.com/questions/53886131/how-does-xorshift32-works
    struct XorShift32 {
        uint32_t state;

        XorShift32() {
            std::random_device rd;
            state = rd() | 1u; // avoid zero
        }

        uint32_t next() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
    };
}
