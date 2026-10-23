#include "app/microscope/interaction/MicroscopeWorldInteractionState.h"

#include "app/microscope/interaction/MicroscopeDragController.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "app/microscope/interaction/MicroscopeTapController.h"

namespace cellularity::microscope {

MicroscopeWorldInteractionState::MicroscopeWorldInteractionState()
        : mDrag(std::make_unique<MicroscopeDragController>()),
          mSelection(std::make_unique<MicroscopeSelectionController>()),
          mTap(std::make_unique<MicroscopeTapController>()) {}

MicroscopeWorldInteractionState::~MicroscopeWorldInteractionState() = default;

MicroscopeDragController& MicroscopeWorldInteractionState::drag() noexcept {
    return *mDrag;
}

const MicroscopeDragController&
MicroscopeWorldInteractionState::drag() const noexcept {
    return *mDrag;
}

MicroscopeSelectionController&
MicroscopeWorldInteractionState::selection() noexcept {
    return *mSelection;
}

const MicroscopeSelectionController&
MicroscopeWorldInteractionState::selection() const noexcept {
    return *mSelection;
}

MicroscopeTapController& MicroscopeWorldInteractionState::tap() noexcept {
    return *mTap;
}

void MicroscopeWorldInteractionState::clearDragging() noexcept {
    mDrag->clear();
}

void MicroscopeWorldInteractionState::resetForWorldReplacement() noexcept {
    mDrag->clear();
    mSelection->reset();
}

bool MicroscopeWorldInteractionState::hasDraggingPointers() const noexcept {
    return mDrag->hasDraggingPointers();
}

bool MicroscopeWorldInteractionState::hasSelectedCell() const noexcept {
    return mSelection->hasSelectedCell();
}

void MicroscopeWorldInteractionState::setToolOrdinal(int toolOrdinal) noexcept {
    mToolOrdinal = toolOrdinal;
}

int MicroscopeWorldInteractionState::toolOrdinal() const noexcept {
    return mToolOrdinal;
}

} // namespace cellularity::microscope
