#pragma once

enum class GenomePreviewBaseMode {
    Cells,
    Tree
};

struct GenomePreviewViewState {
    GenomePreviewBaseMode baseMode = GenomePreviewBaseMode::Cells;
    bool gridEnabled = true;
    bool ghostEnabled = false;
    int selectedGeneIndex = -1;

    [[nodiscard]] constexpr bool directionArrowEnabled() const noexcept {
        return baseMode == GenomePreviewBaseMode::Cells;
    }
};
