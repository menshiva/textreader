#pragma once

#include <cstdint>
#include <cstring>

namespace utf8 {
    inline bool isContinuationByte(const unsigned char c) {
        // utf-8 always has 10xxxxxx
        return c >> 6 == 2;
    }

    inline uint32_t seqLen(const unsigned char c) {
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0) return 2;
            if ((c & 0xF0) == 0xE0) return 3;
            if ((c & 0xF8) == 0xF0) return 4;
        }
        return 1;
    }

    inline uint64_t countCodepoints(const char* dataBegin, const char* dataEnd) {
        // a codepoint is one byte that is not a continuation byte
        uint64_t res = 0;

        // eight bytes at a time: a continuation byte has bit 7 set and bit 6 clear, so the mask leaves
        // 0x01 in every byte that is NOT one, and a single multiply sums those lanes into the top byte
        // taken from: https://doc.rust-lang.org/src/core/str/count.rs.html
        while (dataEnd - dataBegin >= 8) {
            uint64_t w;
            std::memcpy(&w, dataBegin, 8);
            const uint64_t leads = ((~w >> 7) | (w >> 6)) & 0x0101010101010101ull;
            res += (leads * 0x0101010101010101ull) >> 56;
            dataBegin += 8;
        }

        while (dataBegin < dataEnd)
            res += static_cast<uint64_t>(!isContinuationByte(static_cast<unsigned char>(*dataBegin++)));
        return res;
    }

    inline const char* alignToBoundary(const char* begin, const char* p) {
        // walks back to the start of a codepoint (used when a byte limit cut a sequence in half)
        while (p > begin && isContinuationByte(static_cast<unsigned char>(*p)))
            --p;
        return p;
    }
}
