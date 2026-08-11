#include "TextView.h"
#include <algorithm>
#include <charconv>
#include "imgui_internal.h"

void TextView::reset(Source source) {
    m_Source = std::move(source);
    for (int i = 0; i < 2; ++i) {
        m_xyScrollData[i].firstIdx = 0;
        m_xyScrollData[i].pixelOffsetRemainder = 0.0f;
        m_xyScrollData[i].scrollbarDragOffsetOpt.reset();
        m_xyScrollData[i].currentAnimationOpt.reset();
    }
    m_MaxVisibleLineLength = 0;
}

void TextView::draw() {
    if (!m_Source.getLine)
        return;

    ImGui::BeginChild(
        "##textview", ImVec2(0, 0),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    const auto windowRegionMin = ImGui::GetCursorScreenPos();
    const auto windowRegionSize = ImGui::GetContentRegionAvail();
    const ImVec2 fontSize{ImGui::GetFontBaked()->GetCharAdvance('0'), ImGui::GetTextLineHeight()};

    LayoutData layoutData;
    if (!computeLayoutData(windowRegionMin, windowRegionSize, fontSize, m_Source.maxNum[0], m_Source.maxNum[1], layoutData)) {
        ImGui::EndChild();
        return;
    }

    updateAnimation(fontSize);
    for (int i = 0; i < 2; ++i)
        clampScrollData(fontSize, layoutData, m_Source.maxNum[i], i);

    const auto dl = ImGui::GetWindowDrawList();

    const auto textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const auto disabledTextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto separatorCol = ImGui::GetColorU32(ImGuiCol_Separator);

    const auto scrollbarActiveColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive);
    const auto scrollbarHoveredColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabHovered);
    const auto scrollbarColor = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);

    // gutter
    {
        const auto& gutterRegion = layoutData.gutterData.region;

        dl->PushClipRect(gutterRegion.Min, gutterRegion.Max, true);
        for (uint64_t i = 0; i < layoutData.xyMetrics[1].reservedNum + 1; ++i) {
            const uint64_t lineNo = m_xyScrollData[1].firstIdx + i;
            if (lineNo >= m_Source.maxNum[1])
                break;

            const float y = std::floor(gutterRegion.Min.y + fontSize.y * static_cast<float>(i) - m_xyScrollData[1].pixelOffsetRemainder);

            char buf[24];
            const auto res = std::to_chars(buf, buf + sizeof(buf), lineNo + 1);
            const int len = static_cast<int>(res.ptr - buf);
            const float numberWidth = fontSize.x * static_cast<float>(len);
            dl->AddText(
                ImVec2(gutterRegion.Max.x - kGutterTextHorizontalPadding - numberWidth, y),
                disabledTextColor, buf, buf + len
            );

            const float sepY = std::floor(y + fontSize.y) - 0.5f; // -0.5f shifts line a little bit so that lines themselves look centered
            dl->AddLine(
                ImVec2(gutterRegion.Min.x, sepY), ImVec2(gutterRegion.Max.x, sepY),
                separatorCol, kGutterLineThickness
            );
        }
        dl->PopClipRect();

        // vertical line to the right of the gutter
        dl->AddLine(
            ImVec2(gutterRegion.Max.x, gutterRegion.Min.y), ImVec2(gutterRegion.Max.x, gutterRegion.Max.y),
            ImGui::GetColorU32(ImGuiCol_Border), kGutterLineThickness
        );
    }

    // scrollbars
    for (int i = 0; i < 2; ++i) {
        if (const auto sbDataPtr = layoutData.xyScrollbarDataOpt[i].has_value() ? &layoutData.xyScrollbarDataOpt[i].value() : nullptr) {
            const auto& scrollData = m_xyScrollData[i];

            auto colorPtr = &scrollbarColor;
            if (scrollData.scrollbarDragOffsetOpt.has_value())
                colorPtr = &scrollbarActiveColor;
            else if (ImGui::IsWindowHovered() && sbDataPtr->region.Contains(ImGui::GetIO().MousePos))
                colorPtr = &scrollbarHoveredColor;

            const float grab = computeScrollbarGrab(fontSize, *sbDataPtr, i);
            if (i == 0) {
                dl->AddRectFilled(
                    ImVec2(grab, sbDataPtr->region.Min.y), ImVec2(grab + sbDataPtr->grab, sbDataPtr->region.Max.y),
                    *colorPtr, kScrollbarSize * 0.5f
                );
            }
            else {
                dl->AddRectFilled(
                    ImVec2(sbDataPtr->region.Min.x, grab), ImVec2(sbDataPtr->region.Max.x, grab + sbDataPtr->grab),
                    *colorPtr, kScrollbarSize * 0.5f
                );
            }
        }
    }

    // text view
    m_MaxVisibleLineLength = 0;
    if (const auto tvDataPtr = layoutData.textViewDataOpt.has_value() ? &layoutData.textViewDataOpt.value() : nullptr) {
        const auto& textViewRegion = tvDataPtr->region;
        dl->PushClipRect(textViewRegion.Min,textViewRegion.Max, true);
        for (uint64_t i = 0; i < layoutData.xyMetrics[1].reservedNum + 1; ++i) {
            const uint64_t lineNo = m_xyScrollData[1].firstIdx + i;
            if (lineNo >= m_Source.maxNum[1])
                break;

            const float y = std::floor(textViewRegion.Min.y + fontSize.y * static_cast<float>(i) - m_xyScrollData[1].pixelOffsetRemainder);

            uint64_t totalLineLength = 0;
            const auto s = m_Source.getLine(lineNo, m_xyScrollData[0].firstIdx, layoutData.xyMetrics[0].reservedNum + 1, totalLineLength);
            if (totalLineLength > m_MaxVisibleLineLength)
                m_MaxVisibleLineLength = totalLineLength;

            dl->AddText(ImVec2(textViewRegion.Min.x - m_xyScrollData[0].pixelOffsetRemainder, y), textColor, s.data(), s.data() + s.size());
        }
        dl->PopClipRect();
    }

    handleInput(layoutData, fontSize);

    ImGui::EndChild();
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
    outData.xyMetrics[1] = computeMetrics(windowRegionSize.y, fontSize.y);

    const auto getGutterWidth = [&] {
        static constexpr uint64_t maxValueForMinDigitsNum = [] () -> uint64_t {
            uint64_t res = 10;
            for (uint64_t i = 1; i < kGutterMinDigitsNum; ++i)
                res *= 10;
            return res;
        }();

        // compute digits num to fit into gutter
        uint64_t gutterDigitsNum = kGutterMinDigitsNum;
        const uint64_t lastVisibleLine = std::min<uint64_t>(sourceMaxLinesNum, m_xyScrollData[1].firstIdx + outData.xyMetrics[1].reservedNum);
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
        if (static_cast<double>(outData.xyMetrics[1].visibleNumFlt) < static_cast<double>(sourceMaxLinesNum))
            return kScrollbarSize;
        return 0.0f;
    };
    float verticalScrollbarWidth = getVerticalScrollbarWidth();

    const auto getTextViewWidth = [&] {
        return std::max(windowRegionSize.x - verticalScrollbarWidth - gutterWidth - kTextViewLeftPadding, 0.0f);
    };
    float textViewWidth = getTextViewWidth();

    outData.xyMetrics[0] = computeMetrics(textViewWidth, fontSize.x);

    float horizontalScrollbarHeight = 0.0f;
    float availableRegionHeight = windowRegionSize.y;
    if (sourceMaxLineLength && static_cast<double>(outData.xyMetrics[0].visibleNumFlt) < static_cast<double>(sourceMaxLineLength)) {
        // horizontal scrollbar appeared -> recompute all
        horizontalScrollbarHeight = kScrollbarSize;
        availableRegionHeight = std::max(availableRegionHeight - horizontalScrollbarHeight, 0.0f);
        outData.xyMetrics[1] = computeMetrics(availableRegionHeight, fontSize.y);
        gutterWidth = getGutterWidth();
        verticalScrollbarWidth = getVerticalScrollbarWidth();
        textViewWidth = getTextViewWidth();
        outData.xyMetrics[0] = computeMetrics(textViewWidth, fontSize.x);
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
        data.grab = std::clamp(
            static_cast<float>(trackSize * visibleFraction),
            std::min(kScrollbarMinGrabSize, trackSize), trackSize
        );
        data.travel = trackSize - data.grab;

        return data;
    };

    outData.gutterData.region = ImRect(windowRegionMin, windowRegionMin + ImVec2(gutterWidth, availableRegionHeight));
    const auto windowRegionMax = windowRegionMin + windowRegionSize;

    if (horizontalScrollbarHeight != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMin.x, windowRegionMax.y - horizontalScrollbarHeight),
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMax.y)
        );
        outData.xyScrollbarDataOpt[0].emplace(computeScrollbarData(region, region.GetWidth(), outData.xyMetrics[0], sourceMaxLineLength));
    }
    if (verticalScrollbarWidth != 0.0f) {
        const ImRect region(
            ImVec2(windowRegionMax.x - verticalScrollbarWidth, windowRegionMin.y),
            ImVec2(windowRegionMax.x, windowRegionMax.y - horizontalScrollbarHeight)
        );
        outData.xyScrollbarDataOpt[1].emplace(computeScrollbarData(region, region.GetHeight(), outData.xyMetrics[1], sourceMaxLinesNum));
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

void TextView::clampScrollData(const ImVec2& fontSize, const LayoutData& layoutData, const uint64_t sourceMax, const int idx) {
    auto& scrollData = m_xyScrollData[idx];
    const auto& fontMetrics = layoutData.xyMetrics[idx];

    if (!sourceMax || !fontMetrics.reservedNum || sourceMax < fontMetrics.reservedNum) {
        scrollData.firstIdx = 0;
        scrollData.pixelOffsetRemainder = 0.0f;
        return;
    }

    const uint64_t maxFirstCharIdx = sourceMax - fontMetrics.reservedNum;
    const float maxPixelOffset = fontMetrics.remainder > 0.0f ? fontSize[idx] * (1.0f - fontMetrics.remainder) : 0.0f;

    if (scrollData.pixelOffsetRemainder < 0.0f)
        scrollData.pixelOffsetRemainder = 0.0f;

    if (scrollData.firstIdx > maxFirstCharIdx || (scrollData.firstIdx == maxFirstCharIdx && scrollData.pixelOffsetRemainder > maxPixelOffset)) {
        scrollData.firstIdx = maxFirstCharIdx;
        scrollData.pixelOffsetRemainder = maxPixelOffset;
    }
}

float TextView::computeScrollbarGrab(const ImVec2& fontSize, const LayoutData::ScrollbarData& scrollBarData, const int idx) const {
    double posFraction = 0.0;
    if (scrollBarData.maxPos > 0.0) {
        const auto& scrollData = m_xyScrollData[idx];
        const double currentScrollPos = static_cast<double>(scrollData.firstIdx) + static_cast<double>(scrollData.pixelOffsetRemainder / fontSize[idx]);
        posFraction = std::clamp(currentScrollPos / scrollBarData.maxPos, 0.0, 1.0);
    }
    return scrollBarData.region.Min[idx] + static_cast<float>(scrollBarData.travel * posFraction);
}

bool TextView::handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        for (int i = 0; i < 2; ++i)
            m_xyScrollData[i].scrollbarDragOffsetOpt.reset();

    const LayoutData::ScrollbarData* sbPtrs[2];
    for (int i = 0; i < 2; ++i) {
        sbPtrs[i] = layoutData.xyScrollbarDataOpt[i].has_value() ? &layoutData.xyScrollbarDataOpt[i].value() : nullptr;
        if (!sbPtrs[i])
            m_xyScrollData[i].scrollbarDragOffsetOpt.reset();
    }

    const auto& io = ImGui::GetIO();

    const bool alreadyDragging = m_xyScrollData[0].scrollbarDragOffsetOpt.has_value() || m_xyScrollData[1].scrollbarDragOffsetOpt.has_value();
    if (!alreadyDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
        // drag start
        for (int i = 0; i < 2; ++i) {
            const auto scrollBarDataPtr = sbPtrs[i];
            if (scrollBarDataPtr && scrollBarDataPtr->region.Contains(io.MousePos)) {
                const float mousePos = io.MousePos[i];
                const float grab = computeScrollbarGrab(fontSize, *scrollBarDataPtr, i);

                auto& scrollData = m_xyScrollData[i];
                if (mousePos >= grab && mousePos <= grab + scrollBarDataPtr->grab) {
                    // slider drag
                    scrollData.scrollbarDragOffsetOpt = mousePos - grab;   // схватили ползунок
                    scrollData.currentAnimationOpt.reset();
                }
                else {
                    // background click
                    const float travel = scrollBarDataPtr->travel;
                    if (travel > 0.0f) {
                        const float rel = std::clamp(
                            (mousePos - scrollBarDataPtr->grab * 0.5f - scrollBarDataPtr->region.Min[i]) / travel,
                            0.0f, 1.0f
                        );
                        animateTo(fontSize, static_cast<double>(rel) * scrollBarDataPtr->maxPos, i);
                    }
                    scrollData.scrollbarDragOffsetOpt = scrollBarDataPtr->grab * 0.5f;
                }

                break;
            }
        }
    }

    // process drag
    for (int i = 0; i < 2; ++i) {
        const auto scrollBarDataPtr = sbPtrs[i];
        auto& scrollData = m_xyScrollData[i];
        if (scrollBarDataPtr && scrollData.scrollbarDragOffsetOpt.has_value()) {
            if (scrollData.currentAnimationOpt.has_value())
                return true;

            if (scrollBarDataPtr->travel > 0.0f) {
                const float rel = std::clamp(
                    (io.MousePos[i] - scrollData.scrollbarDragOffsetOpt.value() - scrollBarDataPtr->region.Min[i]) / scrollBarDataPtr->travel,
                    0.0f, 1.0f
                );
                setPos(fontSize, static_cast<double>(rel) * scrollBarDataPtr->maxPos, i);
            }

            return true;
        }
    }

    return false;
}

