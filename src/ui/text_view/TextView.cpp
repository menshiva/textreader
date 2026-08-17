#include "TextView.h"
#include <algorithm>
#include <charconv>
#include "imgui_internal.h"
#include "../../utils/Utf8.h"
#include "../Ui.h"

struct TextView::ScrollbarData {
    ImRect region;
    double maxPos;
    float grabSize;

    // how far the grab can slide along its track
    float travelFor(const Axis axis) const { return region.Max[axis] - region.Min[axis] - grabSize; }
};

struct TextView::LayoutData {
    struct FontMetrics {
        float visibleNumFlt;
        uint64_t fullVisibleNum;
        uint64_t maxDrawnNum;
    } xyMetrics[kAxisCount];

    uint64_t xySourceSize[kAxisCount];

    ImRect gutterRegion;

    std::optional<ScrollbarData> xyScrollbarDataOpt[kAxisCount];

    std::optional<ImRect> textViewRegionOpt;
};

TextView::TextView(Ui& parentUi) : m_ParentUi(parentUi) {}

void TextView::reset(const Source* sourcePtr) {
    m_SourcePtr = sourcePtr;
    for (const Axis axis : kAxes) {
        m_xyScrollData[axis].firstIdx = 0;
        m_xyScrollData[axis].pixelOffsetRemainder = 0.0f;
        m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();
        m_xyScrollData[axis].currentAnimationOpt.reset();
    }
    m_LastVisibleLinesNum = 0;
    m_MaxVisibleLineLength = 0;

    m_CurrentSelectionOpt.reset();
    m_Selecting = false;

    m_SearchNeedle.clear();
    m_SearchNeedleCols = 0;
    m_CurrentSearchMatchOpt.reset();
    m_PendingSearchScrollOpt.reset();
}

