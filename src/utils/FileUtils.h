#pragma once

#include <filesystem>

namespace utils {
    std::optional<std::string> checkDiskSpace(const std::filesystem::path& path, uint64_t neededBytes);
}
