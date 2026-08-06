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

    std::string_view get(size_t i) const;

    size_t count() const { return m_LineOffsets.size(); }
private:
    FileMapping& m_File;

    std::vector<uint64_t> m_LineOffsets; // byte offset for each line

    static constexpr size_t kScanChunkBytes = 4ull * 1024ull * 1024ull;
};
