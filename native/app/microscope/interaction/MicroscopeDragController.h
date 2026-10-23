#pragma once

#include <array>

#include "app/NativeWorldContext.h"

class MicroscopeDragController {
public:
    static constexpr int MAX_POINTERS = 20;

    void clear();
    bool hasDraggingPointers() const;
    int draggingPointerCount() const;
    bool isDragging(int slotIndex) const;
    int pointerId(int slotIndex) const;
    double worldX(int slotIndex) const;
    double worldY(int slotIndex) const;
    void setWorldPosition(int slotIndex, double worldX, double worldY);

    bool beginDragSlot(NativeWorldContext context,
            int slotIndex,
            int pointerId,
            double worldX,
            double worldY);
    bool dragSlot(NativeWorldContext context,
            int slotIndex,
            double worldX,
            double worldY,
            double deltaTime);
    void endDragSlot(NativeWorldContext context, int slotIndex);

private:
    std::array<bool, MAX_POINTERS> mIsPointerDragging{};
    std::array<int, MAX_POINTERS> mDraggingPointerIds{};
    std::array<double, MAX_POINTERS> mDraggingWorldX{};
    std::array<double, MAX_POINTERS> mDraggingWorldY{};
    std::array<double, MAX_POINTERS> mDraggingOffsetX{};
    std::array<double, MAX_POINTERS> mDraggingOffsetY{};
    int mDraggingPointerCount = 0;
};
