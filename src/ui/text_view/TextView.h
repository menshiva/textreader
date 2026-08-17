#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include "imgui.h"

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
        virtual ~Source() = default;
        virtual std::string_view getLine(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength) const = 0;
        virtual void getTextSize(uint64_t& maxColsNum, uint64_t& rowsNum) const = 0;
    };
    void reset(const Source* sourcePtr = nullptr);
    void draw();

    void setSearchNeedle(std::string needle);
    void showMatch(uint64_t colIdx, uint64_t lineIdx);

    uint64_t getFirstWholeVisibleLineIdx() const;
    bool isCurrentSearchMatchVisible() const;
private:
    enum Axis : int { kAxisX = 0, kAxisY = 1 };
    static constexpr int kAxisCount = 2;
    static constexpr Axis kAxes[kAxisCount] = {kAxisX, kAxisY};

    uint64_t getFirstWholeVisibleIdx(Axis axis) const;

    struct LayoutData;

    static double computeMaxPos(uint64_t sourceSize, float visibleNumFlt);
    static double computeMaxPos(const LayoutData& layoutData, Axis axis);

    bool computeLayoutData(
        const ImVec2& windowRegionMin, const ImVec2& windowRegionSize, const ImVec2& fontSize,
        const uint64_t (&sourceTextSize)[kAxisCount], LayoutData& outData
    ) const;

    float computeRowScreenY(float regionMinY, const ImVec2& fontSize, uint64_t rowIdx) const;

    void drawGutter(const LayoutData& layoutData, const ImVec2& fontSize) const;
    void drawScrollbars(const LayoutData& layoutData, const ImVec2& fontSize) const;
    void drawText(const LayoutData& layoutData, const ImVec2& fontSize);

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

    struct ScrollbarData;

    float computeScrollbarGrab(const ImVec2& fontSize, const ScrollbarData& scrollBarData, Axis axis) const;

    void scrollByPixels(const ImVec2& fontSize, const ImVec2& delta, Axis axis);

    void handleInput(const LayoutData& layoutData, const ImVec2& fontSize);
    bool handleScrollbarInput(const LayoutData& layoutData, const ImVec2& fontSize);
    void handleSelectionInput(const LayoutData& layoutData, const ImVec2& fontSize);
    void handleWheelInput(const ImVec2& fontSize);
    void handleKeyboardInput(const LayoutData& layoutData, const ImVec2& fontSize);

    double getCurrentPos(const ImVec2& fontSize, Axis axis) const;
    void setPos(const ImVec2& fontSize, double pos, Axis axis);

    void updateAnimation(const ImVec2& fontSize);
    void animateTo(const ImVec2& fontSize, const LayoutData& layoutData, double targetPos, Axis axis);

    struct TextPos { uint64_t col = 0; uint64_t line = 0; };
    TextPos getPosFromMouse(const struct ImRect& textViewRegion, const ImVec2& fontSize, uint64_t sourceLinesNum) const;
    bool getSelectionRange(TextPos& outFrom, TextPos& outTo) const;
    void copySelection() const;

    void applyPendingSearchScroll(const LayoutData& layoutData, const ImVec2& fontSize);

    Ui& m_ParentUi;

    const Source* m_SourcePtr = nullptr;
    ScrollData m_xyScrollData[kAxisCount];
    uint64_t m_LastVisibleLinesNum = 0;
    uint64_t m_MaxVisibleLineLength = 0;

    // start could be > than stop!!!
    struct Selection { TextPos start; TextPos stop; };
    std::optional<Selection> m_CurrentSelectionOpt;
    bool m_Selecting = false;

    std::string m_SearchNeedle;
    uint64_t m_SearchNeedleCols = 0;
    std::optional<TextPos> m_CurrentSearchMatchOpt;
    std::optional<TextPos> m_PendingSearchScrollOpt;

    static constexpr float kGutterTextHorizontalPadding = 4.0f;
    static constexpr uint64_t kGutterMinDigitsNum = 3;
    static constexpr float kGutterLineThickness = 1.0f;

    static constexpr float kTextViewLeftPadding = 8.0f;

    static constexpr float kVerticalLinesPerWheelScroll = 3.0f;
    static constexpr float kHorizontalCharsPerWheelScroll = 10.0f;
    static constexpr float kScrollbarSize = 10.0f;
    static constexpr float kScrollbarMinGrabSize = 24.0f;

    static constexpr float kScrollAnimDuration = 0.1f; // 100 ms

    // a region taller than a whole number of rows by less than this counts as a whole number
    static constexpr float kFractionalRowEpsilon = 0.001f;

    static constexpr float kSelectionAutoScrollSpeedModifier = 0.20f;

    static constexpr size_t kMaxCopyMb = 16;
    static constexpr uint64_t kMaxCopyLines = 50000;
};
