#include "LineIndexer.h"
#include <cassert>
#include "../../utils/Utf8.h"
#include "../file/FileMapping.h"
#include "../file/FileReader.h"

LineIndexer::~LineIndexer() = default;

std::unique_ptr<LineIndexer> LineIndexer::create(const std::filesystem::path& path, std::string& outErrorMsg) {
    auto scanFilePtr = FileReader::open(path, kScanChunkBytes, outErrorMsg);
    if (!scanFilePtr)
        return nullptr;
    auto uiFilePtr = FileMapping::open(path, outErrorMsg);
    if (!uiFilePtr)
        return nullptr;
    return std::unique_ptr<LineIndexer>(new LineIndexer(std::move(scanFilePtr), std::move(uiFilePtr)));
}

std::optional<std::string> LineIndexer::startScan(const std::stop_token& st, std::atomic<float>& outProgress) {
    const uint64_t fileSizeBytes = m_ScanFilePtr->getSizeBytes();
    if (fileSizeBytes == 0)
        return std::nullopt;

    auto chunk = m_ScanFilePtr->next();
    if (chunk.empty())
        return m_ScanFilePtr->hasError() ? std::optional<std::string>("Cannot read the file") : std::nullopt;

    const uint64_t contentStartOffsetBytes = chunk.size() >= 3 && static_cast<unsigned char>(chunk[0]) == 0xEF
        && static_cast<unsigned char>(chunk[1]) == 0xBB && static_cast<unsigned char>(chunk[2]) == 0xBF
        ? 3 : 0;

    // reserve max possible size to preserve stable pointers in get() function
    m_Anchors.reserve(fileSizeBytes / kBytesPerAnchor + 2);

    m_Anchors.push_back({0, contentStartOffsetBytes});
    m_AvailableAnchorsNum.store(1, std::memory_order_release);

    uint64_t currentLineIdx = 0;
    uint64_t currentLineStartOffsetBytes = contentStartOffsetBytes;

    uint64_t currentLineBytesNum = 0;
    uint64_t currentLineColsNum = 0;
    uint64_t maxLineLength = 0;

    const auto appendLineChunk = [&] (const char* dataBegin, const char* dataEnd) {
        // counts columns only for the part that get() will actually return
        const auto bytesNum = static_cast<uint64_t>(dataEnd - dataBegin);
        if (currentLineBytesNum < kMaxBytesPerLine) {
            const uint64_t availableBytes = kMaxBytesPerLine - currentLineBytesNum;
            auto countEnd = dataBegin + std::min<uint64_t>(bytesNum, availableBytes);
            if (countEnd < dataEnd) // handle symbol "cut"
                countEnd = utf8::alignToBoundary(dataBegin, countEnd);
            currentLineColsNum += utf8::countCodepoints(dataBegin, countEnd);
        }
        currentLineBytesNum += bytesNum;
    };

    const auto publishData = [&] (const float progress, const uint64_t scannedBytes) {
        // https://en.cppreference.com/w/cpp/atomic/memory_order#Release-Acquire_ordering
        outProgress.store(progress, std::memory_order_relaxed);
        m_MaxLineLength.store(maxLineLength, std::memory_order_relaxed);
        m_AvailableAnchorsNum.store(m_Anchors.size(), std::memory_order_release);
        m_LinesNum.store(currentLineIdx, std::memory_order_release);
        m_ScannedBytes.store(scannedBytes, std::memory_order_release);
    };

    if (contentStartOffsetBytes >= fileSizeBytes) {
        // edge case - a file with only bom
        publishData(1.0f, fileSizeBytes);
        m_ScanFilePtr.reset();
        return std::nullopt;
    }

    const auto totalToProcessDbl = static_cast<double>(fileSizeBytes - contentStartOffsetBytes);

    // fixes rare case when \r is in the end of the previous chunk and \n is in the beginning of the current one
    char lastByteOfPrevChunk = '\0';

    // absolute offset of the current chunk's first byte
    uint64_t chunkStartBytes = 0;

    // process file by chunks of kScanChunkBytes
    while (true) {
        // process chunk

        const auto chunkBegin = chunk.data();
        auto chunkDataP = chunkBegin + (chunkStartBytes == 0 ? contentStartOffsetBytes : 0); // skip the bom
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

            const uint64_t nextLineStartBytes = chunkStartBytes + static_cast<uint64_t>(newLinePtr - chunkBegin) + 1;
            ++currentLineIdx;
            currentLineStartOffsetBytes = nextLineStartBytes;

            if (nextLineStartBytes < fileSizeBytes && nextLineStartBytes - m_Anchors.back().offsetBytes >= kBytesPerAnchor) {
                // we've processed at least kBytesPerAnchor -> create a new line anchor
                assert(m_Anchors.size() < m_Anchors.capacity());
                m_Anchors.push_back({currentLineIdx, nextLineStartBytes});
            }

            chunkDataP = newLinePtr + 1;
        }

        lastByteOfPrevChunk = *(chunkEnd - 1);
        chunkStartBytes += chunk.size();

        publishData(
            static_cast<float>(static_cast<double>(chunkStartBytes - contentStartOffsetBytes) / totalToProcessDbl),
            currentLineStartOffsetBytes
        );

        if (st.stop_requested())
            return std::nullopt;

        chunk = m_ScanFilePtr->next();
        if (chunk.empty()) {
            if (m_ScanFilePtr->hasError())
                return "Cannot read the file";
            break; // end of file
        }
    }

    // process last line remainder
    if (currentLineStartOffsetBytes < fileSizeBytes) {
        if (currentLineColsNum > maxLineLength)
            maxLineLength = currentLineColsNum;
        ++currentLineIdx;
    }

    publishData(1.0f, fileSizeBytes);
    m_ScanFilePtr.reset();

    return std::nullopt;
}

