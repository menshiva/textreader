#include "FileReader.h"

FileReader::~FileReader() {
    CloseHandle(m_Handle);
}

std::unique_ptr<FileReader> FileReader::open(const std::filesystem::path& path, const size_t bufferSize, std::string& outErrorMsg) {
    if (bufferSize == 0 || bufferSize > MAXDWORD) {
        outErrorMsg = "Invalid read buffer size";
        return nullptr;
    }

    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        outErrorMsg = "Cannot open the file";
        return nullptr;
    }

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(handle, &sz) || sz.QuadPart < 0) {
        outErrorMsg = "Cannot get the file size";
        CloseHandle(handle);
        return nullptr;
    }

    auto buffer = std::make_unique_for_overwrite<char[]>(bufferSize);
    return std::unique_ptr<FileReader>(new FileReader(handle, static_cast<uint64_t>(sz.QuadPart), std::move(buffer), bufferSize));
}

void FileReader::seek(const uint64_t offsetBytes) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offsetBytes);
    if (!SetFilePointerEx(m_Handle, pos, nullptr, FILE_BEGIN)) {
        m_Error = true;
        return;
    }
    m_CursorBytes = offsetBytes;
}

std::span<const char> FileReader::next() {
    if (m_Error || m_CursorBytes >= m_SizeBytes)
        return {}; // end of file

    const auto toRead = static_cast<DWORD>(std::min<uint64_t>(m_BufferSize, m_SizeBytes - m_CursorBytes));
    DWORD read = 0;
    if (!ReadFile(m_Handle, m_Buffer.get(), toRead, &read, nullptr)) {
        m_Error = true;
        return {};
    }

    m_CursorBytes += read;
    return {m_Buffer.get(), read};
}

FileReader::FileReader(
    const HANDLE handle, const uint64_t sizeBytes, std::unique_ptr<char[]>&& buffer, const size_t bufferSize
) : m_Handle(handle), m_SizeBytes(sizeBytes), m_Buffer(std::move(buffer)), m_BufferSize(bufferSize) {}