void TextView::scrollByPixels(const ImVec2& fontSize, const ImVec2& delta, const int idx) {
    auto& scrollData = m_xyScrollData[idx];
    scrollData.currentAnimationOpt.reset();

    const float fontSizeVal = fontSize[idx];
    scrollData.pixelOffsetRemainder += delta[idx] * fontSizeVal;

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

    const auto& io = ImGui::GetIO();

    // mouse wheel
    if (ImGui::IsWindowHovered()) {
        ImVec2 delta(0.0f, 0.0f);
        if (io.MouseWheelH != 0.0f)
            delta.x = -io.MouseWheelH * kHorizontalCharsPerWheelScroll;
        if (io.MouseWheel != 0.0f) {
            if (!io.KeyShift)
                delta.y = -io.MouseWheel * kVerticalLinesPerWheelScroll;
            else {
                if (delta.x == 0.0f)
                    delta.x = -io.MouseWheel * kHorizontalCharsPerWheelScroll;
            }
        }
        for (int i = 0; i < 2; ++i)
            if (delta[i] != 0.0f)
                scrollByPixels(fontSize, delta, i);
    }

    // keyboard
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            if (!io.KeyCtrl)
                animateTo(fontSize, 0.0, 0); // move to x=0
            else
                animateTo(fontSize, 0.0, 1); // move to y=0
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            if (!io.KeyCtrl) {
                // move to x=length of longest visible line
                const auto maxVisibleLineLengthDbl = static_cast<double>(m_MaxVisibleLineLength);
                const auto visibleDbl = static_cast<double>(layoutData.xyMetrics[0].visibleNumFlt);
                const double target = maxVisibleLineLengthDbl > visibleDbl ? maxVisibleLineLengthDbl - visibleDbl : 0.0;
                animateTo(fontSize, target, 0);
            }
            else {
                // move to y=last line
                const double maxVerticalPos = m_Source.maxNum[1] > layoutData.xyMetrics[1].reservedNum
                    ? static_cast<double>(m_Source.maxNum[1]) - static_cast<double>(layoutData.xyMetrics[1].visibleNumFlt)
                    : 0.0;
                animateTo(fontSize, maxVerticalPos, 1);
            }
        }

        const double pageLinesNum = static_cast<double>(layoutData.xyMetrics[1].fullVisibleNum);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
            animateTo(fontSize, getCurrentPos(fontSize, 1) + pageLinesNum, 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
            animateTo(fontSize, getCurrentPos(fontSize, 1) - pageLinesNum, 1);
    }
}

