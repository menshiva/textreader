#include "LineIndexer.h"
#include "../file/FileMapping.h"

static uint32_t utf8SeqLen(const unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

LineIndexer::LineIndexer(FileMapping& file) : m_File(file) {}

void LineIndexer::build() {
    clear();

    const uint64_t fileSize = m_File.getSize();
    if (fileSize == 0)
        return;

    uint64_t contentStart = 0;
    {
        // handle BOM
        const auto head = m_File.view(0, 3);
        if (head.size() >= 3 && static_cast<unsigned char>(head[0]) == 0xEF
            && static_cast<unsigned char>(head[1]) == 0xBB && static_cast<unsigned char>(head[2]) == 0xBF)
        {
            contentStart = 3;
        }
    }
    m_LineOffsets.push_back(contentStart);

    uint64_t lineStart = contentStart;
    uint64_t lineCols = 0;
    uint64_t pos = contentStart;

    while (pos < fileSize) {
        const auto chunk = m_File.view(pos, kScanChunkBytes);
        if (chunk.empty())
            break;

        auto p = chunk.data();
        const auto end = p + chunk.size();

        while (p < end) {
            const auto nl = static_cast<const char*>(memchr(p, '\n', end - p));
            if (!nl) {
                for (const char* c = p; c < end; ++c)
                    lineCols += (static_cast<unsigned char>(*c) & 0xC0) != 0x80;
                break;
            }

            for (const char* c = p; c < nl; ++c)
                lineCols += (static_cast<unsigned char>(*c) & 0xC0) != 0x80;
            if (nl > chunk.data() && *(nl - 1) == '\r' && lineCols > 0)
                --lineCols;

            if (lineCols > m_MaxLineLength)
                m_MaxLineLength = lineCols;
            lineCols = 0;

            const uint64_t nlPos = pos + (nl - chunk.data());

            const uint64_t nextStart = nlPos + 1;
            if (nextStart < fileSize)
                m_LineOffsets.push_back(nextStart);
            lineStart = nextStart;

            p = nl + 1;
        }

        pos += chunk.size();
    }

    if (lineStart < fileSize && lineCols > m_MaxLineLength)
        m_MaxLineLength = lineCols;

    if (m_MaxLineLength > kMaxLineBytes)
        m_MaxLineLength = kMaxLineBytes;

    m_LineOffsets.shrink_to_fit();
}

void LineIndexer::clear() {
    m_LineOffsets.clear();
    m_LineOffsets.shrink_to_fit(); // clear allocation
    m_MaxLineLength = 0;
}

std::string_view LineIndexer::get(const uint64_t lineIdx, const uint64_t fromCol, const uint64_t maxCols, uint64_t& outLineTotalLength) const {
    outLineTotalLength = 0;
    if (lineIdx >= count())
        return {};

    const uint64_t fileSize = m_File.getSize();
    const uint64_t start = m_LineOffsets[lineIdx];
    const uint64_t end = lineIdx + 1 < count() ? m_LineOffsets[lineIdx + 1] : fileSize;

    const size_t want = std::min<uint64_t>(end - start, kMaxLineBytes);
    const auto raw = m_File.view(start, want);
    if (raw.empty())
        return {};

    std::string_view sv(raw.data(), raw.size());
    if (!sv.empty() && sv.back() == '\n')
        sv.remove_suffix(1);
    if (!sv.empty() && sv.back() == '\r')
        sv.remove_suffix(1);

    for (const char c : sv)
        outLineTotalLength += (static_cast<unsigned char>(c) & 0xC0) != 0x80;

    // cols to bytes
    const char* p = sv.data();
    const char* const bufEnd = p + sv.size();

    for (uint64_t c = 0; c < fromCol && p < bufEnd; ++c)
        p += utf8SeqLen(static_cast<unsigned char>(*p));
    if (p >= bufEnd)
        return {};

    const char* q = p;
    for (uint64_t c = 0; c < maxCols && q < bufEnd; ++c)
        q += utf8SeqLen(static_cast<unsigned char>(*q));
    if (q > bufEnd)
        q = bufEnd;

    return {p, static_cast<size_t>(q - p)};
}
