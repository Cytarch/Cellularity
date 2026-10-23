#pragma once

#include <memory>

class MicroscopeDragController;
class MicroscopeSelectionController;
class MicroscopeTapController;

namespace cellularity::microscope {

// Selection and drag state contain world identities and may only be used while
// the simulation owner holds mutable world access.
class MicroscopeWorldInteractionState final {
public:
    MicroscopeWorldInteractionState();
    ~MicroscopeWorldInteractionState();

    MicroscopeWorldInteractionState(const MicroscopeWorldInteractionState&) =
            delete;
    MicroscopeWorldInteractionState&
    operator=(const MicroscopeWorldInteractionState&) = delete;
    MicroscopeWorldInteractionState(MicroscopeWorldInteractionState&&) = delete;
    MicroscopeWorldInteractionState&
    operator=(MicroscopeWorldInteractionState&&) = delete;

    [[nodiscard]] MicroscopeDragController& drag() noexcept;
    [[nodiscard]] const MicroscopeDragController& drag() const noexcept;
    [[nodiscard]] MicroscopeSelectionController& selection() noexcept;
    [[nodiscard]] const MicroscopeSelectionController& selection() const noexcept;
    [[nodiscard]] MicroscopeTapController& tap() noexcept;

    void clearDragging() noexcept;
    void resetForWorldReplacement() noexcept;
    [[nodiscard]] bool hasDraggingPointers() const noexcept;
    [[nodiscard]] bool hasSelectedCell() const noexcept;
    void setToolOrdinal(int toolOrdinal) noexcept;
    [[nodiscard]] int toolOrdinal() const noexcept;

private:
    std::unique_ptr<MicroscopeDragController> mDrag;
    std::unique_ptr<MicroscopeSelectionController> mSelection;
    std::unique_ptr<MicroscopeTapController> mTap;
    int mToolOrdinal = 0;
};

} // namespace cellularity::microscope