void TextView::draw() {
    if (!m_SourcePtr)
        return;

    ImGui::BeginChild(
        "##textview", ImVec2(0, 0),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    const auto windowRegionMin = ImGui::GetCursorScreenPos();
    const auto windowRegionSize = ImGui::GetContentRegionAvail();
    const ImVec2 fontSize{ImGui::GetFontBaked()->GetCharAdvance('0'), ImGui::GetTextLineHeight()};

    uint64_t sourceTextSize[kAxisCount] = {0, 0};
    m_SourcePtr->getTextSize(sourceTextSize[kAxisX], sourceTextSize[kAxisY]);

    LayoutData layoutData;
    if (!computeLayoutData(windowRegionMin, windowRegionSize, fontSize, sourceTextSize, layoutData)) {
        ImGui::EndChild();
        return;
    }
    m_LastVisibleLinesNum = layoutData.xyMetrics[kAxisY].fullVisibleNum;

    if (m_PendingSearchScrollOpt.has_value())
        applyPendingSearchScroll(layoutData, fontSize);

    updateAnimation(fontSize);

    // clamp scroll data
    for (const Axis axis : kAxes) {
        const double maxPos = computeMaxPos(layoutData, axis);
        if (getCurrentPos(fontSize, axis) > maxPos)
            setPos(fontSize, maxPos, axis);
    }

    drawGutter(layoutData, fontSize);
    drawScrollbars(layoutData, fontSize);
    drawText(layoutData, fontSize);

    handleInput(layoutData, fontSize);

    ImGui::EndChild();
}

void TextView::setSearchNeedle(std::string needle) {
    if (needle == m_SearchNeedle)
        return;
    m_SearchNeedle = std::move(needle);
    m_SearchNeedleCols = utf8::countCodepoints(m_SearchNeedle.data(), m_SearchNeedle.data() + m_SearchNeedle.size());
    m_CurrentSearchMatchOpt.reset();
}

void TextView::showMatch(const uint64_t colIdx, const uint64_t lineIdx) {
    m_PendingSearchScrollOpt = TextPos{colIdx, lineIdx};

    if (colIdx == UINT64_MAX) {
        m_CurrentSearchMatchOpt.reset();
        m_CurrentSelectionOpt.reset();
        return;
    }

    m_Selecting = false;
    m_CurrentSearchMatchOpt = TextPos{colIdx, lineIdx};
    m_CurrentSelectionOpt = Selection{TextPos{colIdx, lineIdx}, TextPos{colIdx + m_SearchNeedleCols, lineIdx}};
}

uint64_t TextView::getFirstWholeVisibleLineIdx() const {
    return getFirstWholeVisibleIdx(kAxisY);
}

bool TextView::isCurrentSearchMatchVisible() const {
    if (!m_CurrentSearchMatchOpt.has_value())
        return false;
    if (m_PendingSearchScrollOpt.has_value())
        return true;
    const uint64_t firstIdx = getFirstWholeVisibleIdx(kAxisY);
    return m_CurrentSearchMatchOpt->line >= firstIdx && m_CurrentSearchMatchOpt->line < firstIdx + m_LastVisibleLinesNum;
}

uint64_t TextView::getFirstWholeVisibleIdx(const Axis axis) const {
    const auto& scrollData = m_xyScrollData[axis];
    return scrollData.firstIdx + static_cast<uint64_t>(scrollData.pixelOffsetRemainder > 0.0f);
}

double TextView::computeMaxPos(const uint64_t sourceSize, const float visibleNumFlt) {
    return std::max(0.0, static_cast<double>(sourceSize) - static_cast<double>(visibleNumFlt));
}

double TextView::computeMaxPos(const LayoutData& layoutData, const Axis axis) {
    return computeMaxPos(layoutData.xySourceSize[axis], layoutData.xyMetrics[axis].visibleNumFlt);
}

bool TextView::computeLayoutData(
    const ImVec2& windowRegionMin, const ImVec2& windowRegionSize, const ImVec2& fontSize,
    const uint64_t (&sourceTextSize)[kAxisCount], LayoutData& outData
) const {
    const uint64_t sourceMaxLineLength = sourceTextSize[kAxisX];
    const uint64_t sourceLinesNum = sourceTextSize[kAxisY];
    if (windowRegionSize.x <= 0.0f || windowRegionSize.y <= 0.0f || fontSize.x <= 0.0f || fontSize.y <= 0.0f || !sourceLinesNum)
        return false;

    for (const Axis axis : kAxes)
        outData.xySourceSize[axis] = sourceTextSize[axis];

    static constexpr auto computeMetrics = [] (const float regionSize, const float _fontSize) {
        LayoutData::FontMetrics res;
        res.visibleNumFlt = regionSize / _fontSize;
        const float fullVisibleNumFlt = std::floor(res.visibleNumFlt);
        res.fullVisibleNum = static_cast<uint64_t>(fullVisibleNumFlt);
        res.maxDrawnNum = res.fullVisibleNum + static_cast<uint64_t>(res.visibleNumFlt - fullVisibleNumFlt > 0.001f) + 1;
        return res;
    };
    outData.xyMetrics[kAxisY] = computeMetrics(windowRegionSize.y, fontSize.y);

    const auto getGutterWidth = [&] {
        static constexpr uint64_t maxValueForMinDigitsNum = [] () -> uint64_t {
            uint64_t res = 10;
            for (uint64_t i = 1; i < kGutterMinDigitsNum; ++i)
                res *= 10;
            return res;
        }();

        // compute digits num to fit into gutter
        uint64_t gutterDigitsNum = kGutterMinDigitsNum;
        const uint64_t lastVisibleLine = std::min<uint64_t>(sourceLinesNum, m_xyScrollData[kAxisY].firstIdx + outData.xyMetrics[kAxisY].maxDrawnNum);
        if (lastVisibleLine >= maxValueForMinDigitsNum) {
            uint64_t n = lastVisibleLine;
            gutterDigitsNum = 1;
            while (n >= 10) {
                n /= 10;
                ++gutterDigitsNum;
            }
        }

        return kGutterTextHorizontalPadding * 2.0f + fontSize.x * static_cast<float>(gutterDigitsNum);
    };
    float gutterWidth = getGutterWidth();

    const auto getVerticalScrollbarWidth = [&] {
        if (static_cast<double>(outData.xyMetrics[kAxisY].visibleNumFlt) < static_cast<double>(sourceLinesNum))
            return kScrollbarSize;
        return 0.0f;
    };
    float verticalScrollbarWidth = getVerticalScrollbarWidth();

    const auto getTextViewWidth = [&] {
        return std::max(windowRegionSize.x - verticalScrollbarWidth - gutterWidth - kTextViewLeftPadding, 0.0f);
    };
    float textViewWidth = getTextViewWidth();

    outData.xyMetrics[kAxisX] = computeMetrics(textViewWidth, fontSize.x);

    float horizontalScrollbarHeight = 0.0f;
    float availableRegionHeight = windowRegionSize.y;
    if (sourceMaxLineLength && static_cast<double>(outData.xyMetrics[kAxisX].visibleNumFlt) < static_cast<double>(sourceMaxLineLength)) {
        // horizontal scrollbar appeared -> recompute all
        horizontalScrollbarHeight = kScrollbarSize;
        availableRegionHeight = std::max(availableRegionHeight - horizontalScrollbarHeight, 0.0f);
        outData.xyMetrics[kAxisY] = computeMetrics(availableRegionHeight, fontSize.y);
        gutterWidth = getGutterWidth();
        verticalScrollbarWidth = getVerticalScrollbarWidth();
        textViewWidth = getTextViewWidth();
        outData.xyMetrics[kAxisX] = computeMetrics(textViewWidth, fontSize.x);
    }

    static constexpr auto computeScrollbarData = [] (
        const ImRect& region, const Axis axis,
        const LayoutData::FontMetrics& fontMetrics, const uint64_t sourceSize
    ) {
        ScrollbarData data{region};
        data.maxPos = computeMaxPos(sourceSize, fontMetrics.visibleNumFlt);

        const float trackSize = region.Max[axis] - region.Min[axis];
        const double visibleFraction = static_cast<double>(fontMetrics.visibleNumFlt) / static_cast<double>(sourceSize);
        data.grabSize = std::clamp(
            static_cast<float>(trackSize * visibleFraction),
            std::min(kScrollbarMinGrabSize, trackSize), trackSize
        );

        return data;
    };

    outData.gutterRegion = ImRect(windowRegionMin, windowRegionMin + ImVec2(gutterWidth, availableRegionHeight));
    const auto windowRegionMax = windowRegionMin + windowRegionSize;

    if (horizontalScrollbarHeight != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMin.x, windowRegionMax.y - horizontalScrollbarHeight),
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMax.y)
        );
        outData.xyScrollbarDataOpt[kAxisX].emplace(computeScrollbarData(region, kAxisX, outData.xyMetrics[kAxisX], sourceTextSize[kAxisX]));
    }
    if (verticalScrollbarWidth != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMin.y),
            ImVec2(windowRegionMax.x, windowRegionMax.y - horizontalScrollbarHeight)
        );
        outData.xyScrollbarDataOpt[kAxisY].emplace(computeScrollbarData(region, kAxisY, outData.xyMetrics[kAxisY], sourceTextSize[kAxisY]));
    }

    {
        const ImRect textViewRegion(
            ImVec2(outData.gutterRegion.Max.x + kTextViewLeftPadding, windowRegionMin.y),
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMax.y - horizontalScrollbarHeight)
        );
        // a window narrow enough leaves no room for text at all
        if (textViewRegion.Max.x > textViewRegion.Min.x && textViewRegion.Max.y > textViewRegion.Min.y)
            outData.textViewRegionOpt = textViewRegion;
        else
            outData.textViewRegionOpt.reset();
    }

    return true;
}

