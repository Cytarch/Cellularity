#include "app/microscope/MicroscopeInputController.h"

#include <cmath>

#include "app/interaction/NativeViewport.h"
#include "app/microscope/interaction/MicroscopeDragController.h"

namespace {

constexpr int TOOL_MOVE = 1;

} // namespace

void MicroscopeInputController::clear() {
    mDownPointerCount = 0;
    mDownQueueCount = 0;
    mMoveQueueCount = 0;
    mUpQueueCount = 0;
    mHasQueuedTap = false;
}

bool MicroscopeInputController::stepViewportInertia(NativeViewport& viewport) {
    if (mFlingVelocityX == 0.0f && mFlingVelocityY == 0.0f) {
        return false;
    }

    static constexpr float VELOCITY_TO_SCREEN_DELTA = 1.0f / 100.0f;
    float dx = mFlingVelocityX * VELOCITY_TO_SCREEN_DELTA;
    float dy = mFlingVelocityY * VELOCITY_TO_SCREEN_DELTA;
    viewport.moveByScreenDelta(dx, dy);
    mFlingVelocityX *= 0.92f;
    mFlingVelocityY *= 0.92f;
    if (std::abs(mFlingVelocityX) < 0.01f) {
        mFlingVelocityX = 0.0f;
    }
    if (std::abs(mFlingVelocityY) < 0.01f) {
        mFlingVelocityY = 0.0f;
    }
    return true;
}

bool MicroscopeInputController::viewportInertiaActive() const noexcept {
    return mFlingVelocityX != 0.0f || mFlingVelocityY != 0.0f;
}

void MicroscopeInputController::setViewportFlingVelocity(float velocityX,
        float velocityY) {
    mFlingVelocityX = velocityX;
    mFlingVelocityY = velocityY;
}

void MicroscopeInputController::stopViewportInertia() {
    mFlingVelocityX = 0.0f;
    mFlingVelocityY = 0.0f;
}

bool MicroscopeInputController::queueTouchDown(NativeViewport& viewport,
        int pointerId, float screenX,
        float screenY) {
    if (mDownQueueCount >= TOUCH_QUEUE_MAX) {
        return false;
    }
    int queueIndex = mDownQueueCount++;
    mDownQueuePointerIds[queueIndex] = pointerId;
    mDownQueueWorldX[queueIndex] = viewport.screenToWorldX(screenX);
    mDownQueueWorldY[queueIndex] = viewport.screenToWorldY(screenY);
    return true;
}

bool MicroscopeInputController::queueTouchMove(NativeViewport& viewport,
        int pointerId, float screenX,
        float screenY) {
    if (mMoveQueueCount >= TOUCH_QUEUE_MAX) {
        return false;
    }
    int queueIndex = mMoveQueueCount++;
    mMoveQueuePointerIds[queueIndex] = pointerId;
    mMoveQueueWorldX[queueIndex] = viewport.screenToWorldX(screenX);
    mMoveQueueWorldY[queueIndex] = viewport.screenToWorldY(screenY);
    return true;
}

bool MicroscopeInputController::queueTouchUp(int pointerId) {
    if (mUpQueueCount >= TOUCH_QUEUE_MAX) {
        return false;
    }
    mUpQueuePointerIds[mUpQueueCount++] = pointerId;
    return true;
}

void MicroscopeInputController::queueTap(
        NativeViewport& viewport, float screenX, float screenY,
        const cellularity::GenomeData* genome) {
    // Resolve through the event-time camera. A later Pan/Zoom must not retarget
    // a tap while it waits for the world safe point.
    mQueuedTapWorldX = viewport.screenToWorldX(screenX);
    mQueuedTapWorldY = viewport.screenToWorldY(screenY);
    mQueuedTapHasGenome = genome != nullptr;
    if (genome != nullptr) {
        mQueuedTapGenome = *genome;
    }
    mHasQueuedTap = true;
}

bool MicroscopeInputController::consumeQueuedTap(QueuedMicroscopeTap& tap) {
    if (!mHasQueuedTap) {
        return false;
    }
    mHasQueuedTap = false;
    tap.worldX = mQueuedTapWorldX;
    tap.worldY = mQueuedTapWorldY;
    tap.hasGenome = mQueuedTapHasGenome;
    if (mQueuedTapHasGenome) {
        tap.genome = mQueuedTapGenome;
    }
    return true;
}

int MicroscopeInputController::queuedWorldInputCount() const noexcept {
    return mDownQueueCount + mMoveQueueCount + mUpQueueCount + (mHasQueuedTap ? 1 : 0);
}

