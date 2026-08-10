#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include "imgui_internal.h"

class TextView {
public:
    TextView() = default;
    ~TextView() = default;

    TextView(const TextView&) = delete;
    TextView& operator=(const TextView&) = delete;
    TextView(TextView&&) = delete;
    TextView& operator=(TextView&&) = delete;

    struct Source {
        std::function<uint64_t()> getLineCount;
        std::function<std::string_view(uint64_t)> getLine;
    };
    void draw(const Source& source);

    void reset();
private:
    struct LayoutData {
        uint64_t fullVisibleLinesNum;
        float lineRemainderHeight;
        uint64_t reservedLinesNum;

        ImRect gutterRegion;

        struct VerticalScrollbarData {
            ImRect region;
            double maxPos;
            float grabHeight;
            float travel;
        };

        struct TextViewData {
            ImRect region;
            uint64_t visibleCharsNum;

            std::optional<VerticalScrollbarData> verticalScrollbarData;
        };
        std::optional<TextViewData> textViewData;
    };
    LayoutData computeLayoutData(float charWidth, float lineHeight, uint64_t sourceTotalLinesNum) const;

    void clampTextOffsets(float lineHeight, const LayoutData& layoutData, uint64_t sourceTotalLinesNum);

    float computeScrollbarGrabY(const LayoutData::VerticalScrollbarData& verticalScrollbarData, float lineHeight) const;

    bool handleScrollbarInput(const LayoutData::TextViewData& textViewData, float lineHeight);
    void handleMouseWheelInput(float dy, float lineHeight);
    void handleInput(const LayoutData::TextViewData& textViewData, float lineHeight);

    uint64_t m_FirstLineIdx = 0;
    float m_VerticalPixelOffset = 0.0f;

    std::optional<float> m_VerticalScrollbarDragOffset;

    static constexpr float kGutterTextHorizontalPadding = 4.0f;
    static constexpr uint64_t kGutterMinDigitsNum = 3;
    static constexpr float kGutterLineThickness = 1.0f;

    static constexpr float kTextViewLeftPadding = 8.0f;
    static constexpr float kVerticalLinesPerWheelScroll = 3.0f;

    static constexpr float kScrollbarWidth = 10.0f;
    static constexpr float kScrollbarMinGrabHeight = 24.0f;
};