double TextView::getCurrentPos(const ImVec2 &fontSize, const int idx) const {
    return static_cast<double>(m_xyScrollData[idx].firstIdx) + static_cast<double>(m_xyScrollData[idx].pixelOffsetRemainder / fontSize[idx]);
}

void TextView::setPos(const ImVec2 &fontSize, const double pos, const int idx) {
    const double clamped = std::max(0.0, pos);
    const double whole = std::floor(clamped);
    m_xyScrollData[idx].firstIdx = static_cast<uint64_t>(whole);
    m_xyScrollData[idx].pixelOffsetRemainder = static_cast<float>((clamped - whole) * fontSize[idx]);
}

void TextView::updateAnimation(const ImVec2& fontSize) {
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    for (int i = 0; i < 2; ++i) {
        auto& sd = m_xyScrollData[i];
        if (!sd.currentAnimationOpt.has_value())
            continue;

        auto& anim = sd.currentAnimationOpt.value();
        anim.elapsed += dt;

        const float t = std::clamp(anim.elapsed / kScrollAnimDuration, 0.0f, 1.0f);
        setPos(fontSize, anim.startPos + (anim.targetPos - anim.startPos) * static_cast<double>(t), i);

        if (t >= 1.0f)
            sd.currentAnimationOpt.reset();
    }
}

void TextView::animateTo(const ImVec2 &fontSize, const double targetPos, const int idx) {
    auto& scrollData = m_xyScrollData[idx];

    const double startPos = getCurrentPos(fontSize, idx);
    const double clampedTarget = std::max(0.0, targetPos);
    if (std::abs(clampedTarget - startPos) < 0.5) {
        scrollData.currentAnimationOpt.reset();
        setPos(fontSize, clampedTarget, idx);
        return;
    }

    auto& anim = scrollData.currentAnimationOpt.emplace();
    anim.startPos = startPos;
    anim.targetPos = clampedTarget;
    anim.elapsed = 0.0f;
}