float TextView::computeRowScreenY(const float regionMinY, const ImVec2& fontSize, const uint64_t rowIdx) const {
    return regionMinY + fontSize.y * static_cast<float>(rowIdx) - m_xyScrollData[kAxisY].pixelOffsetRemainder;
}

void TextView::drawGutter(const LayoutData& layoutData, const ImVec2& fontSize) const {
    const auto dl = ImGui::GetWindowDrawList();
    const auto& gutterRegion = layoutData.gutterRegion;

    const auto disabledTextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto separatorColor = ImGui::GetColorU32(ImGuiCol_Separator);

    dl->PushClipRect(gutterRegion.Min, gutterRegion.Max, true);
    for (uint64_t i = 0; i < layoutData.xyMetrics[kAxisY].maxDrawnNum; ++i) {
        const uint64_t lineIdx = m_xyScrollData[kAxisY].firstIdx + i;
        if (lineIdx >= layoutData.xySourceSize[kAxisY])
            break;

        const float y = computeRowScreenY(gutterRegion.Min.y, fontSize, i);

        char buf[24];
        const auto res = std::to_chars(buf, buf + sizeof(buf), lineIdx + 1);
        const int len = static_cast<int>(res.ptr - buf);
        const float numberWidth = fontSize.x * static_cast<float>(len);
        dl->AddText(
            ImVec2(gutterRegion.Max.x - kGutterTextHorizontalPadding - numberWidth, y),
            disabledTextColor, buf, buf + len
        );

        const float sepY = std::floor(y + fontSize.y) - 0.5f; // sub-pixel snap + -0.5f shifts line a little bit so that lines themselves look centered
        dl->AddLine(
            ImVec2(gutterRegion.Min.x, sepY), ImVec2(gutterRegion.Max.x, sepY),
            separatorColor, kGutterLineThickness
        );
    }
    dl->PopClipRect();

    // vertical line to the right of the gutter
    dl->AddLine(
        ImVec2(gutterRegion.Max.x, gutterRegion.Min.y), ImVec2(gutterRegion.Max.x, gutterRegion.Max.y),
        ImGui::GetColorU32(ImGuiCol_Border), kGutterLineThickness
    );
}

