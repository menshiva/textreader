#include "LineIndexer.h"
#include <cassert>
#include "../file/FileMapping.h"

static bool isUtf8ContinuationByte(const unsigned char c) {
    // utf-8 always has 10xxxxxx
    return c >> 6 == 2;
}

static uint64_t countUtf8Codepoints(const char* dataBegin, const char* dataEnd) {
    uint64_t res = 0;
    while (dataBegin < dataEnd)
        res += static_cast<uint64_t>(!isUtf8ContinuationByte(*dataBegin++));
    return res;
}

static uint32_t utf8SeqLen(const unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string_view LineIndexer::get(const uint64_t lineIdx, const uint64_t fromCol, const uint64_t colsNum, uint64_t& outLineTotalLength) const {
    outLineTotalLength = 0;

    const auto lineBytes = getLineBytes(lineIdx);
    if (lineBytes.empty())
        return {};

    const auto dataBegin = lineBytes.data();
    const auto dataEnd = dataBegin + lineBytes.size();

    outLineTotalLength = countUtf8Codepoints(dataBegin, dataEnd);
    if (outLineTotalLength == lineBytes.size()) {
        // bytes num == utf-8 codepoints num -> ascii
        if (fromCol < lineBytes.size())
            return {dataBegin + fromCol, static_cast<size_t>(std::min<uint64_t>(colsNum, lineBytes.size() - fromCol))};
        return {};
    }

    // walk by utf-8 codepoints
    const char* p = dataBegin;
    for (uint64_t c = 0; c < fromCol && p < dataEnd; ++c)
        p += utf8SeqLen(*p);
    if (p >= dataEnd)
        return {};

    const char* q = p;
    for (uint64_t c = 0; c < colsNum && q < dataEnd; ++c)
        q += utf8SeqLen(*q);
    if (q > dataEnd)
        q = dataEnd; // last sequence was cut by kMaxBytesPerLine

    return {p, static_cast<size_t>(q - p)};
}

void LineIndexer::getTextSize(uint64_t& maxColsNum, uint64_t& rowsNum) const {
    rowsNum = m_LinesNum.load(std::memory_order_acquire);
    maxColsNum = m_MaxLineLength.load(std::memory_order_relaxed);
}

LineIndexer::~LineIndexer() = default;

std::unique_ptr<LineIndexer> LineIndexer::create(const std::filesystem::path& path, std::string& outErrorMsg) {
    auto scanFilePtr = FileMapping::open(path, FileMapping::AccessPattern::Sequential, outErrorMsg);
    if (!scanFilePtr)
        return nullptr;
    auto uiFilePtr = FileMapping::open(path, FileMapping::AccessPattern::RandomAccess, outErrorMsg);
    if (!uiFilePtr)
        return nullptr;
    return std::unique_ptr<LineIndexer>(new LineIndexer(std::move(scanFilePtr), std::move(uiFilePtr)));
}

static const char* alignToUtf8Boundary(const char* begin, const char* p) {
    // walks back to the start of a codepoint (used when a byte limit cut a sequence in half)
    while (p > begin && isUtf8ContinuationByte(*p))
        --p;
    return p;
}

void LineIndexer::startScan(const std::stop_token& st, std::atomic<float>& outProgress) {
    const uint64_t fileSizeBytes = m_ScanFilePtr->getSizeBytes();
    if (fileSizeBytes == 0)
        return;

    // handle utf-8 bom
    uint64_t contentStartOffsetBytes = 0;
    {
        const auto head = m_ScanFilePtr->view(0, 3);
        if (head.size() >= 3
                && static_cast<unsigned char>(head[0]) == 0xEF
                && static_cast<unsigned char>(head[1]) == 0xBB
                && static_cast<unsigned char>(head[2]) == 0xBF)
        {
            contentStartOffsetBytes = 3;
        }
    }
    if (contentStartOffsetBytes >= fileSizeBytes)
        return;

    // reserve max possible size to preserve stable pointers in get() function
    m_Anchors.reserve(fileSizeBytes / kBytesPerAnchor + 2);

    m_Anchors.push_back({0, contentStartOffsetBytes});
    m_AvailableAnchorsNum.store(1, std::memory_order_release);

    uint64_t currentLineIdx = 0;
    uint64_t currentLineStartOffsetBytes = contentStartOffsetBytes;

    uint64_t currentLineBytesNum = 0;
    uint64_t currentLineColsNum = 0;
    uint64_t maxLineLength = 0;

    uint64_t lastLineAnchorOffsetBytes = contentStartOffsetBytes;

    const auto appendLineChunk = [&] (const char* dataBegin, const char* dataEnd) {
        // counts columns only for the part that get() will actually return
        const auto bytesNum = static_cast<uint64_t>(dataEnd - dataBegin);
        if (currentLineBytesNum < kMaxBytesPerLine) {
            const uint64_t availableBytes = kMaxBytesPerLine - currentLineBytesNum;
            auto countEnd = dataBegin + std::min<uint64_t>(bytesNum, availableBytes);
            if (countEnd < dataEnd) // handle symbol "cut"
                countEnd = alignToUtf8Boundary(dataBegin, countEnd);
            currentLineColsNum += countUtf8Codepoints(dataBegin, countEnd);
        }
        currentLineBytesNum += bytesNum;
    };

    const auto publishData = [&] (const float progress) {
        outProgress.store(progress, std::memory_order_relaxed);
        m_MaxLineLength.store(maxLineLength, std::memory_order_relaxed);
        m_AvailableAnchorsNum.store(m_Anchors.size(), std::memory_order_release);
        m_LinesNum.store(currentLineIdx, std::memory_order_release);
    };

    const auto totalToProcessDbl = static_cast<double>(fileSizeBytes - contentStartOffsetBytes);

    // fixes rare case when \r is in the end of the previous chunk and \n is in the beginning of the current one
    char lastByteOfPrevChunk = '\0';

    // process file by chunks of kScanChunkBytes
    uint64_t posBytes = contentStartOffsetBytes;
    while (posBytes < fileSizeBytes) {
        if (st.stop_requested())
            return;

        const auto chunk = m_ScanFilePtr->view(posBytes, kScanChunkBytes);
        if (chunk.empty())
            break;

        // process chunk

        const auto chunkBegin = chunk.data();
        auto chunkDataP = chunkBegin;
        const auto chunkEnd = chunkBegin + chunk.size();

        while (chunkDataP < chunkEnd) {
            const auto newLinePtr = static_cast<const char*>(memchr(chunkDataP, '\n', chunkEnd - chunkDataP));
            if (!newLinePtr) {
                // new line not found -> probably should be in the next chunk
                appendLineChunk(chunkDataP, chunkEnd);
                break;
            }

            appendLineChunk(chunkDataP, newLinePtr);
            if (currentLineColsNum && currentLineBytesNum <= kMaxBytesPerLine) {
                // handle \r (CRLF)
                currentLineColsNum -= static_cast<uint64_t>(
                    (newLinePtr > chunkBegin && *(newLinePtr - 1) == '\r')
                        || (newLinePtr == chunkBegin && lastByteOfPrevChunk == '\r')
                );
            }
            if (currentLineColsNum > maxLineLength)
                maxLineLength = currentLineColsNum;
            currentLineBytesNum = 0;
            currentLineColsNum = 0;

            const uint64_t nextLineStartBytes = posBytes + static_cast<uint64_t>(newLinePtr - chunkBegin) + 1;
            ++currentLineIdx;
            currentLineStartOffsetBytes = nextLineStartBytes;

            if (nextLineStartBytes < fileSizeBytes && nextLineStartBytes - lastLineAnchorOffsetBytes >= kBytesPerAnchor) {
                // we've processed at least kBytesPerAnchor -> create a new line anchor
                assert(m_Anchors.size() < m_Anchors.capacity());
                m_Anchors.push_back({currentLineIdx, nextLineStartBytes});
                lastLineAnchorOffsetBytes = nextLineStartBytes;
            }

            chunkDataP = newLinePtr + 1;
        }

        lastByteOfPrevChunk = *(chunkEnd - 1);
        posBytes += chunk.size();

        publishData(static_cast<float>(static_cast<double>(posBytes - contentStartOffsetBytes) / totalToProcessDbl));
    }

    // process last line remainder
    if (currentLineStartOffsetBytes < fileSizeBytes) {
        if (currentLineColsNum > maxLineLength)
            maxLineLength = currentLineColsNum;
        ++currentLineIdx;
    }

    publishData(1.0f);
}

LineIndexer::LineIndexer(
    std::unique_ptr<FileMapping>&& scanFilePtr, std::unique_ptr<FileMapping>&& uiFilePtr
) : m_ScanFilePtr(std::move(scanFilePtr)), m_UiFilePtr(std::move(uiFilePtr)) {}

uint64_t LineIndexer::computeLineOffsetBytes(const uint64_t lineIdx) const {
    const uint64_t linesNum = m_LinesNum.load(std::memory_order_acquire);
    if (lineIdx >= linesNum)
        return UINT64_MAX;

    if (m_CacheData.lineIdx == lineIdx)
        return m_CacheData.lineStartOffsetBytes;

    const size_t anchorsNum = m_AvailableAnchorsNum.load(std::memory_order_relaxed);
    if (!anchorsNum)
        return UINT64_MAX;

    // find anchor idx to which lineIdx belongs
    size_t anchorIdx = SIZE_MAX;
    {
        auto searchBegin = m_Anchors.begin();
        auto searchEnd = m_Anchors.begin() + anchorsNum;

        // try to find nearest from cache
        if (m_CacheData.lineIdx != UINT64_MAX) {
            assert(m_CacheData.anchorIdx < anchorsNum);
            if (lineIdx >= m_CacheData.lineIdx) {
                searchBegin += m_CacheData.anchorIdx;
                if (m_CacheData.anchorIdx + 1 < anchorsNum && lineIdx < m_Anchors[m_CacheData.anchorIdx + 1].lineIdx) {
                    // line is after the cached line, but within the current anchor
                    anchorIdx = m_CacheData.anchorIdx;
                }
            }
            else {
                if (lineIdx >= m_Anchors[m_CacheData.anchorIdx].lineIdx) {
                    // line is before the cached line, but within the current anchor
                    searchEnd = m_Anchors.begin() + m_CacheData.anchorIdx + 1;
                    anchorIdx = m_CacheData.anchorIdx;
                }
                else {
                    searchEnd = m_Anchors.begin() + m_CacheData.anchorIdx;
                    if (m_CacheData.anchorIdx > 0 && lineIdx >= m_Anchors[m_CacheData.anchorIdx - 1].lineIdx) {
                        // line is before the cached line, but within the previous anchor
                        anchorIdx = m_CacheData.anchorIdx - 1;
                    }
                }
            }
        }

        if (anchorIdx == SIZE_MAX) {
            const auto it = std::upper_bound(
                searchBegin, searchEnd, lineIdx,
                [] (const uint64_t _lineIdx, const Anchor& anchor) {
                    return _lineIdx < anchor.lineIdx;
                }
            );
            assert(it != m_Anchors.begin());
            anchorIdx = static_cast<size_t>(it - 1 - m_Anchors.begin());
        }
    }

    // pick closest
    uint64_t currentLineIdx = m_Anchors[anchorIdx].lineIdx;
    uint64_t currentOffsetBytes = m_Anchors[anchorIdx].offsetBytes;
    if (m_CacheData.lineIdx != UINT64_MAX && lineIdx >= m_CacheData.lineIdx && currentLineIdx < m_CacheData.lineIdx) {
        // the cache only helps forward (memchr can't scan backwards)
        currentLineIdx = m_CacheData.lineIdx;
        currentOffsetBytes = m_CacheData.lineStartOffsetBytes;
    }

    uint64_t linesToSkip = lineIdx - currentLineIdx;
    while (linesToSkip > 0) {
        const auto numBytes = static_cast<size_t>(std::min<uint64_t>(kBytesPerAnchor, m_UiFilePtr->getSizeBytes() - currentOffsetBytes));
        const auto chunk = m_UiFilePtr->view(currentOffsetBytes, numBytes);
        if (chunk.empty())
            return UINT64_MAX;

        // process chunk

        const auto chunkBegin = chunk.data();
        auto chunkDataP = chunkBegin;
        const auto chunkEnd = chunkBegin + chunk.size();

        while (linesToSkip > 0 && chunkDataP < chunkEnd) {
            const auto newLinePtr = static_cast<const char*>(memchr(chunkDataP, '\n', chunkEnd - chunkDataP));
            if (!newLinePtr) {
                chunkDataP = chunkEnd;
                break;
            }
            chunkDataP = newLinePtr + 1;
            --linesToSkip;
        }

        if (chunkDataP == chunkBegin)
            return UINT64_MAX; // no progress: index does not match the data

        currentOffsetBytes += static_cast<uint64_t>(chunkDataP - chunkBegin);
    }

    m_CacheData.lineIdx = lineIdx;
    m_CacheData.lineStartOffsetBytes = currentOffsetBytes;
    m_CacheData.anchorIdx = anchorIdx;

    return currentOffsetBytes;
}

std::span<const char> LineIndexer::getLineBytes(const uint64_t lineIdx) const {
    const uint64_t lineOffsetBytes = computeLineOffsetBytes(lineIdx);
    if (lineOffsetBytes == UINT64_MAX)
        return {};

    const auto numBytes = static_cast<size_t>(std::min<uint64_t>(kMaxBytesPerLine, m_UiFilePtr->getSizeBytes() - lineOffsetBytes));
    const auto raw = m_UiFilePtr->view(lineOffsetBytes, numBytes);
    if (raw.empty())
        return {};

    const auto newLinePtr = static_cast<const char*>(memchr(raw.data(), '\n', raw.size()));

    size_t lineBytesNum = newLinePtr ? static_cast<size_t>(newLinePtr - raw.data()) : raw.size();
    if (lineBytesNum > 0 && raw[lineBytesNum - 1] == '\r')
        --lineBytesNum; // CRLF

    return raw.first(lineBytesNum);
}
