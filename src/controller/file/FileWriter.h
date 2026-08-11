#pragma once

#include <filesystem>
#include <windows.h>

class FileWriter {
public:
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    // FileWriter(FileWriter&&) = delete;
    // FileWriter& operator=(FileWriter&&) = delete;

    static std::unique_ptr<FileWriter> open(const std::filesystem::path& path, size_t bufferSize, uint64_t preallocBytes = 0);

    char* getBuffer() const { return m_Buffer; }
    size_t getBufferSize() const { return m_BufferSize; }

    bool submit(size_t count);
    bool finish(size_t tailCount);
private:
    FileWriter() = default;

    bool writeRaw(size_t count) const;

    HANDLE m_Handle = INVALID_HANDLE_VALUE;
    char* m_Buffer = nullptr;

    size_t m_BufferSize = 0;
    DWORD m_Sector = 4096;
    uint64_t m_LogicalSize = 0;
    bool m_Unbuffered = true;
    bool m_Finished = false;
};
