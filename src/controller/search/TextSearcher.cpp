#include "TextSearcher.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include "../file/FileReader.h"

namespace {
    using Searcher = std::boyer_moore_horspool_searcher<std::string::const_iterator>;

    struct Scan {
        FileReader& reader;
        const size_t carrySize; // most of a match that can sit in the previous chunk
        const Searcher& searcher;
        const std::stop_token& st;

        std::atomic<float>& progress;
        const uint64_t totalBytes;
        uint64_t doneBytes = 0;
    };

    // looks for a match that starts inside [fromBytes, toBytes)
    bool scanRange(Scan& s, const uint64_t fromBytes, const uint64_t toBytes, const bool wantLast, search::Result& outResult) {
        if (fromBytes >= toBytes)
            return false;

        // a match may be in two chunks -> the tail of the previous one is kept
        char seam[2 * (search::kMaxNeedleBytes - 1)];
        size_t carryBytes = 0;

        // the last match may start right before toBytes and extend past it
        const uint64_t readEndBytes = std::min(toBytes + s.carrySize, s.reader.getSizeBytes());

        s.reader.seek(fromBytes);

        uint64_t chunkStartBytes = fromBytes;
        while (chunkStartBytes < readEndBytes) {
            auto chunk = s.reader.next();
            if (chunk.empty())
                break;
            if (chunk.size() > readEndBytes - chunkStartBytes)
                chunk = chunk.first(readEndBytes - chunkStartBytes);

            // start in the previous chunk and end in the current
            if (carryBytes) {
                memcpy(seam + carryBytes, chunk.data(), std::min(s.carrySize, chunk.size()));

                const auto seamBegin = seam;
                const auto seamEnd = seamBegin + carryBytes + std::min(s.carrySize, chunk.size());
                for (auto p = seamBegin;;) {
                    const auto match = std::search(p, seamEnd, s.searcher);
                    if (match == seamEnd || static_cast<size_t>(match - seamBegin) >= carryBytes)
                        break;

                    const uint64_t offset = chunkStartBytes - carryBytes + static_cast<uint64_t>(match - seamBegin);
                    if (offset < toBytes) {
                        outResult = search::Result{true, offset};
                        if (!wantLast)
                            return true;
                    }
                    p = match + 1;
                }
            }

            // start inside this chunk
            {
                const auto chunkBegin = chunk.data();
                const auto chunkEnd = chunkBegin + chunk.size();
                for (auto p = chunkBegin;;) {
                    const auto match = std::search(p, chunkEnd, s.searcher);
                    if (match == chunkEnd)
                        break;

                    const uint64_t offset = chunkStartBytes + static_cast<uint64_t>(match - chunkBegin);
                    if (offset >= toBytes)
                        break;

                    outResult = search::Result{true, offset};
                    if (!wantLast)
                        return true;
                    p = match + 1;
                }
            }

            carryBytes = std::min(s.carrySize, chunk.size());
            memcpy(seam, chunk.data() + chunk.size() - carryBytes, carryBytes);

            chunkStartBytes += chunk.size();

            s.doneBytes += chunk.size();
            assert(s.totalBytes);
            s.progress.store(
                static_cast<float>(static_cast<double>(s.doneBytes) / static_cast<double>(s.totalBytes)),
                std::memory_order_relaxed
            );

            if (s.st.stop_requested())
                return true;
        }

        return outResult.found || s.reader.hasError();
    }
}

std::optional<std::string> search::find(
    FileReader& reader, const std::stop_token& st, std::atomic<float>& outProgress,
    const Request& request, Result& outResult
) {
    outResult = Result{};
    if (request.needle.empty())
        return "The search text is empty";
    if (request.needle.size() > kMaxNeedleBytes)
        return "The search text is too long";

    const uint64_t fileSizeBytes = reader.getSizeBytes();
    const uint64_t contentStartBytes = std::min(request.contentStartOffsetBytes, fileSizeBytes);
    const uint64_t endBytes = std::clamp(request.endOffsetBytes, contentStartBytes, fileSizeBytes);
    const uint64_t startBytes = std::clamp(request.startOffsetBytes, contentStartBytes, endBytes);

    const Searcher searcher(request.needle.begin(), request.needle.end());
    Scan scan{reader, request.needle.size() - 1, searcher, st, outProgress, endBytes - contentStartBytes};

    if (!request.backwards) {
        // forward scan
        if (!scanRange(scan, startBytes, endBytes, false, outResult))
            scanRange(scan, contentStartBytes, startBytes, false, outResult);
    }
    else {
        // backward scan
        const auto scanBackwards = [&scan, &outResult] (const uint64_t lo, uint64_t hi) {
            while (hi > lo) {
                const uint64_t windowStartBytes = hi - std::min(hi - lo, kBackwardWindowBytes);
                if (scanRange(scan, windowStartBytes, hi, true, outResult))
                    return true;
                hi = windowStartBytes;
            }
            return false;
        };
        if (!scanBackwards(contentStartBytes, startBytes))
            scanBackwards(startBytes, endBytes);
    }

    if (reader.hasError())
        return "Cannot read the file";
    return std::nullopt;
}
