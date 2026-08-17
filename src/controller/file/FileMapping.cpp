#include "FileMapping.h"

FileMapping::~FileMapping() {
    unmapView();
    if (m_MappingHandle)
        CloseHandle(m_MappingHandle);
    CloseHandle(m_Handle);
}

std::unique_ptr<FileMapping> FileMapping::open(const std::filesystem::path& path, std::string& outErrorMsg) {
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr
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
    const auto sizeBytes = static_cast<uint64_t>(sz.QuadPart);

    HANDLE mappingHandle = nullptr; // empty file can't be mapped
    if (sizeBytes > 0) {
        // CreateFileMappingW() returns NULL instead of INVALID_HANDLE_VALUE!!!!!
        mappingHandle = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mappingHandle) {
            outErrorMsg = "Cannot create the file mapping";
            CloseHandle(handle);
            return nullptr;
        }
    }

    return std::unique_ptr<FileMapping>(new FileMapping(handle, sizeBytes, mappingHandle));
}

// requires for MapViewOfFile (https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile#parameters:~:text=That%20is%2C%20the%20offset%20must%20be%20a%20multiple%20of%20the%20VirtualAlloc%20allocation%20granularity.)
static DWORD allocationGranularity() {
    static const DWORD g = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return si.dwAllocationGranularity;
    }();
    return g;
}

std::span<const char> FileMapping::view(const uint64_t offsetBytes, size_t numBytes) {
    if (!m_MappingHandle || offsetBytes >= m_SizeBytes)
        return {};

    // clamp to the total file size
    numBytes = static_cast<size_t>(std::min<uint64_t>(numBytes, m_SizeBytes - offsetBytes));
    if (numBytes == 0)
        return {};

    if (!m_CurrentViewPtr || offsetBytes < m_CurrentViewOffsetBytes || offsetBytes + numBytes > m_CurrentViewOffsetBytes + m_CurrentViewSizeBytes) {
        // not in current view -> remap
        unmapView();

        const uint64_t windowSizeBytes = std::max<uint64_t>(kMinWindowSizeBytes, numBytes);

        uint64_t startBytes = offsetBytes;

        // align to the allocation granularity
        const uint64_t alignmentShiftBytes = startBytes % static_cast<uint64_t>(allocationGranularity());
        startBytes -= alignmentShiftBytes;

        // aligning moves the start back -> the window has to grow by the same amount + clamp to the total file size
        const auto requestSizeBytes = static_cast<size_t>(std::min<uint64_t>(windowSizeBytes + alignmentShiftBytes, m_SizeBytes - startBytes));

        const ULARGE_INTEGER dwFileOffset{.QuadPart = startBytes};
        m_CurrentViewPtr = static_cast<const char*>(MapViewOfFile(
            m_MappingHandle, FILE_MAP_READ,
            dwFileOffset.HighPart, dwFileOffset.LowPart,
            requestSizeBytes
        ));
        if (!m_CurrentViewPtr)
            return {};

        m_CurrentViewOffsetBytes = startBytes;
        m_CurrentViewSizeBytes = requestSizeBytes;
    }

    return {m_CurrentViewPtr + (offsetBytes - m_CurrentViewOffsetBytes), numBytes};
}

FileMapping::FileMapping(
    const HANDLE handle, const uint64_t sizeBytes, const HANDLE mappingHandle
) : m_Handle(handle), m_SizeBytes(sizeBytes), m_MappingHandle(mappingHandle) {}

void FileMapping::unmapView() {
    if (m_CurrentViewPtr)
        UnmapViewOfFile(m_CurrentViewPtr);
    m_CurrentViewPtr = nullptr;
    m_CurrentViewSizeBytes = 0;
    m_CurrentViewOffsetBytes = 0;
}
