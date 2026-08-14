#pragma once

#include <filesystem>
#include <stop_token>

class FileWriter;

namespace textgen {
    std::optional<std::string> checkDiskSpace(const std::filesystem::path& path, uint64_t neededBytes);

    std::optional<std::string> generate(
        FileWriter& writer, const std::stop_token& st, std::atomic<float>& outProgress,
        uint64_t targetBytes, uint64_t targetLines
    );

    constexpr size_t kWriteBufferSize = 4ull << 20; // 4 mb
}
