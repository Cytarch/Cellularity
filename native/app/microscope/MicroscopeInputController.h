#pragma once

#include <array>

#include "core/CellData.h"
#include "core/Gene.h"

#include "app/NativeWorldContext.h"

class MicroscopeDragController;
class NativeViewport;

struct QueuedMicroscopeTap {
    double worldX = 0.0;
    double worldY = 0.0;
    bool hasGenome = false;
    cellularity::GenomeData genome;
};

class MicroscopeInputController {
public:
    void clear();

    bool stepViewportInertia(NativeViewport& viewport);
    [[nodiscard]] bool viewportInertiaActive() const noexcept;
    void setViewportFlingVelocity(float velocityX, float velocityY);
    void stopViewportInertia();

    bool queueTouchDown(NativeViewport& viewport, int pointerId, float screenX,
            float screenY);
    bool queueTouchMove(NativeViewport& viewport, int pointerId, float screenX,
            float screenY);
    bool queueTouchUp(int pointerId);
    void queueTap(NativeViewport& viewport, float screenX, float screenY,
            const cellularity::GenomeData* genome);
    bool consumeQueuedTap(QueuedMicroscopeTap& tap);
    [[nodiscard]] int queuedWorldInputCount() const noexcept;
    [[nodiscard]] bool hasQueuedTap() const noexcept;
    bool queuePan(NativeViewport& viewport, bool hasDraggingPointers,
            int toolOrdinal, bool hasSelectedCell, float dx, float dy);
    bool queueZoom(NativeViewport& viewport, bool hasDraggingPointers,
            int toolOrdinal, bool hasSelectedCell, float factor,
            float screenX, float screenY);
    bool queueFling(bool hasDraggingPointers, int toolOrdinal, float velocityX,
            float velocityY);
    bool processQueues(NativeWorldContext context, MicroscopeDragController& drag,
            int toolOrdinal, double dragDeltaTime);

private:
    static constexpr int TOUCH_QUEUE_MAX = 200;

    float mFlingVelocityX = 0.0f;
    float mFlingVelocityY = 0.0f;
    int mDownPointerCount = 0;
    int mDownQueueCount = 0;
    int mMoveQueueCount = 0;
    int mUpQueueCount = 0;
    bool mHasQueuedTap = false;
    bool mQueuedTapHasGenome = false;
    double mQueuedTapWorldX = 0.0;
    double mQueuedTapWorldY = 0.0;
    cellularity::GenomeData mQueuedTapGenome;
    std::array<int, TOUCH_QUEUE_MAX> mDownQueuePointerIds{};
    std::array<int, TOUCH_QUEUE_MAX> mMoveQueuePointerIds{};
    std::array<int, TOUCH_QUEUE_MAX> mUpQueuePointerIds{};
    std::array<double, TOUCH_QUEUE_MAX> mDownQueueWorldX{};
    std::array<double, TOUCH_QUEUE_MAX> mDownQueueWorldY{};
    std::array<double, TOUCH_QUEUE_MAX> mMoveQueueWorldX{};
    std::array<double, TOUCH_QUEUE_MAX> mMoveQueueWorldY{};
};
