#pragma once

#include "app/NativeWorldContext.h"

class MicroscopeSelectionController {
public:
    bool hasSelectedCell() const noexcept;
    void reset() noexcept;
    void syncFromViewport(NativeWorldContext context);
    int update(NativeWorldContext context);

private:
    void clear() noexcept;

    bool mHasSelectedCell = false;
    int mSelectedCellId = -1;
    double mSelectedCellDeltaX = 0.0;
    double mSelectedCellDeltaY = 0.0;
};
