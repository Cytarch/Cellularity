#include "app/microscope/interaction/MicroscopeDragController.h"

#include <cmath>

#include "core/CellData.h"

void MicroscopeDragController::clear() {
    mIsPointerDragging.fill(false);
    mDraggingPointerCount = 0;
}

bool MicroscopeDragController::hasDraggingPointers() const {
    return mDraggingPointerCount > 0;
}

int MicroscopeDragController::draggingPointerCount() const {
    return mDraggingPointerCount;
}

bool MicroscopeDragController::isDragging(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MAX_POINTERS && mIsPointerDragging[slotIndex];
}

int MicroscopeDragController::pointerId(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MAX_POINTERS ? mDraggingPointerIds[slotIndex] : -1;
}

double MicroscopeDragController::worldX(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MAX_POINTERS ? mDraggingWorldX[slotIndex] : 0.0;
}

double MicroscopeDragController::worldY(int slotIndex) const {
    return slotIndex >= 0 && slotIndex < MAX_POINTERS ? mDraggingWorldY[slotIndex] : 0.0;
}

void MicroscopeDragController::setWorldPosition(int slotIndex, double worldX, double worldY) {
    if (slotIndex < 0 || slotIndex >= MAX_POINTERS) {
        return;
    }
    mDraggingWorldX[slotIndex] = worldX;
    mDraggingWorldY[slotIndex] = worldY;
}

bool MicroscopeDragController::beginDragSlot(NativeWorldContext context,
        int slotIndex,
        int pointerId,
        double worldX,
        double worldY) {
    if (slotIndex < 0 || slotIndex >= MAX_POINTERS) {
        return false;
    }
    CellData& cellData = context.world.mCellData;
    int cellId = cellData.getCellIndex(worldX, worldY);
    if (cellId == -1) {
        return false;
    }
    cellData.mSelectTag[cellId] = CellData::CELL_AUX2_DRAG_START + slotIndex;
    mDraggingOffsetX[slotIndex] = worldX - cellData.mPositionX[cellId];
    mDraggingOffsetY[slotIndex] = worldY - cellData.mPositionY[cellId];
    mDraggingWorldX[slotIndex] = worldX;
    mDraggingWorldY[slotIndex] = worldY;
    mDraggingPointerIds[slotIndex] = pointerId;
    mIsPointerDragging[slotIndex] = true;
    mDraggingPointerCount++;
    return true;
}

bool MicroscopeDragController::dragSlot(NativeWorldContext context,
        int slotIndex,
        double worldX,
        double worldY,
        double deltaTime) {
    if (slotIndex < 0 || slotIndex >= MAX_POINTERS) {
        return false;
    }
    CellData& cellData = context.world.mCellData;
    int cellId = cellData.getIdFromSelectTag(CellData::CELL_AUX2_DRAG_START + slotIndex);
    if (cellId == -1) {
        return false;
    }

    double offsetX = mDraggingOffsetX[slotIndex];
    double offsetY = mDraggingOffsetY[slotIndex];
    double oldX = cellData.mPositionX[cellId];
    double oldY = cellData.mPositionY[cellId];
    double oldAngle = cellData.mAngle[cellId];
    double currentTouchX = oldX + offsetX;
    double currentTouchY = oldY + offsetY;
    double dx = worldX - currentTouchX;
    double dy = worldY - currentTouchY;
    double radiusSq = cellData.mRadius[cellId] * cellData.mRadius[cellId];
    if (radiusSq == 0.0) {
        return false;
    }

    double rotation = 0.5 * ((dy * offsetX - dx * offsetY) / radiusSq);
    double newAngle = oldAngle + rotation;
    double cosRotation = std::cos(rotation);
    double sinRotation = std::sin(rotation);
    double newOffsetX = cosRotation * offsetX - sinRotation * offsetY;
    double newOffsetY = cosRotation * offsetY + sinRotation * offsetX;
    mDraggingOffsetX[slotIndex] = newOffsetX;
    mDraggingOffsetY[slotIndex] = newOffsetY;

    double newX = worldX - newOffsetX;
    double newY = worldY - newOffsetY;
    cellData.mPositionX[cellId] = newX;
    cellData.mPositionY[cellId] = newY;
    cellData.mAngle[cellId] = newAngle;
    if (deltaTime > 0.0) {
        cellData.mIsStuck[cellId] = false;
        cellData.mVelocityX[cellId] = (newX - oldX) / deltaTime;
        cellData.mVelocityY[cellId] = (newY - oldY) / deltaTime;
        cellData.mAngularVelocity[cellId] = (newAngle - oldAngle) / deltaTime;
    }
    context.world.updateGrids();
    return true;
}

void MicroscopeDragController::endDragSlot(NativeWorldContext context, int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_POINTERS || !mIsPointerDragging[slotIndex]) {
        return;
    }
    int cellId = context.world.mCellData.getIdFromSelectTag(CellData::CELL_AUX2_DRAG_START + slotIndex);
    if (cellId != -1) {
        context.world.mCellData.mSelectTag[cellId] = 0;
    }
    mIsPointerDragging[slotIndex] = false;
    if (mDraggingPointerCount > 0) {
        mDraggingPointerCount--;
    }
}
