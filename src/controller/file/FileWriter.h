#pragma once

#include <filesystem>
#include <windows.h>

// sequential writer (no buffering)
class FileWriter {
public:
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&&) = delete;
    FileWriter& operator=(FileWriter&&) = delete;

    // bufferSize must be a multiple of kSectorAlignment
    static std::unique_ptr<FileWriter> open(
        const std::filesystem::path& path, size_t bufferSize, uint64_t preallocBytes, std::string& outErrorMsg
    );

    char* getBuffer() const { return m_Buffer.get(); }
    size_t getBufferSize() const { return m_BufferSize; }

    // writes the whole buffer
    bool writeBuffer();
    // writes the last partial buffer and trims the file to its real size
    bool finish(size_t num);
private:
    // https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering#alignment-and-file-access-requirements
    struct AlignedDeleter { void operator()(char* p) const { _aligned_free(p); } };
    using AlignedBuffer = std::unique_ptr<char[], AlignedDeleter>;

    FileWriter(HANDLE handle, AlignedBuffer&& buffer, size_t bufferSize);

    bool writeRaw(size_t count) const;

    const HANDLE m_Handle;
    const AlignedBuffer m_Buffer;
    const size_t m_BufferSize;

    uint64_t m_LogicalSize = 0;
    bool m_Finished = false;

    // https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering#alignment-and-file-access-requirements
    static constexpr size_t kSectorAlignment = 4096;
};
