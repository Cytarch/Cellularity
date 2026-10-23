#pragma once

#include <memory>

#include "core/SimulationWorld.h"

class NativeSimulationWorldRenderer;
struct GenomePreviewDrawContext;
class GenomePreviewTreeLayout;
class GenomePreviewTreeRenderer;
class GenomePreviewTreeRenderWorld;
class GenomePreviewTreeSelection;
class GenomePreviewTreeState;

class GenomePreviewTreeController {
public:
    GenomePreviewTreeController();
    ~GenomePreviewTreeController();

    void setEnabled(bool enabled);
    bool enabled() const;

    void reset(SimulationWorld& world);
    void beginStep(SimulationWorld& world);
    void recordSplits(SimulationWorld& world);
    int nodeCount() const;

    int draw(const GenomePreviewDrawContext& drawContext, int selectedGeneIndex);
    int selectGeneAt(const SimulationWorld& world, double worldX, double worldY);

private:
    bool mEnabled = false;
    std::unique_ptr<GenomePreviewTreeState> mState;
    std::unique_ptr<GenomePreviewTreeLayout> mLayout;
    std::unique_ptr<GenomePreviewTreeRenderer> mRenderer;
    std::unique_ptr<GenomePreviewTreeRenderWorld> mRenderWorld;
    std::unique_ptr<GenomePreviewTreeSelection> mSelection;
};
