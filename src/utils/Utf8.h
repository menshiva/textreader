#pragma once

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
