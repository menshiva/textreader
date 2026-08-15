#include "TextView.h"
#include <algorithm>
#include <charconv>
#include "../Ui.h"

TextView::TextView(Ui& parentUi) : m_ParentUi(parentUi) {}

void TextView::reset(Source source) {
    m_Source = std::move(source);
    for (const Axis axis : kAxes) {
        m_xyScrollData[axis].firstIdx = 0;
        m_xyScrollData[axis].pixelOffsetRemainder = 0.0f;
        m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();
        m_xyScrollData[axis].currentAnimationOpt.reset();
    }
    m_MaxVisibleLineLength = 0;

    m_CurrentSelectionOpt.reset();
    m_Selecting = false;
}

void TextView::draw() {
    if (!m_Source.getLine || !m_Source.getTextSize)
        return;

    ImGui::BeginChild(
        "##textview", ImVec2(0, 0),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    const auto windowRegionMin = ImGui::GetCursorScreenPos();
    const auto windowRegionSize = ImGui::GetContentRegionAvail();
    const ImVec2 fontSize{ImGui::GetFontBaked()->GetCharAdvance('0'), ImGui::GetTextLineHeight()};

    uint64_t sourceTextSize[kAxisCount] = {0, 0};
    m_Source.getTextSize(sourceTextSize[kAxisX], sourceTextSize[kAxisY]);

    LayoutData layoutData;
    if (!computeLayoutData(windowRegionMin, windowRegionSize, fontSize, sourceTextSize[kAxisX], sourceTextSize[kAxisY], layoutData)) {
        ImGui::EndChild();
        return;
    }

    updateAnimation(fontSize);

    for (const Axis axis : kAxes)
        clampScrollData(fontSize, layoutData, sourceTextSize[axis], axis);

    drawGutter(layoutData, fontSize, sourceTextSize[kAxisY]);
    drawScrollbars(layoutData, fontSize);
    drawText(layoutData, fontSize, sourceTextSize[kAxisY]);

    handleInput(layoutData, fontSize, sourceTextSize[kAxisY]);

    ImGui::EndChild();
}

void TextView::drawGutter(const LayoutData& layoutData, const ImVec2& fontSize, const uint64_t sourceLinesNum) const {
    const auto dl = ImGui::GetWindowDrawList();
    const auto& gutterRegion = layoutData.gutterData.region;

    const auto disabledTextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto separatorColor = ImGui::GetColorU32(ImGuiCol_Separator);

    dl->PushClipRect(gutterRegion.Min, gutterRegion.Max, true);
    for (uint64_t i = 0; i < layoutData.xyMetrics[kAxisY].reservedNum + 1; ++i) {
        const uint64_t lineNo = m_xyScrollData[kAxisY].firstIdx + i;
        if (lineNo >= sourceLinesNum)
            break;

        const float y = gutterRegion.Min.y + fontSize.y * static_cast<float>(i) - m_xyScrollData[kAxisY].pixelOffsetRemainder;

        char buf[24];
        const auto res = std::to_chars(buf, buf + sizeof(buf), lineNo + 1);
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
        const auto sbDataPtr = layoutData.xyScrollbarDataOpt[axis].has_value() ? &*layoutData.xyScrollbarDataOpt[axis] : nullptr;
        if (!sbDataPtr)
            continue;

        auto colorPtr = &idleColor;
        if (m_xyScrollData[axis].scrollbarDragOffsetOpt.has_value())
            colorPtr = &activeColor;
        else if (isWindowHovered && sbDataPtr->region.Contains(ImGui::GetIO().MousePos))
            colorPtr = &hoveredColor;

        // the slider is the whole track with the scrolled axis narrowed down to the grab range
        auto min = sbDataPtr->region.Min;
        auto max = sbDataPtr->region.Max;
        min[axis] = computeScrollbarGrab(fontSize, *sbDataPtr, axis);
        max[axis] = min[axis] + sbDataPtr->grabSize;

        dl->AddRectFilled(min, max, *colorPtr, kScrollbarSize * 0.5f);
    }
}

// draws only the lines that are on screen - this is the virtualization the assignment asks for
void TextView::drawText(const LayoutData& layoutData, const ImVec2& fontSize, const uint64_t sourceLinesNum) {
    m_MaxVisibleLineLength = 0;
    if (!layoutData.textViewDataOpt.has_value())
        return;

    const auto dl = ImGui::GetWindowDrawList();
    const auto& textViewRegion = layoutData.textViewDataOpt->region;

    const auto textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const auto selectionColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    TextPos selectionFrom, selectionTo;
    const bool hasSelection = getSelectionRange(selectionFrom, selectionTo);

    dl->PushClipRect(textViewRegion.Min, textViewRegion.Max, true);
    for (uint64_t i = 0; i < layoutData.xyMetrics[kAxisY].reservedNum + 1; ++i) {
        const uint64_t lineNo = m_xyScrollData[kAxisY].firstIdx + i;
        if (lineNo >= sourceLinesNum)
            break;

        uint64_t totalLineLength = 0;
        const auto s = m_Source.getLine(lineNo, m_xyScrollData[kAxisX].firstIdx, layoutData.xyMetrics[kAxisX].reservedNum + 1, totalLineLength);
        if (totalLineLength > m_MaxVisibleLineLength)
            m_MaxVisibleLineLength = totalLineLength;
        if (s.empty())
            continue;

        const float y = textViewRegion.Min.y + fontSize.y * static_cast<float>(i) - m_xyScrollData[kAxisY].pixelOffsetRemainder;
        const float lineStartX = textViewRegion.Min.x - m_xyScrollData[kAxisX].pixelOffsetRemainder;

        if (hasSelection && lineNo >= selectionFrom.line && lineNo <= selectionTo.line) {
            const uint64_t fromCol = lineNo == selectionFrom.line ? selectionFrom.col : 0;
            const uint64_t toCol = std::min<uint64_t>(lineNo == selectionTo.line ? selectionTo.col : totalLineLength, totalLineLength);
            if (fromCol < toCol) {
                const auto scrolledColsFlt = static_cast<float>(m_xyScrollData[kAxisX].firstIdx);
                dl->AddRectFilled(
                    ImVec2(lineStartX + fontSize.x * (static_cast<float>(fromCol) - scrolledColsFlt), y),
                    ImVec2(lineStartX + fontSize.x * (static_cast<float>(toCol) - scrolledColsFlt), y + fontSize.y),
                    selectionColor
                );
            }
        }

        dl->AddText(ImVec2(lineStartX, y), textColor, s.data(), s.data() + s.size());
    }
    dl->PopClipRect();
}

bool TextView::computeLayoutData(
    const ImVec2& windowRegionMin, const ImVec2& windowRegionSize, const ImVec2& fontSize,
    const uint64_t sourceMaxLineLength, const uint64_t sourceMaxLinesNum,
    LayoutData& outData
) const {
    if (windowRegionSize.x <= 0.0f || windowRegionSize.y <= 0.0f || fontSize.x <= 0.0f || fontSize.y <= 0.0f || !sourceMaxLinesNum)
        return false;

    static constexpr auto computeMetrics = [] (const float regionSize, const float _fontSize) {
        LayoutData::FontMetrics res;
        res.visibleNumFlt = regionSize / _fontSize;
        const float fullVisibleNumFlt = std::floor(res.visibleNumFlt);
        res.fullVisibleNum = static_cast<uint64_t>(fullVisibleNumFlt);
        res.remainder = res.visibleNumFlt - fullVisibleNumFlt;
        if (res.remainder < 0.001f)
            res.remainder = 0.0f;
        res.reservedNum = res.fullVisibleNum + static_cast<uint64_t>(res.remainder > 0.0f);
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
        const uint64_t lastVisibleLine = std::min<uint64_t>(sourceMaxLinesNum, m_xyScrollData[kAxisY].firstIdx + outData.xyMetrics[kAxisY].reservedNum);
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
        if (static_cast<double>(outData.xyMetrics[kAxisY].visibleNumFlt) < static_cast<double>(sourceMaxLinesNum))
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
        const ImRect& region, const float trackSize,
        const LayoutData::FontMetrics& fontMetrics, const uint64_t maxNum
    ) {
        LayoutData::ScrollbarData data{region};

        const double maxNumDbl = static_cast<double>(maxNum);
        const auto visibleNumDbl = static_cast<double>(fontMetrics.visibleNumFlt);

        data.maxPos = maxNumDbl - visibleNumDbl;

        const double visibleFraction = visibleNumDbl / maxNumDbl;
        data.grabSize = std::clamp(
            static_cast<float>(trackSize * visibleFraction),
            std::min(kScrollbarMinGrabSize, trackSize), trackSize
        );
        data.travel = trackSize - data.grabSize;

        return data;
    };

    outData.gutterData.region = ImRect(windowRegionMin, windowRegionMin + ImVec2(gutterWidth, availableRegionHeight));
    const auto windowRegionMax = windowRegionMin + windowRegionSize;

    if (horizontalScrollbarHeight != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMin.x, windowRegionMax.y - horizontalScrollbarHeight),
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMax.y)
        );
        outData.xyScrollbarDataOpt[kAxisX].emplace(computeScrollbarData(region, region.GetWidth(), outData.xyMetrics[kAxisX], sourceMaxLineLength));
    }
    if (verticalScrollbarWidth != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMin.y),
            ImVec2(windowRegionMax.x, windowRegionMax.y - horizontalScrollbarHeight)
        );
        outData.xyScrollbarDataOpt[kAxisY].emplace(computeScrollbarData(region, region.GetHeight(), outData.xyMetrics[kAxisY], sourceMaxLinesNum));
    }

    {
        auto& textViewData = outData.textViewDataOpt.emplace();
        textViewData.region.Min = ImVec2(outData.gutterData.region.Max.x + kTextViewLeftPadding, windowRegionMin.y);
        textViewData.region.Max = ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMax.y - horizontalScrollbarHeight);
        if (textViewData.region.Max.x <= textViewData.region.Min.x || textViewData.region.Max.y <= textViewData.region.Min.y)
            outData.textViewDataOpt.reset();
    }

    return true;
}

