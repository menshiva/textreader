#include "FileMapping.h"

static DWORD allocationGranularity() {
    static const DWORD g = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return si.dwAllocationGranularity;
    }();
    return g;
}

FileMapping::~FileMapping() {
    close();
}

bool FileMapping::open(const std::filesystem::path &path) {
    const auto newFile = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ /*| FILE_SHARE_WRITE*/,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (newFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(newFile, &sz) || sz.QuadPart <= 0) {
        close();
        return false;
    }
    const auto newSize = static_cast<uint64_t>(sz.QuadPart);

    const auto newMapping = CreateFileMappingW( // returns NULL instead of INVALID_HANDLE_VALUE!!!!!
        newFile, nullptr, PAGE_READONLY, 0, 0, nullptr
    );
    if (!newMapping) {
        CloseHandle(newFile);
        return false;
    }

    close();

    m_FilePath = path;
    m_File = newFile;
    m_Size = newSize;
    m_Mapping = newMapping;

    return true;
}

void FileMapping::close() {
    m_CurrentViewOffset = 0;
    unmapView();

    if (m_Mapping) {
        CloseHandle(m_Mapping);
        m_Mapping = nullptr;
    }
    m_Size = 0;
    if (m_File != INVALID_HANDLE_VALUE) {
        CloseHandle(m_File);
        m_File = INVALID_HANDLE_VALUE;
    }
    m_FilePath.clear();
}

std::span<const char> FileMapping::view(const uint64_t offset, size_t len) {
    if (!m_Mapping || offset >= m_Size)
        return {};

    len = static_cast<size_t>(std::min<uint64_t>(len, m_Size - offset));
    if (len == 0)
        return {};

    if (!(m_CurrentViewPtr && offset >= m_CurrentViewOffset && offset + len <= m_CurrentViewOffset + m_CurrentViewLen)) {
        // not in current view -> get a new map
        unmapView();

        const DWORD gran = allocationGranularity();
        constexpr uint64_t back = kWindowSizeBytes / 2;
        uint64_t start = offset > back ? offset - back : 0; // move to the middle of the window
        start -= start % gran;

        const size_t requestSize = std::min<uint64_t>(std::max<uint64_t>(kWindowSizeBytes, len), m_Size - start);
        m_CurrentViewPtr = static_cast<const char*>(MapViewOfFile(
            m_Mapping, FILE_MAP_READ,
            static_cast<DWORD>(start >> 32),
            static_cast<DWORD>(start & 0xFFFFFFFF),
            requestSize
        ));
        if (!m_CurrentViewPtr)
            return {};

        m_CurrentViewOffset = start;
        m_CurrentViewLen = requestSize;
    }

    return {m_CurrentViewPtr + (offset - m_CurrentViewOffset), len};
}

void FileMapping::unmapView() {
    if (m_CurrentViewPtr) {
        UnmapViewOfFile(m_CurrentViewPtr);
        m_CurrentViewPtr = nullptr;
        m_CurrentViewLen = 0;
    }
}
