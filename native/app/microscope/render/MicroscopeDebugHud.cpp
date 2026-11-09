#include "app/microscope/render/MicroscopeDebugHud.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace cellularity::microscope {
namespace {

constexpr std::array<float, 4> kBackgroundColor = {0.025f, 0.035f, 0.052f,
        0.92f};
constexpr std::array<float, 4> kAccentColor = {0.34f, 0.88f, 1.0f, 1.0f};
constexpr std::array<float, 4> kLabelColor = {0.52f, 0.62f, 0.73f, 1.0f};
constexpr std::array<float, 4> kValueColor = {0.88f, 0.94f, 1.0f, 1.0f};
constexpr auto kFpsWindow = std::chrono::milliseconds(500);
constexpr std::size_t kBackgroundLayer = 0;
constexpr std::size_t kAccentLayer = 1;
constexpr std::size_t kLabelLayer = 2;
constexpr std::size_t kValueLayer = 3;
constexpr std::size_t kLayerCount = 4;

constexpr std::array<const char*, SimulationWorldProfile::STAGE_COUNT>
        kStageLabels = {"TIME LIGHT",
                "RANDOM FOOD CELLS",
                "KINEMATICS",
                "SPLIT",
                "RESET FORCES",
                "FLAGELLOCYTES",
                "REMOVE DEAD",
                "UPDATE GRIDS",
                "EAT FOODS",
                "DEAD SPLASHES",
                "OVERLAPS",
                "FERTILIZATION",
                "CELL COLLISIONS",
                "LINK INTERACTIONS",
                "WALL COLLISIONS",
                "SMELL INTERACTIONS",
                "CELL FORCES",
                "VELOCITIES",
                "CILIOCYTES",
                "METABOLISM",
                "APPLY METABOLISM",
                "SIGNALS"};

// The HUD's numbers are written with std::to_chars, which never consults the C locale. printf
// does: under a locale with a decimal comma (ru_RU, de_DE, ...) it wrote "12,3", and the HUD
// font has no comma, so the values showed "?". Resetting LC_NUMERIC when the library loaded
// was not enough, because the JVM of a packaged desktop application sets the locale again
// later. Writing into a fixed buffer keeps the HUD free of allocations.
class HudValue final {
public:
    HudValue& text(const char* value) noexcept {
        while (*value != '\0' && mEnd < mBuffer.data() + mBuffer.size() - 1) {
            *mEnd++ = *value++;
        }
        *mEnd = '\0';
        return *this;
    }

    HudValue& fixed(double value, int precision) noexcept {
        char* const limit = mBuffer.data() + mBuffer.size() - 1;
        const std::to_chars_result result = std::isfinite(value)
                ? std::to_chars(mEnd, limit, value, std::chars_format::fixed, precision)
                : std::to_chars_result{mEnd, std::errc::invalid_argument};
        if (result.ec == std::errc()) {
            mEnd = result.ptr;
            *mEnd = '\0';
            return *this;
        }
        return text("-");
    }

    [[nodiscard]] const char* c_str() const noexcept { return mBuffer.data(); }

private:
    std::array<char, 64> mBuffer{};
    char* mEnd = mBuffer.data();
};

float choosePixelSize(int surfaceWidth, int surfaceHeight) {
    float pixelSize = surfaceHeight >= 1000 ? 3.0f : 2.0f;
    constexpr int kRows = SimulationWorldProfile::STAGE_COUNT + 4;
    while (pixelSize > 1.0f) {
        const float titlePixelSize = pixelSize + 1.0f;
        const float padding = pixelSize * 4.0f;
        const float rowHeight = pixelSize * 9.0f;
        const float panelWidth = padding * 2.0f + pixelSize * 6.0f * 37.0f;
        const float panelHeight = padding * 2.0f + titlePixelSize * 7.0f + pixelSize * 3.0f + rowHeight * kRows;
        if (panelWidth <= static_cast<float>(surfaceWidth) - 8.0f && panelHeight <= static_cast<float>(surfaceHeight) - 8.0f) {
            break;
        }
        pixelSize -= 1.0f;
    }
    return pixelSize;
}

} // namespace

void MicroscopeDebugHud::recordPresentedFrame() {
    const Clock::time_point now = Clock::now();
    if (mFpsWindowStartedAt == Clock::time_point{}) {
        mFpsWindowStartedAt = now;
        mFpsWindowFrames = 0;
        return;
    }
    ++mFpsWindowFrames;
    const auto elapsed = now - mFpsWindowStartedAt;
    if (elapsed < kFpsWindow) {
        return;
    }

    const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
    const double sampledFps = elapsedSeconds > 0.0
            ? static_cast<double>(mFpsWindowFrames) / elapsedSeconds
            : 0.0;
    mFramesPerSecond = mFpsRevision == 0
            ? sampledFps
            : mFramesPerSecond + (sampledFps - mFramesPerSecond) * 0.35;
    ++mFpsRevision;
    mFpsWindowStartedAt = now;
    mFpsWindowFrames = 0;
}

