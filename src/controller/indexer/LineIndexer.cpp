#include "LineIndexer.h"
#include "../file/FileMapping.h"

LineIndexer::LineIndexer(FileMapping& file) : m_File(file) {}

void LineIndexer::build() {
    clear();

    const uint64_t fileSize = m_File.getSize();
    if (fileSize == 0)
        return;

    m_LineOffsets.push_back(0);

    uint64_t pos = 0;
    while (pos < fileSize) {
        const auto chunk = m_File.view(pos, kScanChunkBytes);
        if (chunk.empty())
            break;

        auto p = chunk.data();
        const auto end = p + chunk.size();

        while (p < end) {
            const auto nl = static_cast<const char*>(memchr(p, '\n', end - p));
            if (!nl)
                break;
            const uint64_t nextStart = pos + (nl - chunk.data()) + 1;
            if (nextStart < fileSize)
                m_LineOffsets.push_back(nextStart);
            p = nl + 1;
        }

        pos += chunk.size();
    }

    m_LineOffsets.shrink_to_fit();
}

void LineIndexer::clear() {
    m_LineOffsets.clear();
    m_LineOffsets.shrink_to_fit(); // clear allocation
}

std::string_view LineIndexer::get(const uint64_t i) const {
    if (i >= count())
        return {};
    const uint64_t fileSize = m_File.getSize();

    const uint64_t start = m_LineOffsets[i];
    const uint64_t end = i + 1 < count() ? m_LineOffsets[i + 1] : fileSize;

    uint64_t len = end - start;
    if (len > 0 && end < fileSize)
        --len; // remove '\n'

    const size_t want = std::min<uint64_t>(len, 64 * 1024);
    const auto s = m_File.view(start, want);
    if (s.empty())
        return {};

    std::string_view sv(s.data(), s.size());
    if (!sv.empty() && sv.back() == '\r') // CRLF
        sv.remove_suffix(1);
    return sv;
}