bool MicroscopeInputController::hasQueuedTap() const noexcept {
    return mHasQueuedTap;
}

bool MicroscopeInputController::queuePan(NativeViewport& viewport,
        bool hasDraggingPointers,
        int toolOrdinal, bool hasSelectedCell,
        float dx, float dy) {
    if (toolOrdinal == TOOL_MOVE && hasDraggingPointers) {
        return false;
    }
    if (hasSelectedCell || !viewport.isReady()) {
        return false;
    }

    stopViewportInertia();
    viewport.moveByScreenDelta(dx, dy);
    return true;
}

bool MicroscopeInputController::queueZoom(NativeViewport& viewport,
        bool hasDraggingPointers,
        int toolOrdinal, bool hasSelectedCell,
        float factor, float screenX,
        float screenY) {
    if (toolOrdinal == TOOL_MOVE && hasDraggingPointers) {
        return false;
    }
    if (!viewport.isReady()) {
        return false;
    }

    stopViewportInertia();
    if (!hasSelectedCell) {
        viewport.zoomAt(factor, screenX, screenY);
    } else {
        viewport.setCamera(viewport.cameraX(), viewport.cameraY(),
                viewport.zoom() * factor);
    }
    return true;
}

bool MicroscopeInputController::queueFling(bool hasDraggingPointers,
        int toolOrdinal, float velocityX,
        float velocityY) {
    if (toolOrdinal == TOOL_MOVE && hasDraggingPointers) {
        return false;
    }
    setViewportFlingVelocity(velocityX, velocityY);
    return true;
}

bool MicroscopeInputController::processQueues(NativeWorldContext context,
        MicroscopeDragController& drag,
        int toolOrdinal,
        double dragDeltaTime) {
    bool changed = false;

    if (toolOrdinal == TOOL_MOVE) {
        for (int queueIndex = 0; queueIndex < mDownQueueCount; queueIndex++) {
            if (drag.draggingPointerCount() == mDownPointerCount) {
                for (int slotIndex = 0;
                        slotIndex < MicroscopeDragController::MAX_POINTERS; slotIndex++) {
                    if (drag.isDragging(slotIndex)) {
                        continue;
                    }
                    if (drag.beginDragSlot(
                                context, slotIndex, mDownQueuePointerIds[queueIndex],
                                mDownQueueWorldX[queueIndex], mDownQueueWorldY[queueIndex])) {
                        changed = true;
                    }
                    break;
                }
            }
            mDownPointerCount++;
        }

        for (int queueIndex = 0; queueIndex < mMoveQueueCount; queueIndex++) {
            if (drag.draggingPointerCount() != mDownPointerCount) {
                continue;
            }
            for (int slotIndex = 0;
                    slotIndex < MicroscopeDragController::MAX_POINTERS; slotIndex++) {
                if (!drag.isDragging(slotIndex) || drag.pointerId(slotIndex) != mMoveQueuePointerIds[queueIndex]) {
                    continue;
                }
                if (!drag.dragSlot(context, slotIndex, mMoveQueueWorldX[queueIndex],
                            mMoveQueueWorldY[queueIndex], 0.0)) {
                    drag.endDragSlot(context, slotIndex);
                } else {
                    drag.setWorldPosition(slotIndex, mMoveQueueWorldX[queueIndex],
                            mMoveQueueWorldY[queueIndex]);
                    changed = true;
                }
                break;
            }
        }

        for (int queueIndex = 0; queueIndex < mUpQueueCount; queueIndex++) {
            for (int slotIndex = 0;
                    slotIndex < MicroscopeDragController::MAX_POINTERS; slotIndex++) {
                if (!drag.isDragging(slotIndex) || drag.pointerId(slotIndex) != mUpQueuePointerIds[queueIndex]) {
                    continue;
                }
                drag.endDragSlot(context, slotIndex);
                changed = true;
                break;
            }
            if (mDownPointerCount > 0) {
                mDownPointerCount--;
            }
        }
    }

    mDownQueueCount = 0;
    mMoveQueueCount = 0;
    mUpQueueCount = 0;

    for (int slotIndex = 0; slotIndex < MicroscopeDragController::MAX_POINTERS;
            slotIndex++) {
        if (!drag.isDragging(slotIndex)) {
            continue;
        }
        if (!drag.dragSlot(context, slotIndex, drag.worldX(slotIndex),
                    drag.worldY(slotIndex), dragDeltaTime)) {
            drag.endDragSlot(context, slotIndex);
        } else {
            changed = true;
        }
    }
    return changed;
}
