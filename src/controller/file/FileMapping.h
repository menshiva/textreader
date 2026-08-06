#pragma once

#include <filesystem>
#include <span>
#include <windows.h>

class FileMapping {
public:
    FileMapping() = default;
    ~FileMapping();

    FileMapping(const FileMapping&) = delete;
    FileMapping& operator=(const FileMapping&) = delete;
    FileMapping(FileMapping&&) = delete;
    FileMapping& operator=(FileMapping&&) = delete;

    bool open(const std::filesystem::path& path);
    void close();

    // returns a view to the [offset, min(offset+len, fileSize)). pointer is valid until the next call
    std::span<const char> view(uint64_t offset, size_t len);

    uint64_t getSize() const { return m_Size; }
private:
    void unmapView();

    HANDLE m_File = INVALID_HANDLE_VALUE;
    uint64_t m_Size = 0;
    HANDLE m_Mapping = nullptr;

    const char* m_CurrentViewPtr = nullptr; // aligned
    size_t m_CurrentViewLen = 0;
    uint64_t m_CurrentViewOffset = 0;

    static constexpr uint64_t kWindowSizeBytes = 64ull * 1024ull * 1024ull;
};