void TextView::clampScrollData(const ImVec2& fontSize, const LayoutData& layoutData, const uint64_t sourceMax, const Axis axis) {
    auto& scrollData = m_xyScrollData[axis];
    const auto& fontMetrics = layoutData.xyMetrics[axis];

    if (!sourceMax || !fontMetrics.reservedNum || sourceMax < fontMetrics.reservedNum) {
        scrollData.firstIdx = 0;
        scrollData.pixelOffsetRemainder = 0.0f;
        return;
    }

    const uint64_t maxFirstCharIdx = sourceMax - fontMetrics.reservedNum;
    const float maxPixelOffset = fontMetrics.remainder > 0.0f ? fontSize[axis] * (1.0f - fontMetrics.remainder) : 0.0f;

    if (scrollData.pixelOffsetRemainder < 0.0f)
        scrollData.pixelOffsetRemainder = 0.0f;

    if (scrollData.firstIdx > maxFirstCharIdx || (scrollData.firstIdx == maxFirstCharIdx && scrollData.pixelOffsetRemainder > maxPixelOffset)) {
        scrollData.firstIdx = maxFirstCharIdx;
        scrollData.pixelOffsetRemainder = maxPixelOffset;
    }
}

float TextView::computeScrollbarGrab(const ImVec2& fontSize, const LayoutData::ScrollbarData& scrollBarData, const Axis axis) const {
    double posFraction = 0.0;
    if (scrollBarData.maxPos > 0.0) {
        const auto& scrollData = m_xyScrollData[axis];
        const double currentScrollPos = static_cast<double>(scrollData.firstIdx) + static_cast<double>(scrollData.pixelOffsetRemainder / fontSize[axis]);
        posFraction = std::clamp(currentScrollPos / scrollBarData.maxPos, 0.0, 1.0);
    }
    return scrollBarData.region.Min[axis] + static_cast<float>(scrollBarData.travel * posFraction);
}