uint64_t LineIndexer::computeLineOffsetBytes(const uint64_t lineIdx) const {
    const uint64_t linesNum = m_LinesNum.load(std::memory_order_acquire);
    if (lineIdx >= linesNum)
        return UINT64_MAX;

    if (m_CacheData.lineIdx == lineIdx)
        return m_CacheData.lineStartOffsetBytes;

    const size_t anchorsNum = m_AvailableAnchorsNum.load(std::memory_order_acquire);
    if (!anchorsNum)
        return UINT64_MAX;

    const size_t anchorIdx = findAnchorByLine(lineIdx, anchorsNum);
    uint64_t currentLineIdx = m_Anchors[anchorIdx].lineIdx;
    uint64_t currentOffsetBytes = m_Anchors[anchorIdx].offsetBytes;

    if (m_CacheData.lineIdx != UINT64_MAX && lineIdx >= m_CacheData.lineIdx && currentLineIdx < m_CacheData.lineIdx) {
        // the cache only helps forward (memchr can't scan backwards)
        currentLineIdx = m_CacheData.lineIdx;
        currentOffsetBytes = m_CacheData.lineStartOffsetBytes;
    }

    uint64_t linesToSkip = lineIdx - currentLineIdx;
    if (!walkLinesForward(currentOffsetBytes, linesToSkip, currentLineIdx, m_UiFilePtr->getSizeBytes()) || linesToSkip)
        return UINT64_MAX;

    m_CacheData.lineIdx = lineIdx;
    m_CacheData.lineStartOffsetBytes = currentOffsetBytes;
    m_CacheData.anchorIdx = anchorIdx;

    return currentOffsetBytes;
}

