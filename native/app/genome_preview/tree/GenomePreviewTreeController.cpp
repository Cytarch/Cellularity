#include "app/genome_preview/tree/GenomePreviewTreeController.h"

#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "app/genome_preview/tree/GenomePreviewTreeLayout.h"
#include "app/genome_preview/tree/GenomePreviewTreeRenderWorld.h"
#include "app/genome_preview/tree/GenomePreviewTreeRenderer.h"
#include "app/genome_preview/tree/GenomePreviewTreeSelection.h"
#include "app/genome_preview/tree/GenomePreviewTreeState.h"

GenomePreviewTreeController::GenomePreviewTreeController()
        : mState(std::make_unique<GenomePreviewTreeState>()),
          mLayout(std::make_unique<GenomePreviewTreeLayout>()),
          mRenderer(std::make_unique<GenomePreviewTreeRenderer>()),
          mRenderWorld(std::make_unique<GenomePreviewTreeRenderWorld>()),
          mSelection(std::make_unique<GenomePreviewTreeSelection>()) {
}

GenomePreviewTreeController::~GenomePreviewTreeController() = default;

void GenomePreviewTreeController::setEnabled(bool enabled) {
    mEnabled = enabled;
}

bool GenomePreviewTreeController::enabled() const {
    return mEnabled;
}

void GenomePreviewTreeController::reset(SimulationWorld& world) {
    mLayout->reset();
    mState->reset(world);
}

void GenomePreviewTreeController::beginStep(SimulationWorld& world) {
    mState->beginStep(world);
}

void GenomePreviewTreeController::recordSplits(SimulationWorld& world) {
    mState->recordSplits(world);
}

int GenomePreviewTreeController::nodeCount() const {
    return mState->nodeCount();
}

int GenomePreviewTreeController::draw(const GenomePreviewDrawContext& drawContext,
        int selectedGeneIndex) {
    const SimulationWorld& world = drawContext.world;
    NativeSimulationWorldRenderer& renderer = drawContext.synchronousRenderer;
    std::vector<GenomePreviewTreeNode>& nodes = mState->mutableNodes();
    mLayout->update(world, nodes);
    const SimulationWorld* renderWorld = mRenderWorld->update(world, nodes, renderer);
    if (renderWorld == nullptr) {
        return 0;
    }
    return mRenderer->draw(drawContext,
            *renderWorld,
            nodes,
            mLayout->lineVertices(),
            selectedGeneIndex);
}

int GenomePreviewTreeController::selectGeneAt(const SimulationWorld& world, double worldX, double worldY) {
    std::vector<GenomePreviewTreeNode>& nodes = mState->mutableNodes();
    mLayout->update(world, nodes);
    return mSelection->selectGeneAt(nodes, worldX, worldY);
}