void TextView::drawScrollbars(const LayoutData& layoutData, const ImVec2& fontSize) const {
    const auto dl = ImGui::GetWindowDrawList();

    const auto idleColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);
    const auto hoveredColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabHovered);
    const auto activeColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive);

    const bool isWindowHovered = ImGui::IsWindowHovered();
    for (const Axis axis : kAxes) {
        const auto scrollbarDataPtr = layoutData.xyScrollbarDataOpt[axis].has_value() ? &*layoutData.xyScrollbarDataOpt[axis] : nullptr;
        if (!scrollbarDataPtr)
            continue;

        auto colorPtr = &idleColor;
        if (m_xyScrollData[axis].scrollbarDragOffsetOpt.has_value())
            colorPtr = &activeColor;
        else if (isWindowHovered && scrollbarDataPtr->region.Contains(ImGui::GetIO().MousePos))
            colorPtr = &hoveredColor;

        // the slider is the whole track with the scrolled axis narrowed down to the grab range
        auto min = scrollbarDataPtr->region.Min;
        auto max = scrollbarDataPtr->region.Max;
        min[axis] = computeScrollbarGrab(fontSize, *scrollbarDataPtr, axis);
        max[axis] = min[axis] + scrollbarDataPtr->grabSize;

        dl->AddRectFilled(min, max, *colorPtr, kScrollbarSize * 0.5f);
    }
}

