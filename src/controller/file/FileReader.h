#pragma once

#include <filesystem>
#include <span>
#include <windows.h>

// forward-only reader over a file backed by ordinary buffered ReadFile (sequential scan)
class FileReader {
public:
    ~FileReader();

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&&) noexcept = delete;
    FileReader& operator=(FileReader&&) noexcept = delete;

    static std::unique_ptr<FileReader> open(const std::filesystem::path& path, size_t bufferSize, std::string& outErrorMsg);

    void seek(uint64_t offsetBytes);

    // returns the next chunk of the file
    // the pointer is valid until the next call
    std::span<const char> next();

    uint64_t getSizeBytes() const { return m_SizeBytes; }
    bool hasError() const { return m_Error; }
private:
    FileReader(HANDLE handle, uint64_t sizeBytes, std::unique_ptr<char[]>&& buffer, size_t bufferSize);

    const HANDLE m_Handle;
    const uint64_t m_SizeBytes;

    const std::unique_ptr<char[]> m_Buffer;
    const size_t m_BufferSize;

    uint64_t m_CursorBytes = 0;
    bool m_Error = false;
};
