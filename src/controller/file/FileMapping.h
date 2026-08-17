#pragma once

#include <filesystem>
#include <span>
#include <windows.h>

// read-only sliding window over a file backed by a memory mapping (random access)
class FileMapping {
public:
    ~FileMapping();

    FileMapping(const FileMapping&) = delete;
    FileMapping& operator=(const FileMapping&) = delete;
    FileMapping(FileMapping&&) noexcept = delete;
    FileMapping& operator=(FileMapping&&) noexcept = delete;

    static std::unique_ptr<FileMapping> open(const std::filesystem::path& path, std::string& outErrorMsg);

    // returns a view into [offsetBytes, min(offsetBytes + numBytes, m_SizeBytes)).
    // the pointer is valid until the next call
    std::span<const char> view(uint64_t offsetBytes, size_t numBytes);

    uint64_t getSizeBytes() const { return m_SizeBytes; }
private:
    FileMapping(HANDLE handle, uint64_t sizeBytes, HANDLE mappingHandle);

    void unmapView();

    const HANDLE m_Handle;
    const uint64_t m_SizeBytes;

    const HANDLE m_MappingHandle;
    const char* m_CurrentViewPtr = nullptr; // maps to m_CurrentViewOffsetBytes
    size_t m_CurrentViewSizeBytes = 0;
    uint64_t m_CurrentViewOffsetBytes = 0;

    // caps the working set regardless of file size
    static constexpr uint64_t kMinWindowSizeBytes = 4ull << 20; // 4 mb
};
