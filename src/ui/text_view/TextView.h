#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include "imgui_internal.h"

class Ui;

class TextView {
public:
    explicit TextView(Ui& parentUi);
    ~TextView() = default;

    TextView(const TextView&) = delete;
    TextView& operator=(const TextView&) = delete;
    TextView(TextView&&) noexcept = delete;
    TextView& operator=(TextView&&) noexcept = delete;

    struct Source {
        std::function<std::string_view(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength)> getLine;
        std::function<void(uint64_t& maxColsNum, uint64_t& rowsNum)> getTextSize;
    };
    void reset(Source source = Source{nullptr, nullptr});
    void draw();
private:
    enum Axis : int { kAxisX = 0, kAxisY = 1 };
    static constexpr int kAxisCount = 2;
    static constexpr Axis kAxes[kAxisCount] = {kAxisX, kAxisY};

    struct LayoutData {
        struct FontMetrics {
            float visibleNumFlt;

            uint64_t fullVisibleNum;
            float remainder;

            uint64_t reservedNum; // fullVisibleNum + (remainder > 0.0f)
        };
        FontMetrics xyMetrics[kAxisCount];

        struct GutterData {
            ImRect region;
        } gutterData;

        struct ScrollbarData {
            ImRect region;
            double maxPos;
            float grabSize;
            float travel;
        };
        std::optional<ScrollbarData> xyScrollbarDataOpt[kAxisCount];

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

    void drawGutter(const LayoutData& layoutData, const ImVec2& fontSize, uint64_t sourceLinesNum) const;
    void drawScrollbars(const LayoutData& layoutData, const ImVec2& fontSize) const;
    void drawText(const LayoutData& layoutData, const ImVec2& fontSize, uint64_t sourceLinesNum);

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

    void clampScrollData(const ImVec2& fontSize, const LayoutData& layoutData, uint64_t sourceMax, Axis axis);
    float computeScrollbarGrab(const ImVec2& fontSize, const LayoutData::ScrollbarData& scrollBarData, Axis axis) const;

    void scrollByPixels(const ImVec2& fontSize, const ImVec2& delta, Axis axis);

    void handleInput(const LayoutData& layoutData, const ImVec2& fontSize, uint64_t sourceMaxLinesNum);
    bool handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize);
    void handleSelectionInput(const LayoutData& layoutData, const ImVec2& fontSize, uint64_t sourceMaxLinesNum);
    void handleWheelInput(const ImVec2& fontSize);
    void handleKeyboardInput(const LayoutData& layoutData, const ImVec2& fontSize, uint64_t sourceMaxLinesNum);

    double getCurrentPos(const ImVec2& fontSize, Axis axis) const;
    void setPos(const ImVec2& fontSize, double pos, Axis axis);

    void updateAnimation(const ImVec2& fontSize);
    void animateTo(const ImVec2& fontSize, double targetPos, Axis axis);

    struct TextPos { uint64_t col = 0; uint64_t line = 0; };
    TextPos getPosFromMouse(const LayoutData::TextViewData& textViewData, const ImVec2& fontSize, uint64_t sourceMaxLinesNum) const;
    bool getSelectionRange(TextPos& outFrom, TextPos& outTo) const;
    void copySelection() const;

    Ui& m_ParentUi;

    Source m_Source;
    ScrollData m_xyScrollData[kAxisCount];
    uint64_t m_MaxVisibleLineLength = 0;

    // start could be > than stop!!!
    struct Selection { TextPos start; TextPos stop; };
    std::optional<Selection> m_CurrentSelectionOpt;
    bool m_Selecting = false;

    static constexpr float kGutterTextHorizontalPadding = 4.0f;
    static constexpr uint64_t kGutterMinDigitsNum = 3;
    static constexpr float kGutterLineThickness = 1.0f;

    static constexpr float kTextViewLeftPadding = 8.0f;

    static constexpr float kVerticalLinesPerWheelScroll = 3.0f;
    static constexpr float kHorizontalCharsPerWheelScroll = 10.0f;
    static constexpr float kScrollbarSize = 10.0f;
    static constexpr float kScrollbarMinGrabSize = 24.0f;

    static constexpr float kScrollAnimDuration = 0.1f; // 100 ms

    static constexpr float kSelectionAutoScrollSpeedModifier = 0.20f;

    static constexpr size_t kMaxCopyMb = 16;
};
