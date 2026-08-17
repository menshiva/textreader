#pragma once

#include <optional>
#include <stop_token>
#include <string>

class FileReader;

namespace search {
    struct Request {
        std::string needle;
        bool backwards = false;
        uint64_t startOffsetBytes = 0; // forward: the first offset that may match; backwards: matches have to start before it
        uint64_t contentStartOffsetBytes = 0;
        uint64_t endOffsetBytes = 0;
    };

    struct Result {
        bool found = false;
        uint64_t offsetBytes = 0;
    };

    std::optional<std::string> find(
        FileReader& reader, const std::stop_token& st, std::atomic<float>& outProgress,
        const Request& request, Result& outResult
    );

    constexpr size_t kReadBufferSize = 4ull << 20; // 4 mb
    constexpr uint64_t kBackwardWindowBytes = 4ull << 20; // 4 mb
    constexpr size_t kMaxNeedleBytes = 1024;
}
