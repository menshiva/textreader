#pragma once

#include <filesystem>
#include <span>
#include <stop_token>

class FileReader;
class FileMapping;

// sparse line index over a text file
// stores a byte offset only every kBytesPerAnchor
// the rest is rescanned with memchr during get()
class LineIndexer {
public:
    ~LineIndexer();

    LineIndexer(const LineIndexer&) = delete;
    LineIndexer& operator=(const LineIndexer&) = delete;
    LineIndexer(LineIndexer&&) noexcept = delete;
    LineIndexer& operator=(LineIndexer&&) noexcept = delete;

    static std::unique_ptr<LineIndexer> create(const std::filesystem::path& path, std::string& outErrorMsg);

    std::optional<std::string> startScan(const std::stop_token& st, std::atomic<float>& outProgress);

    // returns codepoints [fromCol, min(fromCol + colsNum, line length)) of the line.
    // pointer is valid until the next call
    std::string_view get(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength) const;
    void getTextSize(uint64_t& maxColsNum, uint64_t& rowsNum) const;
private:
    LineIndexer(std::unique_ptr<FileReader>&& scanFilePtr, std::unique_ptr<FileMapping>&& uiFilePtr);

    uint64_t computeLineOffsetBytes(uint64_t lineIdx) const;
    std::span<const char> getLineBytes(uint64_t lineIdx) const;

    std::unique_ptr<FileReader> m_ScanFilePtr; // for startScan() only
    const std::unique_ptr<FileMapping> m_UiFilePtr; // for get() only

    struct Anchor { uint64_t lineIdx; uint64_t offsetBytes; };
    std::vector<Anchor> m_Anchors;

    // data for get() and getTextSize() functions
    std::atomic<size_t> m_AvailableAnchorsNum = 0;
    std::atomic<uint64_t> m_MaxLineLength = 0;
    std::atomic<uint64_t> m_LinesNum = 0;

    // cache data useful for UI get() requests (e.g. when requesting neighbor lines one after another)
    mutable struct CacheData {
        uint64_t lineIdx = UINT64_MAX;
        uint64_t lineStartOffsetBytes = 0;
        size_t anchorIdx = 0;
    } m_CacheData;

    static constexpr uint64_t kBytesPerAnchor = 128ull << 10; // caps how far get() has to rescan (trades index memory for lookup latency) - 128 kb
    static constexpr size_t kScanChunkBytes = 4ull << 20; // read buffer for the linear scan - 4 mb
    static constexpr size_t kMaxBytesPerLine = 64ull << 10; // guards against files with very long lines or no line breaks - 64 kb
};
