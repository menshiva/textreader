#include "TextGenerator.h"
#include "../../utils/Random.h"
#include "../file/FileWriter.h"

std::optional<std::string> textgen::generate(
    FileWriter& writer, const std::stop_token& st, std::atomic<float>& outProgress,
    const uint64_t targetBytes, const uint64_t targetLines
) {
    constexpr static char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .";
    constexpr static uint32_t alphabetLen = sizeof(kAlphabet) - 1;

    constexpr static uint32_t kMaxLineLen = 1024;
    constexpr static size_t kPoolSize = 64ull << 10; // pre-made random characters - 64 kb

    utils::XorShift32 rnd;

    // generate random chars
    std::vector<char> pool(kPoolSize);
    for (size_t i = 0; i < kPoolSize; ++i)
        pool[i] = kAlphabet[rnd.next() % alphabetLen];

    const auto begin = writer.getBuffer();
    const auto end = begin + writer.getBufferSize();
    auto p = begin;

    bool writeFailed = false;

    const auto flushBuffer = [&] (const float percent) {
        if (!writer.writeBuffer())
            return false;
        outProgress.store(percent, std::memory_order_relaxed);
        p = begin;
        return true;
    };
    // appends "left" characters with CRLF and flushing buffer if needed
    const auto appendLine = [&] (uint32_t left, const float percent) {
        while (left > 0) {
            if (p == end && !flushBuffer(percent))
                return false;
            // copy random chunk from pool
            const uint32_t chunk = std::min<uint32_t>(static_cast<uint32_t>(end - p), left);
            memcpy(p, pool.data() + rnd.next() % (kPoolSize - kMaxLineLen), chunk);
            p += chunk;
            left -= chunk;
        }
        if (p == end && !flushBuffer(percent))
            return false;
        *p++ = '\r';
        if (p == end && !flushBuffer(percent))
            return false;
        *p++ = '\n';
        return true;
    };

    if (targetBytes) {
        const auto targetBytesFlt = static_cast<float>(targetBytes);
        uint64_t remaining = targetBytes;
        while (remaining >= 2) { // 2 = CRLF
            if (st.stop_requested())
                break;

            const auto maxLen = static_cast<uint32_t>(std::min<uint64_t>(remaining - 2, kMaxLineLen - 1));
            uint32_t len = rnd.next() % kMaxLineLen;
            if (len > maxLen)
                len = maxLen;
            if (remaining - 2 - len == 1)
                ++len; // force append single leftover byte

            if (!appendLine(len, static_cast<float>(targetBytes - remaining) / targetBytesFlt)) {
                writeFailed = true;
                break;
            }
            remaining -= len + 2;
        }
    }
    else {
        const auto targetLinesFlt = static_cast<float>(targetLines);
        for (uint64_t n = 0; n < targetLines; ++n) {
            if (st.stop_requested())
                break;
            if (!appendLine(rnd.next() % kMaxLineLen, static_cast<float>(n) / targetLinesFlt)) {
                writeFailed = true;
                break;
            }
        }
    }

    if (!writer.finish(writeFailed ? 0 : p - begin))
        writeFailed = true;

    if (writeFailed)
        return "Cannot write the generated file (out of disk space?)";

    outProgress.store(1.0f, std::memory_order_relaxed);
    return std::nullopt;
}
