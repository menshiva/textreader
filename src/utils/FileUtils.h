#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace utils {
    std::optional<std::string> checkDiskSpace(const std::filesystem::path& path, uint64_t neededBytes);
}
