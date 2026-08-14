#include "FileUtils.h"
#include <windows.h>

std::optional<std::string> utils::checkDiskSpace(const std::filesystem::path& path, const uint64_t neededBytes) {
    if (neededBytes == 0)
        return std::nullopt;

    const auto dir = path.parent_path();
    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &freeBytes, nullptr, nullptr))
        return std::nullopt;

    if (freeBytes.QuadPart >= neededBytes)
        return std::nullopt;

    return "Not enough free disk space";
}
