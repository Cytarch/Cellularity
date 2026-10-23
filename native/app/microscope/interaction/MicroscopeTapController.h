#pragma once

#include "app/NativeWorldContext.h"

class MicroscopeInputController;
class MicroscopeSelectionController;

class MicroscopeTapController {
public:
    bool applyQueuedTap(NativeWorldContext context,
            MicroscopeInputController& input,
            MicroscopeSelectionController& selection,
            int toolOrdinal);
};
