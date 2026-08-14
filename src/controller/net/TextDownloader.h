#pragma once

#include <filesystem>
#include <stop_token>

class FileWriter;

namespace http {
    struct ResponseInfo {
        uint64_t totalBytes = 0; // 0 when there is no Content-Length
    };

    std::optional<std::string> download(
        FileWriter& writer, const std::stop_token& st, std::atomic<float>& outProgress, ResponseInfo& outInfo,
        const std::string& url, const std::filesystem::path& targetPath
    );

    constexpr size_t kWriteBufferSize = 1ull << 20; // 1 mb
}