std::string_view LineIndexer::get(const uint64_t lineIdx, const uint64_t fromCol, const uint64_t colsNum, uint64_t& outLineTotalLength) const {
    const auto lineBytes = getLineBytes(lineIdx);
    if (lineBytes.empty())
        return {};

    const auto dataBegin = lineBytes.data();
    const auto dataEnd = dataBegin + lineBytes.size();

    outLineTotalLength = utf8::countCodepoints(dataBegin, dataEnd);
    if (outLineTotalLength == lineBytes.size()) {
        // bytes num == utf-8 codepoints num -> ascii
        if (fromCol < lineBytes.size())
            return {dataBegin + fromCol, static_cast<size_t>(std::min<uint64_t>(colsNum, lineBytes.size() - fromCol))};
        return {};
    }

    // walk by utf-8 codepoints
    const char* p = dataBegin;
    for (uint64_t c = 0; c < fromCol && p < dataEnd; ++c)
        p += utf8::seqLen(static_cast<unsigned char>(*p));
    if (p >= dataEnd)
        return {};

    const char* q = p;
    for (uint64_t c = 0; c < colsNum && q < dataEnd; ++c)
        q += utf8::seqLen(static_cast<unsigned char>(*q));
    if (q > dataEnd)
        q = dataEnd; // last sequence was cut by kMaxBytesPerLine

    return {p, static_cast<size_t>(q - p)};
}

bool LineIndexer::locate(const uint64_t byteOffset, uint64_t& outColIdx, uint64_t& outLineIdx) const {
    const size_t anchorsNum = m_AvailableAnchorsNum.load(std::memory_order_acquire);
    const uint64_t linesNum = m_LinesNum.load(std::memory_order_acquire);
    if (!anchorsNum || !linesNum)
        return false;

    const size_t anchorIdx = findAnchorByOffset(byteOffset, anchorsNum);
    if (anchorIdx == SIZE_MAX)
        return false;

    // count every line break between the anchor and the offset
    uint64_t lineIdx = m_Anchors[anchorIdx].lineIdx;
    uint64_t lineStartOffsetBytes = m_Anchors[anchorIdx].offsetBytes;
    uint64_t linesToSkip = UINT64_MAX;
    if (!walkLinesForward(lineStartOffsetBytes, linesToSkip, lineIdx, byteOffset))
        return false;
    if (lineIdx >= linesNum)
        return false; // the line is not there (yet)
    outLineIdx = lineIdx;

    const uint64_t bytesIntoLine = byteOffset - lineStartOffsetBytes;
    if (bytesIntoLine < kMaxBytesPerLine) {
        const auto lineHead = m_UiFilePtr->view(lineStartOffsetBytes, bytesIntoLine);
        if (lineHead.size() < bytesIntoLine)
            return false;
        outColIdx = utf8::countCodepoints(lineHead.data(), lineHead.data() + lineHead.size());
    }
    else {
        outColIdx = UINT64_MAX;
    }

    m_CacheData.lineIdx = lineIdx;
    m_CacheData.lineStartOffsetBytes = lineStartOffsetBytes;
    m_CacheData.anchorIdx = anchorIdx;

    return true;
}

void LineIndexer::getTextSize(uint64_t& maxColsNum, uint64_t& rowsNum) const {
    rowsNum = m_LinesNum.load(std::memory_order_acquire);
    maxColsNum = m_MaxLineLength.load(std::memory_order_relaxed);
}

uint64_t LineIndexer::getContentStartOffsetBytes() const {
    return m_AvailableAnchorsNum.load(std::memory_order_acquire) ? m_Anchors[0].offsetBytes : 0;
}

LineIndexer::LineIndexer(
    std::unique_ptr<FileReader>&& scanFilePtr, std::unique_ptr<FileMapping>&& uiFilePtr
) : m_ScanFilePtr(std::move(scanFilePtr)), m_UiFilePtr(std::move(uiFilePtr)) {}

