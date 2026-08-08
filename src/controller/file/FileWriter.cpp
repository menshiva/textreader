#include "FileWriter.h"

FileWriter::~FileWriter() {
    if (m_Handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_Handle);
    if (m_Buffer)
        VirtualFree(m_Buffer, 0, MEM_RELEASE);
}

std::unique_ptr<FileWriter> FileWriter::open(const std::filesystem::path& path, const size_t bufferSize, const uint64_t preallocBytes) {
    if (bufferSize == 0 || bufferSize % 4096 != 0 || bufferSize > MAXDWORD)
        return nullptr;

    auto res = std::unique_ptr<FileWriter>(new FileWriter());

    static constexpr DWORD kBase = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
    res->m_Handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, kBase | FILE_FLAG_NO_BUFFERING, nullptr
    );
    if (res->m_Handle == INVALID_HANDLE_VALUE) {
        // try to open a file without FILE_FLAG_NO_BUFFERING
        res->m_Handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, kBase, nullptr
        );
        if (res->m_Handle == INVALID_HANDLE_VALUE)
            return nullptr;
        res->m_Unbuffered = false;
    }

    FILE_STORAGE_INFO si{};
    if (GetFileInformationByHandleEx(res->m_Handle, FileStorageInfo, &si, sizeof(si)) && si.LogicalBytesPerSector > res->m_Sector)
        res->m_Sector = si.LogicalBytesPerSector;
    if (bufferSize % res->m_Sector != 0)
        return nullptr;

    res->m_BufferSize = bufferSize;

    if (preallocBytes) {
        FILE_END_OF_FILE_INFO eof{};
        eof.EndOfFile.QuadPart = static_cast<LONGLONG>(preallocBytes);
        if (!SetFileInformationByHandle(res->m_Handle, FileEndOfFileInfo, &eof, sizeof(eof)))
            return nullptr; // probably no space
    }

    res->m_Buffer = static_cast<char*>(VirtualAlloc(nullptr, res->m_BufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!res->m_Buffer)
        return nullptr;

    return res;
}

bool FileWriter::submit(const size_t count) {
    if (m_Finished || count == 0 || count > m_BufferSize)
        return false;
    if (m_Unbuffered && count % m_Sector != 0)
        return false;

    if (!writeRaw(count))
        return false;
    m_LogicalSize += count;

    return true;
}

bool FileWriter::finish(const size_t tailCount) {
    if (m_Finished)
        return false;
    m_Finished = true;

    bool ok = true;

    if (tailCount > 0 && tailCount <= m_BufferSize) {
        size_t toWrite = tailCount;
        if (m_Unbuffered) {
            toWrite = (tailCount + m_Sector - 1) & ~static_cast<size_t>(m_Sector - 1);
            if (toWrite > m_BufferSize)
                toWrite = m_BufferSize;
            memset(m_Buffer + tailCount, '\0', toWrite - tailCount);
        }
        ok = writeRaw(toWrite);
        if (ok)
            m_LogicalSize += tailCount;
    }

    FILE_END_OF_FILE_INFO eof{};
    eof.EndOfFile.QuadPart = static_cast<LONGLONG>(m_LogicalSize);
    if (!SetFileInformationByHandle(m_Handle, FileEndOfFileInfo, &eof, sizeof(eof)))
        ok = false;

    return ok;
}

bool FileWriter::writeRaw(const size_t count) const {
    DWORD written = 0;
    if (!WriteFile(m_Handle, m_Buffer, static_cast<DWORD>(count), &written, nullptr))
        return false;
    return written == count;
}
