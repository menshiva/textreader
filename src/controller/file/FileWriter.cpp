#include "FileWriter.h"

FileWriter::~FileWriter() {
    CloseHandle(m_Handle);
}

std::unique_ptr<FileWriter> FileWriter::open(
    const std::filesystem::path& path, const size_t bufferSize, const uint64_t preallocBytes, std::string& outErrorMsg
) {
    if (bufferSize == 0 || bufferSize % kSectorAlignment != 0 || bufferSize > MAXDWORD) {
        outErrorMsg = "Invalid write buffer size";
        return nullptr;
    }

    AlignedBuffer buffer(static_cast<char*>(_aligned_malloc(bufferSize, kSectorAlignment)));
    if (!buffer) {
        outErrorMsg = "Cannot allocate the write buffer";
        return nullptr;
    }

    const auto handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        outErrorMsg = "Cannot create the file for writing";
        return nullptr;
    }

    if (preallocBytes) {
        FILE_END_OF_FILE_INFO eof{};
        eof.EndOfFile.QuadPart = static_cast<LONGLONG>(preallocBytes);
        if (!SetFileInformationByHandle(handle, FileEndOfFileInfo, &eof, sizeof(eof))) {
            outErrorMsg = "Cannot reserve space for the file (not enough disk space?)";
            CloseHandle(handle);
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return nullptr;
        }
    }

    return std::unique_ptr<FileWriter>(new FileWriter(handle, std::move(buffer), bufferSize));
}

bool FileWriter::writeBuffer() {
    if (m_Finished)
        return false;

    if (!writeRaw(m_BufferSize))
        return false;
    m_LogicalSize += m_BufferSize;

    return true;
}

bool FileWriter::finish(const size_t num) {
    if (m_Finished || num > m_BufferSize)
        return false;
    m_Finished = true;

    bool ok = true;

    if (num > 0) {
        const size_t toWrite = (num + kSectorAlignment - 1) & ~(kSectorAlignment - 1); // round up to the nearest kSectorAlignment
        memset(m_Buffer.get() + num, '\0', toWrite - num);
        ok = writeRaw(toWrite);
        if (ok)
            m_LogicalSize += num; // the padding is not part of the file
    }

    FILE_END_OF_FILE_INFO eof{};
    eof.EndOfFile.QuadPart = static_cast<LONGLONG>(m_LogicalSize);
    if (!SetFileInformationByHandle(m_Handle, FileEndOfFileInfo, &eof, sizeof(eof)))
        ok = false;

    return ok;
}

bool FileWriter::writeRaw(size_t count) const {
    const char* p = m_Buffer.get();
    while (count > 0) {
        DWORD written = 0;
        if (!WriteFile(m_Handle, p, static_cast<DWORD>(count), &written, nullptr) || written == 0)
            return false;
        p += written;
        count -= written;
    }
    return true;
}

FileWriter::FileWriter(
    const HANDLE handle, AlignedBuffer&& buffer, const size_t bufferSize
) : m_Handle(handle), m_Buffer(std::move(buffer)), m_BufferSize(bufferSize) {}