// draws only the lines that are on screen
void TextView::drawText(const LayoutData& layoutData, const ImVec2& fontSize) {
    m_MaxVisibleLineLength = 0;
    if (!layoutData.textViewRegionOpt.has_value())
        return;

    const auto dl = ImGui::GetWindowDrawList();
    const auto& textViewRegion = *layoutData.textViewRegionOpt;

    const auto textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const auto selectionColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    const auto searchMatchColor = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    const auto searchCurrentMatchColor = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);

    TextPos selectionFrom, selectionTo;
    const bool hasSelection = getSelectionRange(selectionFrom, selectionTo);

    const uint64_t firstColIdx = m_xyScrollData[kAxisX].firstIdx;
    const auto columnX = [&, firstColX = textViewRegion.Min.x - m_xyScrollData[kAxisX].pixelOffsetRemainder] (const uint64_t colIdx) {
        return colIdx >= firstColIdx
            ? firstColX + fontSize.x * static_cast<float>(colIdx - firstColIdx)
            : firstColX - fontSize.x * static_cast<float>(firstColIdx - colIdx);
    };

    const uint64_t overscanCols = m_SearchNeedleCols ? std::min(firstColIdx, m_SearchNeedleCols - 1) : 0;
    const uint64_t fetchFromCol = firstColIdx - overscanCols;
    const uint64_t fetchColsNum = layoutData.xyMetrics[kAxisX].maxDrawnNum + overscanCols;

    dl->PushClipRect(textViewRegion.Min, textViewRegion.Max, true);
    for (uint64_t i = 0; i < layoutData.xyMetrics[kAxisY].maxDrawnNum; ++i) {
        const uint64_t lineIdx = m_xyScrollData[kAxisY].firstIdx + i;
        if (lineIdx >= layoutData.xySourceSize[kAxisY])
            break;

        uint64_t totalLineLength = 0;
        const auto lineText = m_SourcePtr->getLine(lineIdx, fetchFromCol, fetchColsNum, totalLineLength);
        if (totalLineLength > m_MaxVisibleLineLength)
            m_MaxVisibleLineLength = totalLineLength;
        if (lineText.empty())
            continue;

        const float y = computeRowScreenY(textViewRegion.Min.y, fontSize, i);

        // draw selection
        if (hasSelection && lineIdx >= selectionFrom.line && lineIdx <= selectionTo.line) {
            const uint64_t fromCol = lineIdx == selectionFrom.line ? selectionFrom.col : 0;
            const uint64_t toCol = std::min<uint64_t>(lineIdx == selectionTo.line ? selectionTo.col : totalLineLength, totalLineLength);
            if (fromCol < toCol) {
                dl->AddRectFilled(
                    ImVec2(columnX(fromCol), y), ImVec2(columnX(toCol), y + fontSize.y),
                    selectionColor
                );
            }
        }

        // draw search matches
        if (!m_SearchNeedle.empty()) {
            // matches come out in order -> each column is counted from the previous match
            size_t countedBytes = 0;
            uint64_t countedCols = 0;
            for (size_t pos = 0; (pos = lineText.find(m_SearchNeedle, pos)) != std::string_view::npos; ++pos) {
                countedCols += utf8::countCodepoints(lineText.data() + countedBytes, lineText.data() + pos);
                countedBytes = pos;
                const uint64_t startCol = fetchFromCol + countedCols;
                dl->AddRectFilled(
                    ImVec2(columnX(startCol), y), ImVec2(columnX(startCol + m_SearchNeedleCols), y + fontSize.y),
                    m_CurrentSearchMatchOpt.has_value() && m_CurrentSearchMatchOpt->line == lineIdx && m_CurrentSearchMatchOpt->col == startCol
                        ? searchCurrentMatchColor
                        : searchMatchColor
                );
            }
        }

        // draw text itself
        dl->AddText(ImVec2(columnX(fetchFromCol), y), textColor, lineText.data(), lineText.data() + lineText.size());
    }
    dl->PopClipRect();
}

float TextView::computeScrollbarGrab(const ImVec2& fontSize, const ScrollbarData& scrollBarData, const Axis axis) const {
    double posFraction = 0.0;
    if (scrollBarData.maxPos > 0.0) {
        const auto& scrollData = m_xyScrollData[axis];
        const double currentScrollPos = static_cast<double>(scrollData.firstIdx) + static_cast<double>(scrollData.pixelOffsetRemainder / fontSize[axis]);
        posFraction = std::clamp(currentScrollPos / scrollBarData.maxPos, 0.0, 1.0);
    }
    return scrollBarData.region.Min[axis] + static_cast<float>(static_cast<double>(scrollBarData.travelFor(axis)) * posFraction);
}

void TextView::scrollByPixels(const ImVec2& fontSize, const ImVec2& delta, const Axis axis) {
    auto& scrollData = m_xyScrollData[axis];
    scrollData.currentAnimationOpt.reset();

    const float fontSizeVal = fontSize[axis];
    scrollData.pixelOffsetRemainder += delta[axis] * fontSizeVal;

    const float deltaUnits = std::floor(scrollData.pixelOffsetRemainder / fontSizeVal);
    scrollData.pixelOffsetRemainder -= deltaUnits * fontSizeVal;

    if (deltaUnits >= 0.0f) {
        scrollData.firstIdx += static_cast<uint64_t>(deltaUnits);
        return;
    }

    const auto step = static_cast<uint64_t>(-deltaUnits);
    if (step >= scrollData.firstIdx) {
        scrollData.firstIdx = 0;
        scrollData.pixelOffsetRemainder = 0.0f;
    }
    else {
        scrollData.firstIdx -= step;
    }
}

void TextView::handleInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (handleScrollbarInput(layoutData, fontSize))
        return;
    handleSelectionInput(layoutData, fontSize);
    handleWheelInput(fontSize);
    handleKeyboardInput(layoutData, fontSize);
}

