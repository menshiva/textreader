#pragma once

#include <string_view>
#include <vector>

class FileMapping;

class LineIndexer {
public:
    explicit LineIndexer(FileMapping& file);
    ~LineIndexer() = default;

    LineIndexer(const LineIndexer&) = delete;
    LineIndexer& operator=(const LineIndexer&) = delete;
    LineIndexer(LineIndexer&&) = delete;
    LineIndexer& operator=(LineIndexer&&) = delete;

    void build();
    void clear();

    std::string_view get(uint64_t lineIdx, uint64_t fromCol, uint64_t maxCols, uint64_t& outLineTotalLength) const;
    uint64_t count() const { return m_LineOffsets.size(); }
    uint64_t maxLineLength() const { return m_MaxLineLength; }
private:
    FileMapping& m_File;

    std::vector<uint64_t> m_LineOffsets; // byte offset for each line
    uint64_t m_MaxLineLength = 0;

    static constexpr size_t kScanChunkBytes = 4ull << 20; // 4 mb
    static constexpr size_t kMaxLineBytes = 64 * 1024;
};
