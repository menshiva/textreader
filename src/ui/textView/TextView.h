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
        std::function<std::string_view(uint64_t lineIdx, uint64_t fromCol, uint64_t maxCols)> getLine;
        uint64_t maxNum[2];
    };
    void reset(Source source = Source{nullptr, {0, 0}});
    void draw();
private:
    struct LayoutData {
        struct FontMetrics {
            float visibleNumFlt;

            uint64_t fullVisibleNum;
            float remainder;

            uint64_t reservedNum; // fullVisibleNum + (remainder > 0.0f)
        };
        FontMetrics xyMetrics[2];

        struct GutterData {
            ImRect region;
        } gutterData;

        struct ScrollbarData {
            ImRect region;
            double maxPos;
            float grab;
            float travel;
        };
        std::optional<ScrollbarData> xyScrollbarDataOpt[2];

        struct TextViewData {
            ImRect region;
        };
        std::optional<TextViewData> textViewDataOpt;
    };
    bool computeLayoutData(
        const ImVec2& windowRegionMin, const ImVec2& windowRegionSize, const ImVec2& fontSize,
        uint64_t sourceMaxLineLength, uint64_t sourceMaxLinesNum,
        LayoutData& outData
    ) const;

    struct ScrollData {
        uint64_t firstIdx = 0;
        float pixelOffsetRemainder = 0.0f;
        std::optional<float> scrollbarDragOffsetOpt;

        struct Animation {
            double startPos;
            double targetPos;
            float elapsed;
        };
        std::optional<Animation> currentAnimationOpt;
    };

    void clampScrollData(const ImVec2& fontSize, const LayoutData& layoutData, uint64_t sourceMax, int idx);
    float computeScrollbarGrab(const ImVec2& fontSize, const LayoutData::ScrollbarData& scrollBarData, int idx) const;

    bool handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize);
    void scrollByPixels(const ImVec2& fontSize, const ImVec2& delta, int idx);
    void handleInput(const LayoutData& layoutData, const ImVec2& fontSize);

    double getCurrentPos(const ImVec2& fontSize, int idx) const;
    void setPos(const ImVec2& fontSize, double pos, int idx);

    void updateAnimation(const ImVec2& fontSize);
    void animateTo(const ImVec2& fontSize, double targetPos, int idx);

    Source m_Source;
    ScrollData m_xyScrollData[2];

    static constexpr float kGutterTextHorizontalPadding = 4.0f;
    static constexpr uint64_t kGutterMinDigitsNum = 3;
    static constexpr float kGutterLineThickness = 1.0f;

    static constexpr float kTextViewLeftPadding = 8.0f;

    static constexpr float kVerticalLinesPerWheelScroll = 3.0f;
    static constexpr float kHorizontalCharsPerWheelScroll = 10.0f;
    static constexpr float kScrollbarSize = 10.0f;
    static constexpr float kScrollbarMinGrabSize = 24.0f;

    static constexpr float kScrollAnimDuration = 0.1f; // 100 ms
};