bool TextView::handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        for (const Axis axis : kAxes)
            m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();

    const ScrollbarData* scrollbarDataPtrs[kAxisCount];
    for (const Axis axis : kAxes) {
        scrollbarDataPtrs[axis] = layoutData.xyScrollbarDataOpt[axis].has_value() ? &*layoutData.xyScrollbarDataOpt[axis] : nullptr;
        if (!scrollbarDataPtrs[axis])
            m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();
    }

    const auto& io = ImGui::GetIO();

    const bool alreadyDragging = m_xyScrollData[kAxisX].scrollbarDragOffsetOpt.has_value() || m_xyScrollData[kAxisY].scrollbarDragOffsetOpt.has_value();
    if (!alreadyDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
        // drag start
        for (const Axis axis : kAxes) {
            const auto scrollbarDataPtr = scrollbarDataPtrs[axis];
            if (scrollbarDataPtr && scrollbarDataPtr->region.Contains(io.MousePos)) {
                const float mousePos = io.MousePos[axis];
                const float grab = computeScrollbarGrab(fontSize, *scrollbarDataPtr, axis);

                auto& scrollData = m_xyScrollData[axis];
                if (mousePos >= grab && mousePos <= grab + scrollbarDataPtr->grabSize) {
                    // grabbed the slider itself
                    scrollData.scrollbarDragOffsetOpt = mousePos - grab;
                    scrollData.currentAnimationOpt.reset();
                }
                else {
                    // background click
                    const float travel = scrollbarDataPtr->travelFor(axis);
                    if (travel > 0.0f) {
                        const float rel = std::clamp(
                            (mousePos - scrollbarDataPtr->grabSize * 0.5f - scrollbarDataPtr->region.Min[axis]) / travel,
                            0.0f, 1.0f
                        );
                        animateTo(fontSize, layoutData, static_cast<double>(rel) * scrollbarDataPtr->maxPos, axis);
                    }
                    scrollData.scrollbarDragOffsetOpt = scrollbarDataPtr->grabSize * 0.5f;
                }

                break;
            }
        }
    }

    // process drag
    for (const Axis axis : kAxes) {
        const auto scrollbarDataPtr = scrollbarDataPtrs[axis];
        auto& scrollData = m_xyScrollData[axis];
        if (scrollbarDataPtr && scrollData.scrollbarDragOffsetOpt.has_value()) {
            if (scrollData.currentAnimationOpt.has_value())
                return true; // background click is still animating

            const float travel = scrollbarDataPtr->travelFor(axis);
            if (travel > 0.0f) {
                const float rel = std::clamp(
                    (io.MousePos[axis] - *scrollData.scrollbarDragOffsetOpt - scrollbarDataPtr->region.Min[axis]) / travel,
                    0.0f, 1.0f
                );
                setPos(fontSize, static_cast<double>(rel) * scrollbarDataPtr->maxPos, axis);
            }

            return true;
        }
    }

    return false;
}

void TextView::handleSelectionInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (!layoutData.textViewRegionOpt.has_value())
        return;

    const auto& region = *layoutData.textViewRegionOpt;
    const auto& mousePos = ImGui::GetIO().MousePos;
    const uint64_t sourceLinesNum = layoutData.xySourceSize[kAxisY];

    const bool isOverText = ImGui::IsWindowHovered() && region.Contains(mousePos);
    if (isOverText)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    if (isOverText && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto pos = getPosFromMouse(region, fontSize, sourceLinesNum);
        m_CurrentSelectionOpt = Selection{pos, pos};
        m_Selecting = true;
    }

    if (!m_Selecting)
        return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_Selecting = false;
        return;
    }

    // handle autoscroll - the further out the faster
    {
        ImVec2 autoScroll(0.0f, 0.0f);
        for (const Axis axis : kAxes) {
            if (mousePos[axis] < region.Min[axis])
                autoScroll[axis] = (mousePos[axis] - region.Min[axis]) / fontSize[axis];
            else if (mousePos[axis] > region.Max[axis])
                autoScroll[axis] = (mousePos[axis] - region.Max[axis]) / fontSize[axis];

            if (autoScroll[axis] != 0.0f)
                scrollByPixels(fontSize, autoScroll * kSelectionAutoScrollSpeedModifier, axis);
        }
    }

    if (m_CurrentSelectionOpt.has_value())
        m_CurrentSelectionOpt->stop = getPosFromMouse(region, fontSize, sourceLinesNum);
}

