#pragma once

#include <filesystem>
#include <span>
#include <windows.h>

// read-only sliding window over a file backed by a memory mapping
class FileMapping {
public:
    ~FileMapping();

    FileMapping(const FileMapping&) = delete;
    FileMapping& operator=(const FileMapping&) = delete;
    FileMapping(FileMapping&&) noexcept = delete;
    FileMapping& operator=(FileMapping&&) noexcept = delete;

    enum class AccessPattern : uint8_t {
        Sequential, // reading strictly forward (the window starts at the requested offset)
        RandomAccess // jumping in both directions (the request is centred in the window)
    };
    static std::unique_ptr<FileMapping> open(const std::filesystem::path& path, AccessPattern accessPattern, std::string& outErrorMsg);

    // returns a view into [offsetBytes, min(offsetBytes + numBytes, m_SizeBytes)).
    // pointer is valid until the next call
    std::span<const char> view(uint64_t offsetBytes, size_t numBytes);

    uint64_t getSizeBytes() const { return m_SizeBytes; }
private:
    FileMapping(AccessPattern accessPattern, HANDLE handle, uint64_t sizeBytes, HANDLE mappingHandle);

    void unmapView();

    const AccessPattern m_AccessPattern;
    const HANDLE m_Handle;
    const uint64_t m_SizeBytes;

    const HANDLE m_MappingHandle;
    const char* m_CurrentViewPtr = nullptr; // maps to m_CurrentViewOffsetBytes
    size_t m_CurrentViewSizeBytes = 0;
    uint64_t m_CurrentViewOffsetBytes = 0;

    // caps the working set regardless of file size (larger windows mean fewer remaps but more resident pages)
    static constexpr uint64_t kMinWindowSizeBytes = 64ull << 20; // 64 mb
};
