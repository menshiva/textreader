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

std::span<const char> FileReader::next() {
    if (m_Error)
        return {};

    DWORD read = 0;
    if (!ReadFile(m_Handle, m_Buffer.get(), static_cast<DWORD>(m_BufferSize), &read, nullptr)) {
        m_Error = true;
        return {};
    }
    return {m_Buffer.get(), read}; // read == 0 -> end of file
}

FileReader::FileReader(
    const HANDLE handle, const uint64_t sizeBytes, std::unique_ptr<char[]>&& buffer, const size_t bufferSize
) : m_Handle(handle), m_SizeBytes(sizeBytes), m_Buffer(std::move(buffer)), m_BufferSize(bufferSize) {}