void TextView::handleWheelInput(const ImVec2& fontSize) {
    if (!ImGui::IsWindowHovered())
        return;

    ImVec2 delta(0.0f, 0.0f);

    const auto& io = ImGui::GetIO();
    if (io.MouseWheelH != 0.0f)
        delta.x = -io.MouseWheelH * kHorizontalCharsPerWheelScroll;
    if (io.MouseWheel != 0.0f) {
        if (!io.KeyShift)
            delta.y = -io.MouseWheel * kVerticalLinesPerWheelScroll;
        else if (delta.x == 0.0f)
            delta.x = -io.MouseWheel * kHorizontalCharsPerWheelScroll;
    }

    for (const Axis axis : kAxes)
        if (delta[axis] != 0.0f)
            scrollByPixels(fontSize, delta, axis);
}

void TextView::handleKeyboardInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive())
        return;

    const auto& io = ImGui::GetIO();
    const uint64_t sourceLinesNum = layoutData.xySourceSize[kAxisY];

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && sourceLinesNum) {
        m_CurrentSelectionOpt = Selection{TextPos{0, 0}, TextPos{UINT64_MAX, sourceLinesNum - 1}};
        m_Selecting = false;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        copySelection();

    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        animateTo(fontSize, layoutData, 0.0, io.KeyCtrl ? kAxisY : kAxisX);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        if (!io.KeyCtrl) {
            // move to x=length of longest visible line
            const auto maxVisibleLineLengthDbl = static_cast<double>(m_MaxVisibleLineLength);
            const auto visibleDbl = static_cast<double>(layoutData.xyMetrics[kAxisX].visibleNumFlt);
            animateTo(fontSize, layoutData, maxVisibleLineLengthDbl - visibleDbl, kAxisX);
        }
        else {
            // move to y=last line
            animateTo(fontSize, layoutData, computeMaxPos(layoutData, kAxisY), kAxisY);
        }
    }

    const double pageLinesNumDbl = static_cast<double>(layoutData.xyMetrics[kAxisY].fullVisibleNum);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        animateTo(fontSize, layoutData, getCurrentPos(fontSize, kAxisY) + pageLinesNumDbl, kAxisY);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        animateTo(fontSize, layoutData, getCurrentPos(fontSize, kAxisY) - pageLinesNumDbl, kAxisY);
}

double TextView::getCurrentPos(const ImVec2& fontSize, const Axis axis) const {
    return static_cast<double>(m_xyScrollData[axis].firstIdx) + static_cast<double>(m_xyScrollData[axis].pixelOffsetRemainder / fontSize[axis]);
}

void TextView::setPos(const ImVec2& fontSize, const double pos, const Axis axis) {
    const double clamped = std::max(0.0, pos);
    const double whole = std::floor(clamped);
    m_xyScrollData[axis].firstIdx = static_cast<uint64_t>(whole);
    m_xyScrollData[axis].pixelOffsetRemainder = static_cast<float>((clamped - whole) * fontSize[axis]);
}

void TextView::updateAnimation(const ImVec2& fontSize) {
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    for (const Axis axis : kAxes) {
        auto& scrollData = m_xyScrollData[axis];
        if (!scrollData.currentAnimationOpt.has_value())
            continue;

        auto& anim = *scrollData.currentAnimationOpt;
        anim.elapsed += dt;

        const float t = std::clamp(anim.elapsed / kScrollAnimDuration, 0.0f, 1.0f);
        setPos(fontSize, anim.startPos + (anim.targetPos - anim.startPos) * static_cast<double>(t), axis);

        if (t >= 1.0f)
            scrollData.currentAnimationOpt.reset();
    }
}

void TextView::animateTo(const ImVec2& fontSize, const LayoutData& layoutData, const double targetPos, const Axis axis) {
    auto& scrollData = m_xyScrollData[axis];

    const double startPos = getCurrentPos(fontSize, axis);
    const double clampedTarget = std::clamp(targetPos, 0.0, computeMaxPos(layoutData, axis));
    if (std::abs(clampedTarget - startPos) < 0.5) {
        scrollData.currentAnimationOpt.reset();
        setPos(fontSize, clampedTarget, axis);
        return;
    }

    auto& anim = scrollData.currentAnimationOpt.emplace();
    anim.startPos = startPos;
    anim.targetPos = clampedTarget;
    anim.elapsed = 0.0f;
}