bool TextView::handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        for (const Axis axis : kAxes)
            m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();

    const LayoutData::ScrollbarData* sbPtrs[kAxisCount];
    for (const Axis axis : kAxes) {
        sbPtrs[axis] = layoutData.xyScrollbarDataOpt[axis].has_value() ? &*layoutData.xyScrollbarDataOpt[axis] : nullptr;
        if (!sbPtrs[axis])
            m_xyScrollData[axis].scrollbarDragOffsetOpt.reset();
    }

    const auto& io = ImGui::GetIO();

    const bool alreadyDragging = m_xyScrollData[kAxisX].scrollbarDragOffsetOpt.has_value() || m_xyScrollData[kAxisY].scrollbarDragOffsetOpt.has_value();
    if (!alreadyDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
        // drag start
        for (const Axis axis : kAxes) {
            const auto scrollBarDataPtr = sbPtrs[axis];
            if (scrollBarDataPtr && scrollBarDataPtr->region.Contains(io.MousePos)) {
                const float mousePos = io.MousePos[axis];
                const float grab = computeScrollbarGrab(fontSize, *scrollBarDataPtr, axis);

                auto& scrollData = m_xyScrollData[axis];
                if (mousePos >= grab && mousePos <= grab + scrollBarDataPtr->grabSize) {
                    // grabbed the slider itself
                    scrollData.scrollbarDragOffsetOpt = mousePos - grab;
                    scrollData.currentAnimationOpt.reset();
                }
                else {
                    // background click
                    const float travel = scrollBarDataPtr->travel;
                    if (travel > 0.0f) {
                        const float rel = std::clamp(
                            (mousePos - scrollBarDataPtr->grabSize * 0.5f - scrollBarDataPtr->region.Min[axis]) / travel,
                            0.0f, 1.0f
                        );
                        animateTo(fontSize, static_cast<double>(rel) * scrollBarDataPtr->maxPos, axis);
                    }
                    scrollData.scrollbarDragOffsetOpt = scrollBarDataPtr->grabSize * 0.5f;
                }

                break;
            }
        }
    }

    // process drag
    for (const Axis axis : kAxes) {
        const auto scrollBarDataPtr = sbPtrs[axis];
        auto& scrollData = m_xyScrollData[axis];
        if (scrollBarDataPtr && scrollData.scrollbarDragOffsetOpt.has_value()) {
            if (scrollData.currentAnimationOpt.has_value())
                return true;

            if (scrollBarDataPtr->travel > 0.0f) {
                const float rel = std::clamp(
                    (io.MousePos[axis] - *scrollData.scrollbarDragOffsetOpt - scrollBarDataPtr->region.Min[axis]) / scrollBarDataPtr->travel,
                    0.0f, 1.0f
                );
                setPos(fontSize, static_cast<double>(rel) * scrollBarDataPtr->maxPos, axis);
            }

            return true;
        }
    }

    return false;
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

