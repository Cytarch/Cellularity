#include "app/microscope/interaction/MicroscopeSelectionController.h"

bool MicroscopeSelectionController::hasSelectedCell() const noexcept {
    return mHasSelectedCell;
}

void MicroscopeSelectionController::reset() noexcept {
    clear();
}

void MicroscopeSelectionController::syncFromViewport(
        NativeWorldContext context) {
    SimulationWorld& world = context.world;
    int selectedCellId = world.mCellData.getIdFromSelectTag(1);
    if (selectedCellId == -1 || world.mCellData.mIsDead[selectedCellId]) {
        clear();
        return;
    }

    mHasSelectedCell = true;
    mSelectedCellId = selectedCellId;
    mSelectedCellDeltaX = static_cast<double>(context.viewport.cameraX()) - world.mCellData.mPositionX[selectedCellId];
    mSelectedCellDeltaY = static_cast<double>(context.viewport.cameraY()) - world.mCellData.mPositionY[selectedCellId];
}

int MicroscopeSelectionController::update(NativeWorldContext context) {
    SimulationWorld& world = context.world;
    int selectedCellId = world.mCellData.getIdFromSelectTag(1);
    if (selectedCellId == -1 || world.mCellData.mIsDead[selectedCellId]) {
        clear();
        return -1;
    }

    if (!mHasSelectedCell || mSelectedCellId != selectedCellId) {
        mHasSelectedCell = true;
        mSelectedCellId = selectedCellId;
        mSelectedCellDeltaX = static_cast<double>(context.viewport.cameraX()) - world.mCellData.mPositionX[selectedCellId];
        mSelectedCellDeltaY = static_cast<double>(context.viewport.cameraY()) - world.mCellData.mPositionY[selectedCellId];
    }

    mSelectedCellDeltaX *= 0.8;
    mSelectedCellDeltaY *= 0.8;
    context.viewport.setCamera(
            static_cast<float>(world.mCellData.mPositionX[selectedCellId] + mSelectedCellDeltaX),
            static_cast<float>(world.mCellData.mPositionY[selectedCellId] + mSelectedCellDeltaY),
            context.viewport.zoom());
    return selectedCellId;
}

void MicroscopeSelectionController::clear() noexcept {
    mHasSelectedCell = false;
    mSelectedCellId = -1;
    mSelectedCellDeltaX = 0.0;
    mSelectedCellDeltaY = 0.0;
}