TextView::TextPos TextView::getPosFromMouse(
    const ImRect& textViewRegion, const ImVec2& fontSize, const uint64_t sourceLinesNum
) const {
    const auto& region = textViewRegion;
    const auto& mousePos = ImGui::GetIO().MousePos;

    TextPos res{m_xyScrollData[kAxisX].firstIdx, m_xyScrollData[kAxisY].firstIdx};

    const auto offset = (mousePos - region.Min + ImVec2(m_xyScrollData[kAxisX].pixelOffsetRemainder, m_xyScrollData[kAxisY].pixelOffsetRemainder)) / fontSize;
    if (offset.x > 0.0f)
        res.col += static_cast<uint64_t>(std::round(offset.x)); // round to col
    if (offset.y > 0.0f)
        res.line += static_cast<uint64_t>(offset.y); // truncate to line

    if (sourceLinesNum && res.line >= sourceLinesNum)
        res.line = sourceLinesNum - 1;

    return res;
}

bool TextView::getSelectionRange(TextPos& outFrom, TextPos& outTo) const {
    if (!m_CurrentSelectionOpt.has_value())
        return false;

    outFrom = m_CurrentSelectionOpt->start;
    outTo = m_CurrentSelectionOpt->stop;

    if (outFrom.line > outTo.line || (outFrom.line == outTo.line && outFrom.col > outTo.col))
        std::swap(outFrom, outTo);

    return outFrom.line != outTo.line || outFrom.col != outTo.col;
}

void TextView::copySelection() const {
    static constexpr size_t kMaxCopyBytes = kMaxCopyMb << 20;

    TextPos from, to;
    if (!getSelectionRange(from, to))
        return;

    const uint64_t lastLineIdx = std::min(to.line, from.line + kMaxCopyLines - 1);

    std::string text;
    bool truncated = lastLineIdx < to.line;
    uint64_t copiedLines = 0;
    for (uint64_t lineIdx = from.line; lineIdx <= lastLineIdx; ++lineIdx) {
        const uint64_t fromCol = lineIdx == from.line ? from.col : 0;
        const uint64_t colsNum = lineIdx == to.line ? to.col - fromCol : UINT64_MAX;
        uint64_t tmp = 0;
        const auto lineText = m_SourcePtr->getLine(lineIdx, fromCol, colsNum, tmp);

        if (lineIdx != from.line)
            text += "\r\n";
        text.append(lineText);
        ++copiedLines;

        if (text.size() >= kMaxCopyBytes) {
            truncated = truncated || lineIdx < to.line;
            break;
        }
    }
    if (text.empty())
        return;

    ImGui::SetClipboardText(text.c_str());

    if (truncated) {
        m_ParentUi.showMessage({
            "The selection is too large to copy.\nCopied the first " + std::to_string(copiedLines)
                + " lines (" + std::to_string(text.size() >> 20) + " Mb).",
            nullptr
        });
    }
}

static double computeCenter(const uint64_t idx, const uint64_t visibleNum) {
    return static_cast<double>(idx) - std::floor(static_cast<double>(visibleNum) * 0.5);
}

void TextView::applyPendingSearchScroll(const LayoutData& layoutData, const ImVec2& fontSize) {
    const auto target = *m_PendingSearchScrollOpt;
    m_PendingSearchScrollOpt.reset();

    const uint64_t firstLineIdx = getFirstWholeVisibleIdx(kAxisY);
    const uint64_t visibleLinesNum = layoutData.xyMetrics[kAxisY].fullVisibleNum;
    if (target.line < firstLineIdx || target.line >= firstLineIdx + visibleLinesNum)
        animateTo(fontSize, layoutData, computeCenter(target.line, visibleLinesNum), kAxisY);

    if (target.col == UINT64_MAX)
        return;

    const uint64_t firstColIdx = getFirstWholeVisibleIdx(kAxisX);
    const uint64_t visibleColsNum = layoutData.xyMetrics[kAxisX].fullVisibleNum;
    if (target.col < firstColIdx || target.col + m_SearchNeedleCols > firstColIdx + visibleColsNum)
        animateTo(fontSize, layoutData, computeCenter(target.col, visibleColsNum), kAxisX);
}