void TextView::handleInput(const LayoutData& layoutData, const ImVec2& fontSize, const uint64_t sourceMaxLinesNum) {
    if (handleScrollbarInput(layoutData, fontSize))
        return;
    handleSelectionInput(layoutData, fontSize, sourceMaxLinesNum);
    handleWheelInput(fontSize);
    handleKeyboardInput(layoutData, fontSize, sourceMaxLinesNum);
}

void TextView::handleSelectionInput(const LayoutData& layoutData, const ImVec2& fontSize, const uint64_t sourceMaxLinesNum) {
    if (!layoutData.textViewDataOpt.has_value())
        return;

    const auto& textViewData = *layoutData.textViewDataOpt;
    const auto& region = textViewData.region;
    const auto& mousePos = ImGui::GetIO().MousePos;

    const bool isOverText = ImGui::IsWindowHovered() && region.Contains(mousePos);
    if (isOverText)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    if (isOverText && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto pos = getPosFromMouse(textViewData, fontSize, sourceMaxLinesNum);
        m_CurrentSelectionOpt = Selection{pos, pos};
        m_Selecting = true;
    }

    if (!m_Selecting)
        return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_Selecting = false;
        return;
    }

    // handle autoscroll
    {
        ImVec2 autoScroll(0.0f, 0.0f);
        if (mousePos.y < region.Min.y) autoScroll.y = (mousePos.y - region.Min.y) / fontSize.y;
        else if (mousePos.y > region.Max.y) autoScroll.y = (mousePos.y - region.Max.y) / fontSize.y;
        if (mousePos.x < region.Min.x) autoScroll.x = (mousePos.x - region.Min.x) / fontSize.x;
        else if (mousePos.x > region.Max.x) autoScroll.x = (mousePos.x - region.Max.x) / fontSize.x;

        for (const Axis axis : kAxes)
            if (autoScroll[axis] != 0.0f)
                scrollByPixels(fontSize, autoScroll * kSelectionAutoScrollSpeedModifier, axis);
    }

    if (m_CurrentSelectionOpt.has_value())
        m_CurrentSelectionOpt->stop = getPosFromMouse(textViewData, fontSize, sourceMaxLinesNum);
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