size_t LineIndexer::findAnchorByLine(const uint64_t lineIdx, const size_t anchorsNum) const {
    auto searchBegin = m_Anchors.begin();
    auto searchEnd = m_Anchors.begin() + anchorsNum;

    // try to find nearest from cache
    if (m_CacheData.lineIdx != UINT64_MAX) {
        assert(m_CacheData.anchorIdx < anchorsNum);
        if (lineIdx >= m_CacheData.lineIdx) {
            if (m_CacheData.anchorIdx + 1 < anchorsNum && lineIdx < m_Anchors[m_CacheData.anchorIdx + 1].lineIdx)
                return m_CacheData.anchorIdx; // after the cached line, within its anchor
            searchBegin += m_CacheData.anchorIdx;
        }
        else if (lineIdx >= m_Anchors[m_CacheData.anchorIdx].lineIdx) {
            return m_CacheData.anchorIdx; // before the cached line, within its anchor
        }
        else {
            if (m_CacheData.anchorIdx > 0 && lineIdx >= m_Anchors[m_CacheData.anchorIdx - 1].lineIdx)
                return m_CacheData.anchorIdx - 1; // one anchor back
            searchEnd = m_Anchors.begin() + m_CacheData.anchorIdx;
        }
    }

    const auto it = std::upper_bound(
        searchBegin, searchEnd, lineIdx,
        [] (const uint64_t _lineIdx, const Anchor& anchor) {
            return _lineIdx < anchor.lineIdx;
        }
    );
    assert(it != m_Anchors.begin());
    return static_cast<size_t>(it - 1 - m_Anchors.begin());
}

size_t LineIndexer::findAnchorByOffset(const uint64_t byteOffset, const size_t anchorsNum) const {
    auto searchBegin = m_Anchors.begin();
    auto searchEnd = m_Anchors.begin() + anchorsNum;

    // try to find nearest from cache
    if (m_CacheData.lineIdx != UINT64_MAX) {
        assert(m_CacheData.anchorIdx < anchorsNum);
        if (byteOffset >= m_CacheData.lineStartOffsetBytes) {
            if (m_CacheData.anchorIdx + 1 < anchorsNum && byteOffset < m_Anchors[m_CacheData.anchorIdx + 1].offsetBytes)
                return m_CacheData.anchorIdx; // after the cached line, within its anchor
            searchBegin += m_CacheData.anchorIdx;
        }
        else if (byteOffset >= m_Anchors[m_CacheData.anchorIdx].offsetBytes) {
            return m_CacheData.anchorIdx; // before the cached line, within its anchor
        }
        else {
            if (m_CacheData.anchorIdx > 0 && byteOffset >= m_Anchors[m_CacheData.anchorIdx - 1].offsetBytes)
                return m_CacheData.anchorIdx - 1; // one anchor back
            searchEnd = m_Anchors.begin() + m_CacheData.anchorIdx;
        }
    }

    const auto it = std::upper_bound(
        searchBegin, searchEnd, byteOffset,
        [] (const uint64_t offset, const Anchor& anchor) {
            return offset < anchor.offsetBytes;
        }
    );
    if (it == m_Anchors.begin())
        return SIZE_MAX;
    return static_cast<size_t>(it - 1 - m_Anchors.begin());
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

bool LineIndexer::walkLinesForward(uint64_t& lineStartOffsetBytes, uint64_t& linesToSkip, uint64_t& lineIdx, const uint64_t limitOffsetBytes) const {
    uint64_t cursorBytes = lineStartOffsetBytes;
    while (linesToSkip > 0 && cursorBytes < limitOffsetBytes) {
        const auto numBytes = static_cast<size_t>(std::min<uint64_t>(kBytesPerAnchor, limitOffsetBytes - cursorBytes));
        const auto chunk = m_UiFilePtr->view(cursorBytes, numBytes);
        if (chunk.empty())
            return false;

        const auto chunkBegin = chunk.data();
        const auto chunkEnd = chunkBegin + chunk.size();

        auto chunkDataP = chunkBegin;
        while (linesToSkip > 0) {
            const auto newLinePtr = static_cast<const char*>(memchr(chunkDataP, '\n', static_cast<size_t>(chunkEnd - chunkDataP)));
            if (!newLinePtr) {
                chunkDataP = chunkEnd;
                break;
            }
            chunkDataP = newLinePtr + 1;
            lineStartOffsetBytes = cursorBytes + static_cast<uint64_t>(chunkDataP - chunkBegin);
            ++lineIdx;
            --linesToSkip;
        }

        cursorBytes += static_cast<uint64_t>(chunkDataP - chunkBegin);
    }
    return true;
}
