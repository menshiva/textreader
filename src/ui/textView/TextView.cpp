#include "TextView.h"
#include <algorithm>
#include <charconv>
#include "imgui_internal.h"

void TextView::draw(const Source& source) {
    if (!source.getLineCount || !source.getLine)
        return;

    ImGui::BeginChild(
        "##textview", ImVec2(0, 0),
        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    const auto textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const auto disabledTextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto separatorCol = ImGui::GetColorU32(ImGuiCol_Separator);

    const float charWidth = ImGui::GetFontBaked()->GetCharAdvance('0');
    const float lineHeight = ImGui::GetTextLineHeight();
    const uint64_t sourceLinesNum = source.getLineCount();

    const auto layoutData = computeLayoutData(charWidth, lineHeight, sourceLinesNum);
    const auto textViewLayoutDataPtr = layoutData.textViewData.has_value() ? &layoutData.textViewData.value() : nullptr;
    const auto dl = ImGui::GetWindowDrawList();

    clampTextOffsets(lineHeight, layoutData, sourceLinesNum);

    // gutter
    {
        if (textViewLayoutDataPtr) {
            dl->PushClipRect(layoutData.gutterRegion.Min, layoutData.gutterRegion.Max, true);
            for (uint64_t i = 0; i < layoutData.reservedLinesNum + 1; ++i) {
                const uint64_t lineNo = m_FirstLineIdx + i;
                if (lineNo >= sourceLinesNum)
                    break;

                const float y = std::floor(layoutData.gutterRegion.Min.y + lineHeight * static_cast<float>(i) - m_VerticalPixelOffset);

                char buf[24];
                const auto res = std::to_chars(buf, buf + sizeof(buf), lineNo + 1);
                const int len = static_cast<int>(res.ptr - buf);
                const float numberWidth = charWidth * static_cast<float>(len);
                dl->AddText(
                    ImVec2(layoutData.gutterRegion.Max.x - kGutterTextHorizontalPadding - numberWidth, y),
                    disabledTextColor, buf, buf + len
                );

                const float sepY = std::floor(y + lineHeight) - 0.5f; // -0.5f shifts line a little bit so that lines themselves look centered
                dl->AddLine(
                    ImVec2(layoutData.gutterRegion.Min.x, sepY), ImVec2(layoutData.gutterRegion.Max.x, sepY),
                    separatorCol, kGutterLineThickness
                );
            }
            dl->PopClipRect();
        }

        // vertical line to the right of the gutter
        dl->AddLine(
            ImVec2(layoutData.gutterRegion.Max.x, layoutData.gutterRegion.Min.y),
            ImVec2(layoutData.gutterRegion.Max.x, layoutData.gutterRegion.Max.y),
            ImGui::GetColorU32(ImGuiCol_Border), kGutterLineThickness
        );
    }

    // text view
    if (textViewLayoutDataPtr) {
        {
            dl->PushClipRect(textViewLayoutDataPtr->region.Min, textViewLayoutDataPtr->region.Max, true);
            for (uint64_t i = 0; i < layoutData.reservedLinesNum + 1; ++i) {
                const uint64_t lineNo = m_FirstLineIdx + i;
                if (lineNo >= sourceLinesNum)
                    break;

                auto s = source.getLine(lineNo);
                if (s.size() > textViewLayoutDataPtr->visibleCharsNum)
                    s = s.substr(0, textViewLayoutDataPtr->visibleCharsNum);

                const float y = std::floor(textViewLayoutDataPtr->region.Min.y + lineHeight * static_cast<float>(i) - m_VerticalPixelOffset);
                dl->AddText(ImVec2(textViewLayoutDataPtr->region.Min.x, y), textColor, s.data(), s.data() + s.size());
            }
            dl->PopClipRect();
        }

        // vertical scroll bar
        if (const auto verticalScrollbarDataPtr = textViewLayoutDataPtr->verticalScrollbarData.has_value() ? &textViewLayoutDataPtr->verticalScrollbarData.value() : nullptr) {
            const ImU32 scrollbarColor = ImGui::GetColorU32(m_VerticalScrollbarDragOffset.has_value()
                ? ImGuiCol_ScrollbarGrabActive
                : ImGui::IsWindowHovered() && verticalScrollbarDataPtr->region.Contains(ImGui::GetIO().MousePos)
                    ? ImGuiCol_ScrollbarGrabHovered
                    : ImGuiCol_ScrollbarGrab
            );

            const float grabY = computeScrollbarGrabY(*verticalScrollbarDataPtr, lineHeight);
            dl->AddRectFilled(
                ImVec2(verticalScrollbarDataPtr->region.Min.x, grabY),
                ImVec2(verticalScrollbarDataPtr->region.Max.x, grabY + verticalScrollbarDataPtr->grabHeight),
                scrollbarColor, kScrollbarWidth * 0.5f
            );
        }

        // TODO
        // dl->AddRect(windowRegionMin, windowRegionMax, IM_COL32(255,0,0,255));
        // dl->AddRect(ImVec2(0, windowRegionMin.y), ImVec2(0 + textWidth, windowRegionMax.y), IM_COL32(0,255,0,255));

        handleInput(*textViewLayoutDataPtr, lineHeight);
    }

    ImGui::EndChild();
}

void TextView::reset() {
    m_FirstLineIdx = 0;
    m_VerticalPixelOffset = 0.0f;

    m_VerticalScrollbarDragOffset.reset();
}

static uint64_t digitCount(uint64_t n) {
    uint64_t d = 1;
    while (n >= 10) {
        n /= 10;
        ++d;
    }
    return d;
}

TextView::LayoutData TextView::computeLayoutData(const float charWidth, const float lineHeight, const uint64_t sourceTotalLinesNum) const {
    LayoutData res;

    const auto windowRegionSize = ImGui::GetContentRegionAvail();
    const auto windowRegionMin = ImGui::GetCursorScreenPos();
    const auto windowRegionMax = windowRegionMin + windowRegionSize;

    {
        const float visibleLinesNum = lineHeight > 0.0f ? std::max<float>(0.0f, windowRegionSize.y / lineHeight) : 0.0f;
        const float fullVisibleLinesNum = std::floor(visibleLinesNum);
        res.fullVisibleLinesNum = static_cast<uint64_t>(fullVisibleLinesNum);
        res.lineRemainderHeight = visibleLinesNum - fullVisibleLinesNum;
        if (res.lineRemainderHeight < 0.001f)
            res.lineRemainderHeight = 0.0f;
        res.reservedLinesNum = res.fullVisibleLinesNum + static_cast<uint64_t>(res.lineRemainderHeight > 0.0f);
    }

    const bool drawContent = sourceTotalLinesNum && res.reservedLinesNum;

    // gutter
    {
        uint64_t gutterDigitsNum = kGutterMinDigitsNum;
        if (drawContent) {
            const uint64_t lastVisibleLine = std::min<uint64_t>(sourceTotalLinesNum, m_FirstLineIdx + res.reservedLinesNum);
            const uint64_t lastVisibleLineDigitsNum = digitCount(lastVisibleLine);
            if (lastVisibleLineDigitsNum > gutterDigitsNum)
                gutterDigitsNum = lastVisibleLineDigitsNum;
        }
        const float gutterWidth = kGutterTextHorizontalPadding * 2.0f + charWidth * static_cast<float>(gutterDigitsNum);
        res.gutterRegion = ImRect(windowRegionMin, ImVec2(windowRegionMin.x + gutterWidth, windowRegionMax.y));
    }

    if (drawContent) {
        auto& textViewData = res.textViewData.emplace();
        float textViewContentRight = windowRegionMax.x;

        const double visibleLines = static_cast<double>(res.fullVisibleLinesNum) + static_cast<double>(res.lineRemainderHeight);
        const double sourceTotalLinesNumDbl = static_cast<double>(sourceTotalLinesNum);

        // vertical scrollbar
        if (sourceTotalLinesNumDbl > visibleLines) {
            textViewContentRight -= kScrollbarWidth;
            auto& vertScrollbarData = textViewData.verticalScrollbarData.emplace();

            vertScrollbarData.region = ImRect(ImVec2(textViewContentRight, windowRegionMin.y), windowRegionMax);
            vertScrollbarData.maxPos = sourceTotalLinesNumDbl - visibleLines;

            const float trackHeight = vertScrollbarData.region.GetHeight();
            const double visibleFraction = visibleLines / sourceTotalLinesNumDbl;
            vertScrollbarData.grabHeight = std::clamp(static_cast<float>(trackHeight * visibleFraction), kScrollbarMinGrabHeight, trackHeight);
            vertScrollbarData.travel = trackHeight - vertScrollbarData.grabHeight;
        }

        // text view
        {
            textViewData.region = ImRect(
                ImVec2(res.gutterRegion.Max.x + kTextViewLeftPadding, windowRegionMin.y),
                ImVec2(textViewContentRight, windowRegionMax.y)
            );
            textViewData.visibleCharsNum = static_cast<uint64_t>(std::ceil(std::max<float>(0.0f, textViewData.region.GetWidth() / charWidth)));
        }
    }

    return res;
}

void TextView::clampTextOffsets(const float lineHeight, const LayoutData& layoutData, const uint64_t sourceTotalLinesNum) {
    if (lineHeight <= 0.0f || !layoutData.reservedLinesNum)
        return;

    if (!sourceTotalLinesNum) {
        reset();
        return;
    }

    if (sourceTotalLinesNum < layoutData.reservedLinesNum) {
        reset();
        return;
    }

    const uint64_t maxFirstLineIdx = sourceTotalLinesNum - layoutData.reservedLinesNum;
    const float maxPixelOffset = layoutData.lineRemainderHeight > 0.0f ? lineHeight * (1.0f - layoutData.lineRemainderHeight) : 0.0f;

    if (m_FirstLineIdx > maxFirstLineIdx || (m_FirstLineIdx == maxFirstLineIdx && m_VerticalPixelOffset > maxPixelOffset)) {
        m_FirstLineIdx = maxFirstLineIdx;
        m_VerticalPixelOffset = maxPixelOffset;
    }
}

float TextView::computeScrollbarGrabY(const LayoutData::VerticalScrollbarData& verticalScrollbarData, const float lineHeight) const {
    double posFraction = 0.0;
    if (verticalScrollbarData.maxPos > 0.0) {
        double currentScrollPos = static_cast<double>(m_FirstLineIdx);
        if (lineHeight > 0.0f)
            currentScrollPos += static_cast<double>(m_VerticalPixelOffset / lineHeight);
        posFraction = currentScrollPos / verticalScrollbarData.maxPos;
    }
    return  verticalScrollbarData.region.Min.y + static_cast<float>(verticalScrollbarData.travel * posFraction);
}

bool TextView::handleScrollbarInput(const LayoutData::TextViewData& textViewData, const float lineHeight) {
    const auto verticalScrollbarDataPtr = textViewData.verticalScrollbarData.has_value() ? &textViewData.verticalScrollbarData.value() : nullptr;
    if (!verticalScrollbarDataPtr) {
        m_VerticalScrollbarDragOffset.reset();
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && verticalScrollbarDataPtr->region.Contains(io.MousePos)) {
        auto& val = m_VerticalScrollbarDragOffset.emplace();
        const float grabY = computeScrollbarGrabY(*verticalScrollbarDataPtr, lineHeight);
        if (io.MousePos.y >= grabY && io.MousePos.y <= grabY + verticalScrollbarDataPtr->grabHeight)
            val = io.MousePos.y - grabY; // slider drag
        else
            val = verticalScrollbarDataPtr->grabHeight * 0.5f; // background click
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_VerticalScrollbarDragOffset.reset();
    if (!m_VerticalScrollbarDragOffset.has_value())
        return false;

    if (verticalScrollbarDataPtr->travel > 0.0f && lineHeight > 0.0f) {
        const float rel = std::clamp(
            (io.MousePos.y - m_VerticalScrollbarDragOffset.value() - verticalScrollbarDataPtr->region.Min.y) / verticalScrollbarDataPtr->travel,
            0.0f, 1.0f
        );

        const double targetPos = static_cast<double>(rel) * verticalScrollbarDataPtr->maxPos;
        const double wholeLines = std::floor(targetPos);

        m_FirstLineIdx = static_cast<uint64_t>(wholeLines);
        m_VerticalPixelOffset = static_cast<float>((targetPos - wholeLines) * lineHeight);
    }

    return true;
}

void TextView::handleMouseWheelInput(const float dy, const float lineHeight) {
    m_VerticalPixelOffset += dy * lineHeight;

    const float deltaLines = std::floor(m_VerticalPixelOffset / lineHeight);
    m_VerticalPixelOffset -= deltaLines * lineHeight;

    if (deltaLines >= 0.0f) {
        m_FirstLineIdx += static_cast<uint64_t>(deltaLines);
        return;
    }

    const auto step = static_cast<uint64_t>(-deltaLines);
    if (step >= m_FirstLineIdx) {
        m_FirstLineIdx = 0;
        m_VerticalPixelOffset = 0.0f;
    }
    else {
        m_FirstLineIdx -= step;
    }
}

void TextView::handleInput(const LayoutData::TextViewData& textViewData, const float lineHeight) {
    if (lineHeight <= 0.0f)
        return;
    const auto& io = ImGui::GetIO();

    if (handleScrollbarInput(textViewData, lineHeight))
        return;

    if (ImGui::IsWindowHovered()) {
        if (io.MouseWheel != 0.0f)
            handleMouseWheelInput(-io.MouseWheel * kVerticalLinesPerWheelScroll, lineHeight);
    }
}