const cellularity::render::PixelTextRenderer&
MicroscopeDebugHud::prepareGeometry(int surfaceWidth,
        int surfaceHeight, const MicroscopeDebugHudTelemetry& telemetry) {
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return mTextRenderer;
    }

    recordPresentedFrame();
    if (surfaceWidth != mLastSurfaceWidth || surfaceHeight != mLastSurfaceHeight || telemetry.rateSampleSequence != mLastRateSampleSequence || telemetry.stageTimings.sequence != mLastStageTimingSequence || mFpsRevision != mLastFpsRevision) {
        rebuildGeometry(surfaceWidth, surfaceHeight, telemetry);
    }
    return mTextRenderer;
}

void MicroscopeDebugHud::rebuildGeometry(
        int surfaceWidth, int surfaceHeight,
        const MicroscopeDebugHudTelemetry& telemetry) {
    mTextRenderer.beginGeometry(surfaceWidth, surfaceHeight, kLayerCount);
    mTextRenderer.configureLayer(kBackgroundLayer, kBackgroundColor, 1);
    mTextRenderer.configureLayer(kAccentLayer, kAccentColor, 48);
    mTextRenderer.configureLayer(kLabelLayer, kLabelColor, 320);
    mTextRenderer.configureLayer(kValueLayer, kValueColor, 256);

    const float pixelSize = choosePixelSize(surfaceWidth, surfaceHeight);
    const float titlePixelSize = pixelSize + 1.0f;
    const float padding = pixelSize * 4.0f;
    const float rowHeight = pixelSize * 9.0f;
    const float titleHeight = titlePixelSize * 7.0f;
    constexpr int kRows = SimulationWorldProfile::STAGE_COUNT + 4;
    const float panelWidth = padding * 2.0f + pixelSize * 6.0f * 37.0f;
    const float panelHeight = padding * 2.0f + titleHeight + pixelSize * 3.0f + rowHeight * kRows;
    const float panelLeft = 8.0f;
    const float panelTop = 8.0f;
    const float labelX = panelLeft + padding;
    const float valueX = labelX + pixelSize * 6.0f * 20.0f;

    mTextRenderer.appendRectangle(kBackgroundLayer, panelLeft, panelTop,
            panelWidth, panelHeight);
    mTextRenderer.appendRectangle(
            kAccentLayer, panelLeft + padding,
            panelTop + padding + titleHeight + pixelSize,
            panelWidth - padding * 2.0f, pixelSize);

    float y = panelTop + padding;
    mTextRenderer.appendText(kAccentLayer, "PERFORMANCE", labelX, y,
            titlePixelSize);
    y += titleHeight + pixelSize * 3.0f;

    mTextRenderer.appendText(kLabelLayer, "FPS", labelX, y, pixelSize);
    mTextRenderer.appendText(kValueLayer, HudValue().fixed(mFramesPerSecond, 1).c_str(),
            valueX, y, pixelSize);
    y += rowHeight;

    mTextRenderer.appendText(kLabelLayer, "UPS", labelX, y, pixelSize);
    {
        HudValue value;
        if (telemetry.actualRateValid && std::isfinite(telemetry.actualCompletedUps)) {
            value.fixed(telemetry.actualCompletedUps, 0);
        } else {
            value.text("WAITING");
        }
        mTextRenderer.appendText(kValueLayer, value.c_str(), valueX, y, pixelSize);
    }
    y += rowHeight;

    mTextRenderer.appendText(kLabelLayer, "SIM TOTAL", labelX, y, pixelSize);
    {
        HudValue value;
        if (telemetry.stageTimings.sequence != 0) {
            value.fixed(telemetry.stageTimings.totalNanos / 1'000'000.0, 3).text(" MS");
        } else {
            value.text("WAITING");
        }
        mTextRenderer.appendText(kValueLayer, value.c_str(), valueX, y, pixelSize);
    }
    y += rowHeight;

    mTextRenderer.appendText(kAccentLayer, "SIMULATION STAGES", labelX, y,
            pixelSize);
    mTextRenderer.appendText(kAccentLayer, "MS / %", valueX, y, pixelSize);
    y += rowHeight;

    for (std::size_t stage = 0; stage < kStageLabels.size(); ++stage) {
        mTextRenderer.appendText(kLabelLayer, kStageLabels[stage], labelX, y,
                pixelSize);
        if (telemetry.stageTimings.sequence != 0) {
            const double stageNanos = telemetry.stageTimings.stageNanos[stage];
            const double percentage = telemetry.stageTimings.totalNanos > 0.0
                    ? stageNanos / telemetry.stageTimings.totalNanos * 100.0
                    : 0.0;
            mTextRenderer.appendText(kValueLayer,
                    HudValue()
                            .fixed(stageNanos / 1'000'000.0, 3)
                            .text(" ")
                            .fixed(percentage, 1)
                            .text("%")
                            .c_str(),
                    valueX, y, pixelSize);
        } else {
            mTextRenderer.appendText(kValueLayer, "-", valueX, y, pixelSize);
        }
        y += rowHeight;
    }

    mLastSurfaceWidth = surfaceWidth;
    mLastSurfaceHeight = surfaceHeight;
    mLastRateSampleSequence = telemetry.rateSampleSequence;
    mLastStageTimingSequence = telemetry.stageTimings.sequence;
    mLastFpsRevision = mFpsRevision;
    mTextRenderer.commitGeometry();
}

} // namespace cellularity::microscope