void TextView::handleKeyboardInput(const LayoutData& layoutData, const ImVec2& fontSize, const uint64_t sourceMaxLinesNum) {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive())
        return;

    const auto& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && sourceMaxLinesNum) {
        m_CurrentSelectionOpt = Selection{TextPos{0, 0}, TextPos{UINT64_MAX, sourceMaxLinesNum - 1}};
        m_Selecting = false;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        copySelection();

    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        animateTo(fontSize, 0.0, io.KeyCtrl ? kAxisY : kAxisX);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        if (!io.KeyCtrl) {
            // move to x=length of longest visible line
            const auto maxVisibleLineLengthDbl = static_cast<double>(m_MaxVisibleLineLength);
            const auto visibleDbl = static_cast<double>(layoutData.xyMetrics[kAxisX].visibleNumFlt);
            const double target = maxVisibleLineLengthDbl > visibleDbl ? maxVisibleLineLengthDbl - visibleDbl : 0.0;
            animateTo(fontSize, target, kAxisX);
        }
        else {
            // move to y=last line
            const double maxVerticalPos = sourceMaxLinesNum > layoutData.xyMetrics[kAxisY].reservedNum
                ? static_cast<double>(sourceMaxLinesNum) - static_cast<double>(layoutData.xyMetrics[kAxisY].visibleNumFlt)
                : 0.0;
            animateTo(fontSize, maxVerticalPos, kAxisY);
        }
    }

    const double pageLinesNumDbl = static_cast<double>(layoutData.xyMetrics[kAxisY].fullVisibleNum);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        animateTo(fontSize, getCurrentPos(fontSize, kAxisY) + pageLinesNumDbl, kAxisY);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        animateTo(fontSize, getCurrentPos(fontSize, kAxisY) - pageLinesNumDbl, kAxisY);
}

double TextView::getCurrentPos(const ImVec2 &fontSize, const Axis axis) const {
    return static_cast<double>(m_xyScrollData[axis].firstIdx) + static_cast<double>(m_xyScrollData[axis].pixelOffsetRemainder / fontSize[axis]);
}

void TextView::setPos(const ImVec2 &fontSize, const double pos, const Axis axis) {
    const double clamped = std::max(0.0, pos);
    const double whole = std::floor(clamped);
    m_xyScrollData[axis].firstIdx = static_cast<uint64_t>(whole);
    m_xyScrollData[axis].pixelOffsetRemainder = static_cast<float>((clamped - whole) * fontSize[axis]);
}

void TextView::updateAnimation(const ImVec2& fontSize) {
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    for (const Axis axis : kAxes) {
        auto& sd = m_xyScrollData[axis];
        if (!sd.currentAnimationOpt.has_value())
            continue;

        auto& anim = *sd.currentAnimationOpt;
        anim.elapsed += dt;

        const float t = std::clamp(anim.elapsed / kScrollAnimDuration, 0.0f, 1.0f);
        setPos(fontSize, anim.startPos + (anim.targetPos - anim.startPos) * static_cast<double>(t), axis);

        if (t >= 1.0f)
            sd.currentAnimationOpt.reset();
    }
}

void TextView::animateTo(const ImVec2 &fontSize, const double targetPos, const Axis axis) {
    auto& scrollData = m_xyScrollData[axis];

    const double startPos = getCurrentPos(fontSize, axis);
    const double clampedTarget = std::max(0.0, targetPos);
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
    const LayoutData::TextViewData& textViewData, const ImVec2& fontSize, const uint64_t sourceMaxLinesNum
) const {
    const auto& region = textViewData.region;
    const auto& mousePos = ImGui::GetIO().MousePos;

    TextPos res{m_xyScrollData[kAxisX].firstIdx, m_xyScrollData[kAxisY].firstIdx};

    const auto offset = (mousePos - region.Min + ImVec2(m_xyScrollData[kAxisX].pixelOffsetRemainder, m_xyScrollData[kAxisY].pixelOffsetRemainder)) / fontSize;
    if (offset.x > 0.0f)
        res.col += static_cast<uint64_t>(std::round(offset.x)); // round to col
    if (offset.y > 0.0f)
        res.line += static_cast<uint64_t>(offset.y); // truncate to line

    if (sourceMaxLinesNum && res.line >= sourceMaxLinesNum)
        res.line = sourceMaxLinesNum - 1;

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

    std::string text;
    for (uint64_t line = from.line; line <= to.line; ++line) {
        const uint64_t fromCol = line == from.line ? from.col : 0;
        const uint64_t colsNum = line == to.line ? to.col - fromCol : UINT64_MAX;
        uint64_t tmp;
        const auto s = m_Source.getLine(line, fromCol, colsNum, tmp);

        if (line != from.line)
            text += "\r\n";
        text.append(s);

        if (text.size() >= kMaxCopyBytes)
            break;
    }
    if (text.empty())
        return;

    ImGui::SetClipboardText(text.c_str());

    if (text.size() >= kMaxCopyBytes) {
        m_ParentUi.showInfoMsg({
            "The selection is too large to copy.\nCopied the first ~" + std::to_string(kMaxCopyMb) + " Mb.",
            nullptr
        });
    }
}
