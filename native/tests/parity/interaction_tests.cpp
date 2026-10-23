#include "app/NativeCellularityController.h"
#include "app/NativeSimulationOwner.h"
#include "app/NativeSimulationStepper.h"
#include "app/NativeWorldEditing.h"
#include "app/NativeWorldSession.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeFrameController.h"
#include "app/microscope/MicroscopeInputController.h"
#include "app/microscope/MicroscopeInputTimeline.h"
#include "app/microscope/MicroscopeSimulationState.h"
#include "app/microscope/interaction/MicroscopeDragController.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "app/microscope/interaction/MicroscopeTapController.h"
#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/RenderContactChord.h"
#include "core/SimulationRandom.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cellularity::tests;

static_assert(!std::is_copy_constructible_v<NativeWorldSession::ReadAccess>);
static_assert(!std::is_copy_constructible_v<NativeWorldSession::WriteAccess>);
static_assert(!std::is_copy_constructible_v<NativeSimulationOwner>);
static_assert(!std::is_move_constructible_v<NativeSimulationOwner>);
static_assert(!std::is_copy_constructible_v<NativeSimulationOwner::ReadAccess>);
static_assert(
        !std::is_copy_constructible_v<NativeSimulationOwner::WriteAccess>);
static_assert(
        std::is_same_v<
                decltype(std::declval<NativeWorldSession::ReadAccess&>().world()),
                const SimulationWorld&>);
static_assert(
        std::is_same_v<
                decltype(std::declval<NativeWorldSession::WriteAccess&>().world()),
                SimulationWorld&>);

namespace {

void testViewportSnapshotOwnsCompleteImmutableValues() {
    const cellularity::render::RenderViewBounds coverage{-1.5f, 1.5f, -1.0f,
            1.0f};
    NativeViewport viewport(1.0f);
    const auto unready = cellularity::app::snapshotViewport(viewport, coverage);
    assertTrue(unready.mvpMatrix == cellularity::app::identityMatrix(),
            "Unready viewport snapshot has a finite identity matrix");

    viewport.setSurfaceSize(2560, 1390);
    viewport.setCamera(0.125f, -0.25f, 2.0f);
    const auto expectedMatrix =
            viewport.worldMatrix(cellularity::app::identityMatrix());
    const auto snapshot = cellularity::app::snapshotViewport(viewport, coverage);
    const std::uint64_t capturedRevision = viewport.revision();
    assertTrue(
            snapshot.revision == capturedRevision && snapshot.coverageRevision == capturedRevision && snapshot.mvpMatrix == expectedMatrix && snapshot.cameraX == viewport.cameraX() && snapshot.cameraY == viewport.cameraY() && snapshot.zoom == viewport.zoom() && snapshot.viewWidth == viewport.viewWidth() && snapshot.viewHeight == viewport.viewHeight() && snapshot.surfaceWidth == 2560 && snapshot.surfaceHeight == 1390 && snapshot.coverageBounds.minX == coverage.minX && snapshot.coverageBounds.maxX == coverage.maxX,
            "Viewport snapshot owns camera, surface, matrix, revision, and coverage");

    viewport.setCamera(-0.5f, 0.5f, 4.0f);
    assertTrue(snapshot.revision == capturedRevision && snapshot.mvpMatrix == expectedMatrix && snapshot.cameraX != viewport.cameraX(),
            "Viewport snapshot is unchanged after its source mutates");
}

void testWorldEpochAndViewportRevisionAreMonotonicIdentities() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    NativeWorldSession firstSession(false, environment);
    NativeWorldSession secondSession(false, environment);

    std::uint64_t firstEpoch = 0;
    {
        auto access = firstSession.read();
        firstEpoch = access.worldEpoch();
        assertTrue(firstEpoch != 0, "Native world epoch is never zero");
    }
    {
        auto access = firstSession.write();
        assertTrue(access.worldEpoch() == firstEpoch,
                "Native world epoch is stable across read/write access");
        const std::uint64_t replacementEpoch = access.markWorldContentsReplaced();
        assertTrue(replacementEpoch != firstEpoch && replacementEpoch != 0,
                "In-place world replacement advances the epoch barrier");
    }
    {
        auto access = secondSession.read();
        assertTrue(access.worldEpoch() != firstEpoch,
                "Each native world lifetime has a distinct epoch");
    }

    NativeViewport viewport(1.0f);
    const std::uint64_t initialRevision = viewport.revision();
    viewport.setSurfaceSize(100, 80);
    const std::uint64_t surfaceRevision = viewport.revision();
    viewport.setCamera(0.1f, -0.2f, 2.0f);
    const std::uint64_t cameraRevision = viewport.revision();
    assertTrue(initialRevision != 0, "Native viewport revision is never zero");
    assertTrue(surfaceRevision > initialRevision,
            "Surface mutation advances viewport revision");
    assertTrue(cameraRevision > surfaceRevision,
            "Camera mutation advances viewport revision");
    assertTrue(viewport.revision() == cameraRevision,
            "Viewport reads do not advance revision");
}

void testGenomePreviewResetClearsStaleTopology() {
    Environment environment = Environment::constructDefaultEnvironment(4, 1, 1.0);
    SimulationWorld world(true, environment);
    CellData& cellData = world.mCellData;
    int first = addAliveCell(cellData, CellType::PHAGOCYTE);
    int second = addAliveCell(cellData, CellType::GLUEOCYTE);
    const CellRuntimeId firstRuntimeId = cellData.runtimeId(first);
    const CellRuntimeId secondRuntimeId = cellData.runtimeId(second);
    cellData.mTopology.addCellLink(
            first, second, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.5f, 0.0f);
    cellData.mTopology.addBoundaryLink(first, 0.0, 0.0, 0.0, 0.5f, 0.0f);
    world.mFoodData.resizeActiveFoodCount(1);
    world.mDeadSplashData.resizeActiveDeadSplashCount(1);
    world.mTime = 12.0;
    world.mEnvironment.mLightAngle = 0.25;

    resetGenomePreviewWorld(world);

    assertTrue(cellData.mCellCount == 0, "Genome preview reset clears cells");
    assertTrue(cellData.mTopology.edgeCount() == 0,
            "Genome preview reset clears topology edges");
    assertTrue(cellData.mTopology.boundaryCount() == 0,
            "Genome preview reset clears topology boundaries");
    assertTrue(cellData.mTopology.linkCount(first) == 0,
            "Genome preview reset clears stale first adjacency");
    assertTrue(cellData.mTopology.linkCount(second) == 0,
            "Genome preview reset clears stale second adjacency");
    assertTrue(world.mFoodData.mFoodCount == 0,
            "Genome preview reset clears foods");
    assertTrue(world.mDeadSplashData.mDeadSplashCount == 0,
            "Genome preview reset clears dead splashes");
    assertNear(0.0, world.mTime, 0.0, "Genome preview reset clears time");
    assertNear(Environment::PI / 2.0, world.mEnvironment.mLightAngle, 0.0,
            "Genome preview reset restores light angle");

    auto genome = cellularity::tests::canonicalDefaultGenomeData();
    cellularity::GeneBuilder(genome[0]).setCellType(CellType::PHAGOCYTE);
    cellularity::GeneBuilder(genome[0]).setInitialCompatibilityFlag(true);
    assertTrue(addUserCellToWorld(world, 0.0, 0.0, genome),
            "Genome preview reset allows new user cell");
    assertTrue(cellData.mCellCount == 1, "Genome preview reset new cell count");
    assertTrue(cellData.runtimeId(0) != firstRuntimeId && cellData.runtimeId(0) != secondRuntimeId,
            "Genome preview reset never reuses retired runtime identities");
    assertTrue(cellData.mTopology.edgeCount() == 0,
            "Genome preview reset new cell has no stale edges");
    assertTrue(cellData.mTopology.boundaryCount() == 0,
            "Genome preview reset new cell has no stale boundaries");
    assertTrue(cellData.mTopology.linkCount(0) == 0,
            "Genome preview reset new cell has no stale links");
}

void testWorldResetContentsClearsStaleTopology() {
    Environment environment = Environment::constructDefaultEnvironment(4, 2, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int first = addAliveCell(cellData, CellType::PHAGOCYTE);
    int second = addAliveCell(cellData, CellType::GLUEOCYTE);
    cellData.mTopology.addCellLink(
            first, second, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.5f, 0.0f);
    cellData.mTopology.addBoundaryLink(first, 0.0, 0.0, 0.0, 0.5f, 0.0f);
    world.mFoodData.addFood(0.0, 0.0, 0.1f, 0.0f, world.mEnvironment);
    world.mDeadSplashData.resizeActiveDeadSplashCount(1);
    world.mTime = 12.0;
    world.mEnvironment.mLightAngle = 0.25;
    world.updateGrids();
    assertTrue(world.foodSpatialIndexIsFresh(),
            "World reset fixture starts with a fresh Food index");

    resetWorldContents(world);

    assertTrue(cellData.mCellCount == 0, "World reset clears cells");
    assertTrue(cellData.mTopology.edgeCount() == 0,
            "World reset clears topology edges");
    assertTrue(cellData.mTopology.boundaryCount() == 0,
            "World reset clears topology boundaries");
    assertTrue(cellData.mTopology.linkCount(first) == 0,
            "World reset clears first stale adjacency");
    assertTrue(cellData.mTopology.linkCount(second) == 0,
            "World reset clears second stale adjacency");
    assertTrue(world.mFoodData.mFoodCount == 0, "World reset clears foods");
    assertTrue(world.mDeadSplashData.mDeadSplashCount == 0,
            "World reset clears dead splashes");
    assertNear(0.0, world.mTime, 0.0, "World reset clears time");
    assertNear(Environment::PI / 2.0, world.mEnvironment.mLightAngle, 0.0,
            "World reset restores light angle");
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "World reset invalidates the retired Food index");
    world.updateGrids();
    assertTrue(world.foodSpatialIndexIsFresh(),
            "Production reset grid update publishes a fresh empty Food index");
}

void testUserAddedCellsStartWithZeroAge() {
    auto genome = cellularity::tests::canonicalDefaultGenomeData();
    cellularity::GeneBuilder(genome[0]).setCellType(CellType::PHAGOCYTE);
    cellularity::GeneBuilder(genome[0]).setInitialCompatibilityFlag(true);

    Environment previewEnvironment =
            Environment::setupPreviewSimulationEnvironment(4);
    SimulationWorld previewWorld(true, previewEnvironment);
    assertTrue(addUserCellToWorld(previewWorld, 0.0, 0.0, genome),
            "Genome preview cell add succeeds");
    assertNear(0.0, previewWorld.mCellData.mAge[0], 0.0,
            "Genome preview cell starts at zero age");

    Environment dishEnvironment =
            Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld dishWorld(false, dishEnvironment);
    assertTrue(addUserCellToWorld(dishWorld, 0.0, 0.0, genome),
            "User cell add succeeds");
    assertNear(0.0, dishWorld.mCellData.mAge[0], 0.0,
            "User-added dish cell starts at zero age");
}

cellularity::GenomeData makeDynamicWorldEditingGenome(
        std::size_t geneCount) {
    cellularity::GenomeData genome(geneCount);
    const std::size_t initialIndex = geneCount - 1;
    for (std::size_t index = 0; index < geneCount; ++index) {
        const cellularity::GeneIndex owner(
                static_cast<cellularity::GeneIndex::Value>(index));
        const auto gene = cellularity::materializeCanonicalGene(
                cellularity::CanonicalGeneDefaults::value(), owner, geneCount,
                index == initialIndex);
        assertTrue(gene.has_value(),
                "Dynamic world-editing Gene materializes");
        genome[index] = *gene;
        cellularity::GeneBuilder builder(genome[index]);
        builder.setCellType(index == initialIndex ? CellType::GLUEOCYTE
                                                  : CellType::PHAGOCYTE);
        builder.setColor(0, static_cast<float>(index + 1) / static_cast<float>(geneCount + 1));
    }
    return genome;
}

void testWorldEditingAcceptsDynamicGenomeRecords() {
    constexpr std::array<std::size_t, 2> lengths{2, 150};
    for (const std::size_t length : lengths) {
        Environment environment =
                Environment::constructDefaultEnvironment(2, 0, 1.0);
        SimulationWorld world(false, environment);
        const cellularity::GenomeData genome =
                makeDynamicWorldEditingGenome(length);

        assertTrue(addUserCellToWorld(world, 0.0, 0.0, genome),
                "World editing adds a dynamic Genome");

        const CellData& cells = world.mCellData;
        assertTrue(cells.mCellCount == 1 && cells.geneCount(0) == length,
                "World editing preserves dynamic Genome length");
        assertTrue(cells.activeGeneIndex(0) == cellularity::GeneIndex(static_cast<cellularity::GeneIndex::Value>(length - 1)),
                "World editing selects dynamic initial Gene");
        assertTrue(cells.mActiveCellType[0] == CellType::GLUEOCYTE,
                "World editing refreshes dynamic active phenotype");
        assertTrue(cells.genomePool().validateInvariants().isValid() && cells.genePool().validateInvariants().isValid(),
                "World editing preserves dynamic pool ownership");
    }
}

void testCellTapOwnsDynamicGenomeUntilApplied() {
    Environment environment =
            Environment::constructDefaultEnvironment(2, 0, 1.0);
    SimulationWorld world(false, environment);
    NativeViewport viewport(1.0f);
    viewport.setSurfaceSize(100, 100);
    viewport.setCamera(0.0f, 0.0f, 1.0f);
    MicroscopeInputController input;
    MicroscopeTapController tap;
    MicroscopeSelectionController selection;
    auto genome = cellularity::tests::canonicalDefaultGenomeData();
    cellularity::GeneBuilder(genome[0]).setCellType(CellType::GLUEOCYTE);

    input.queueTap(viewport, 50.0f, 50.0f, &genome);
    cellularity::GeneBuilder(genome[0]).setCellType(CellType::PHAGOCYTE);
    assertTrue(tap.applyQueuedTap(NativeWorldContext{world, viewport}, input,
                       selection, 0),
            "CELL tap applies an owned queued Genome");

    assertTrue(world.mCellData.mCellCount == 1 && world.mCellData.geneCount(0) == genome.size(),
            "CELL tap imports the dynamic Genome payload");
    assertTrue(world.mCellData.mActiveCellType[0] == CellType::GLUEOCYTE,
            "CELL tap is independent from later caller mutation");
    assertTrue(world.mCellData.genomePool().validateInvariants().isValid() && world.mCellData.genePool().validateInvariants().isValid(),
            "CELL tap preserves pooled Genome ownership");
}

void testAddUserCellAtCapacityKeepsTopologyValid() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int first = addAliveCell(cellData, CellType::PHAGOCYTE);
    int second = addAliveCell(cellData, CellType::GLUEOCYTE);
    const std::array<CellRuntimeId, 2> originalRuntimeIds{
            cellData.runtimeId(first), cellData.runtimeId(second)
    };
    cellData.mTopology.addCellLink(
            first, second, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.5f, 0.0f);
    assertTrue(cellData.mTopology.edgeCount() == 1,
            "Add user cell capacity test seeds topology");

    auto genome = cellularity::tests::canonicalDefaultGenomeData();
    cellularity::GeneBuilder(genome[0]).setCellType(CellType::PHAGOCYTE);
    cellularity::GeneBuilder(genome[0]).setInitialCompatibilityFlag(true);
    SimulationRandom::global.setSeed(1337);

    assertTrue(addUserCellToWorld(world, 0.0, 0.0, genome),
            "Add user cell at capacity succeeds");

    assertTrue(cellData.mCellCount == 2,
            "Add user cell at capacity keeps capacity");
    int retainedIdentityCount = 0;
    int freshIdentityCount = 0;
    for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
        const CellRuntimeId runtimeId = cellData.runtimeId(cellIndex);
        if (runtimeId == originalRuntimeIds[0] || runtimeId == originalRuntimeIds[1]) {
            ++retainedIdentityCount;
        } else if (isEntityCellRuntimeId(runtimeId)) {
            ++freshIdentityCount;
        }
    }
    assertTrue(retainedIdentityCount == 1 && freshIdentityCount == 1,
            "Capacity replacement preserves the moved Cell and identifies the "
            "new Cell");
    assertTrue(cellData.mTopology.validate(),
            "Add user cell at capacity keeps topology valid");
    assertTrue(cellData.mTopology.edgeCount() == 0,
            "Add user cell at capacity removes stale edge");
    assertTrue(cellData.mTopology.linkCount(0) == 0,
            "Add user cell at capacity clears first adjacency");
    assertTrue(cellData.mTopology.linkCount(1) == 0,
            "Add user cell at capacity clears second adjacency");
}

void testNativeCellularityControllerFreezeRedrawDoesNotAdvanceSimulation() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(100, 100);

    auto worldAccess = controller.writeWorld();
    SimulationWorld& world = worldAccess.world();
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mMass[cell] = CellData::MAX_MASS;
    cellData.mRadius[cell] =
            std::sqrt(cellData.mMass[cell] / CellData::MASS_PER_AREA);
    cellData.refreshRadiusSq(cell);
    cellData.mAge[cell] = CellData::AGE_OF_CONSENT + 0.1;
    cellData.editActiveGene(cell, [](cellularity::GeneBuilder& splitGene) {
        splitGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        splitGene.setSplitRatio(0.5f);
        splitGene.setChild1GeneIndex(cellularity::GeneIndex(0));
        splitGene.setChild2GeneIndex(cellularity::GeneIndex(0));
    });
    world.updateGrids();

    controller.setMicroscopeTemperatureMode(0);
    controller.stepMicroscopeFrame();

    assertTrue(cellData.mCellCount == 1,
            "Freeze redraw does not split a ready cell");
    assertNear(CellData::AGE_OF_CONSENT + 0.1, cellData.mAge[cell], 0.0,
            "Freeze redraw does not advance cell age");
}

void testNativeCellularityControllerMicroscopeStepCountsCompletedUpdate() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(100, 100);

    controller.setMicroscopeTemperatureMode(1);
    controller.stepMicroscopeFrame();

    assertTrue(
            controller.completedUpdateCount() == 1,
            "Native controller microscope observe step counts completed update");
}

void testMicroscopeModeContractsMatchCaptureTable() {
    using Controller = MicroscopeFrameController;

    const Controller::ModeContract freeze =
            Controller::modeContract(Controller::MODE_FREEZE);
    assertTrue(freeze.updatePath == Controller::UpdatePath::None && freeze.updateCadence == Controller::UpdateCadence::Disabled && freeze.captureCadence == Controller::CaptureCadence::DirtyImmediate && freeze.captureStage == Controller::CaptureStage::StableWorld && !freeze.usesExpandedCoverage,
            "Freeze mode contract");
    assertTrue(Controller::captureDue(freeze, false, false, 0),
            "Freeze captures initial dirty state immediately");
    assertTrue(!Controller::captureDue(freeze, true, false, 0),
            "Freeze does not recapture clean stable state");
    assertTrue(Controller::captureDue(freeze, true, true, 0),
            "Freeze captures a changed viewport immediately");

    for (const int mode : {Controller::MODE_OBSERVE, Controller::MODE_SLOW}) {
        const Controller::ModeContract contract = Controller::modeContract(mode);
        assertTrue(contract.updatePath == Controller::UpdatePath::SplitRendering && contract.updateCadence == Controller::UpdateCadence::DisplayPaced60Hz && contract.captureCadence == Controller::CaptureCadence::EveryDisplayStep && contract.captureStage == Controller::CaptureStage::AfterUpdatePhaseOne && contract.usesExpandedCoverage,
                "Observe/Slow mode contract");
        assertTrue(Controller::captureDue(contract, true, false, 0),
                "Observe/Slow captures every display-paced step");
    }

    const Controller::ModeContract incubate =
            Controller::modeContract(Controller::MODE_INCUBATE);
    assertTrue(
            incubate.updatePath == Controller::UpdatePath::FullWithoutRendering && incubate.updateCadence == Controller::UpdateCadence::ConfiguredTarget && incubate.captureCadence == Controller::CaptureCadence::Fixed30Hz && incubate.captureStage == Controller::CaptureStage::AfterCompletedUpdate && incubate.usesExpandedCoverage,
            "Incubate mode contract");
    assertTrue(
            !Controller::captureDue(incubate, true, false,
                    Controller::INCUBATE_CAPTURE_INTERVAL_NANOS - 1),
            "Incubate stays below 30 Hz before exact cadence boundary");
    assertTrue(
            Controller::captureDue(incubate, true, false,
                    Controller::INCUBATE_CAPTURE_INTERVAL_NANOS),
            "Incubate captures at exact 30 Hz cadence boundary");
    assertTrue(Controller::captureDue(incubate, false, false, 0) && Controller::captureDue(incubate, true, true, 0),
            "Incubate initial and viewport-dirty captures are immediate");

    const Controller::ModeContract noRender =
            Controller::modeContract(Controller::MODE_INCUBATE_NO_RENDER);
    assertTrue(
            noRender.updatePath == Controller::UpdatePath::FullWithoutRendering && noRender.updateCadence == Controller::UpdateCadence::ConfiguredTarget && noRender.captureCadence == Controller::CaptureCadence::Disabled && noRender.captureStage == Controller::CaptureStage::AfterCompletedUpdate && !noRender.usesExpandedCoverage,
            "Incubate No Render mode contract");
    assertTrue(!Controller::captureDue(noRender, false, true,
                       std::numeric_limits<std::int64_t>::max()),
            "Incubate No Render never captures a source frame");
}

void testMicroscopeFrameControllerCapturesAtExactModeSemanticPoints() {
    using Controller = MicroscopeFrameController;

    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    SimulationWorld world(false, environment);
    const int visibleCell = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    constexpr float baseGreen = 0.25f;
    world.mCellData.mPositionX[visibleCell] = 0.0;
    world.mCellData.mPositionY[visibleCell] = 0.0;
    world.mCellData.mColor[visibleCell][1] = baseGreen;
    world.mCellData.mToxin[visibleCell] = 1.0f;
    world.mCellData.mToxication[visibleCell] = 0.0f;
    world.updateGrids();
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(100, 80);
    viewport.setCamera(0.0f, 0.0f, 1.0f);
    NativeSimulationStepper simulation;
    cellularity::microscope::MicroscopeSimulationState simulationState;
    MicroscopeInputController input;
    cellularity::microscope::MicroscopeInputTimeline inputTimeline;
    MicroscopeDragController drag;
    MicroscopeTapController tap;
    MicroscopeSelectionController selection;
    Controller frame;
    cellularity::render::RenderSourceFrame sourceFrame;
    cellularity::render::RenderSourceCapture sourceCapture(
            environment.maxCellCount, sourceFrame);

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    assertTrue(!simulationState.schedulerSnapshot().has_value(),
            "Scheduler shadow is opt-in and disabled by default");
    simulationState.enableScheduler(
            cellularity::simulation::SimulationSchedulerLimits(
                    std::chrono::milliseconds(10), std::chrono::milliseconds(100),
                    std::chrono::milliseconds(1), 4096, 1.25),
            cellularity::simulation::simulationSchedulerNow());
#endif

    struct CaptureObservation {
        Controller::CaptureStage stage;
        int worldUpdateStage;
        std::uint64_t completedUpdates;
        float margin;
        float capturedCellGreen;
    };

    std::vector<CaptureObservation> captures;
    const auto capture = [&](const SimulationWorld& capturedWorld,
                                 const NativeViewport&, float margin,
                                 Controller::CaptureStage stage) {
        cellularity::render::RenderFrameCaptureMetadata metadata;
        switch (stage) {
            case Controller::CaptureStage::StableWorld:
                metadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
                break;
            case Controller::CaptureStage::AfterUpdatePhaseOne:
                metadata.stage =
                        cellularity::render::RenderCaptureStage::AfterUpdatePhaseOne;
                break;
            case Controller::CaptureStage::AfterCompletedUpdate:
                metadata.stage =
                        cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
                break;
        }
        const cellularity::render::RenderViewBounds bounds{-1.0f, 1.0f, -1.0f,
                1.0f};
        sourceCapture.capture(capturedWorld, bounds, metadata, sourceFrame);
        const auto sourceCell = std::find_if(
                sourceFrame.cells.begin(), sourceFrame.cells.end(),
                [&](const cellularity::render::RenderSourceCell& cell) {
                    return cell.sourceCellIndex == visibleCell;
                });
        assertTrue(sourceCell != sourceFrame.cells.end(),
                "Frame callback captures the visible verification Cell");
        captures.push_back({stage, capturedWorld.getUpdateStage(),
                simulation.completedUpdateCount(), margin,
                sourceCell->green});
    };
    const auto step = [&]() {
        return frame.stepFrame(NativeWorldContext{world, viewport}, simulation,
                simulationState, capture, input, inputTimeline, drag,
                tap, selection, 0, false);
    };
    const auto setMode = [&](int temperatureMode) {
        if (simulationState.setTemperatureMode(temperatureMode)) {
            frame.onTemperatureModeChanged();
        }
    };
    const auto currentRenderGreen = [&]() {
        return world.mCellData.mColor[visibleCell][1] + world.mCellData.mToxication[visibleCell];
    };

    assertTrue(step(), "Freeze emits its initial dirty capture");
    assertTrue(captures.size() == 1 && captures.back().stage == Controller::CaptureStage::StableWorld && captures.back().worldUpdateStage == SimulationWorld::UPDATE_STAGE_CAN_TOUCH && captures.back().completedUpdates == 0 && captures.back().margin == 0.0f,
            "Freeze capture uses stable world with zero margin and no update");
    assertTrue(!step() && captures.size() == 1,
            "Freeze clean redraw does not produce another capture");
    frame.invalidateFrameData();
    assertTrue(step() && captures.size() == 2 && captures.back().stage == Controller::CaptureStage::StableWorld,
            "Freeze invalidation produces an immediate stable capture");

    const float expandedMargin =
            std::max(viewport.viewWidth(), viewport.viewHeight()) * 0.1f;
    setMode(Controller::MODE_OBSERVE);
    assertTrue(step(), "Observe produces a frame on its display step");
    assertTrue(captures.back().stage == Controller::CaptureStage::AfterUpdatePhaseOne && captures.back().worldUpdateStage != SimulationWorld::UPDATE_STAGE_CAN_TOUCH && captures.back().completedUpdates == 0 && captures.back().margin == expandedMargin && simulation.completedUpdateCount() == 1 && world.getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH,
            "Observe captures between update1(true) and update2(true)");
    assertNear(baseGreen, captures.back().capturedCellGreen, 1e-7,
            "Observe source content excludes same-step metabolism");
    assertTrue(world.mCellData.mToxication[visibleCell] > 0.0f && std::abs(captures.back().capturedCellGreen - currentRenderGreen()) > 1e-4f,
            "Observe world advances metabolism only after source capture");

    setMode(Controller::MODE_SLOW);
    const float toxicationBeforeSlow =
            world.mCellData.mToxication[visibleCell];
    const float renderGreenBeforeSlow = currentRenderGreen();
    assertTrue(step(), "Slow produces a frame on its display step");
    assertTrue(captures.back().stage == Controller::CaptureStage::AfterUpdatePhaseOne && captures.back().completedUpdates == 1 && captures.back().margin == expandedMargin && simulation.completedUpdateCount() == 2,
            "Slow preserves the Observe split capture point");
    assertNear(renderGreenBeforeSlow, captures.back().capturedCellGreen, 1e-7,
            "Slow source content represents its pre-metabolism world");
    assertTrue(world.mCellData.mToxication[visibleCell] > toxicationBeforeSlow && std::abs(captures.back().capturedCellGreen - currentRenderGreen()) > 1e-4f,
            "Slow world advances metabolism only after source capture");

    setMode(Controller::MODE_INCUBATE);
    simulationState.setLegacyIncubateTargetUps(1.0);
    assertTrue(step(), "Incubate produces its initial keyframe");
    assertTrue(captures.back().stage == Controller::CaptureStage::AfterCompletedUpdate && captures.back().worldUpdateStage == SimulationWorld::UPDATE_STAGE_CAN_TOUCH && captures.back().completedUpdates == 3 && captures.back().margin == expandedMargin && simulation.completedUpdateCount() == 3,
            "Incubate captures only after completed update(false)");

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    auto shadowSnapshot = simulationState.schedulerSnapshot();
    assertTrue(
            shadowSnapshot.has_value() && shadowSnapshot->policy == cellularity::simulation::SimulationRatePolicyKind::MaxThroughput && !shadowSnapshot->requestedTargetUps.has_value() && shadowSnapshot->observedBatchCount == 1 && shadowSnapshot->observedUpdateCount == 1 && simulation.completedUpdateCount() == 3,
            "Scheduler shadow observes Incubate without changing its authoritative "
            "update count");
#endif

    const std::size_t capturesBeforeNoRender = captures.size();
    setMode(Controller::MODE_INCUBATE_NO_RENDER);
    simulationState.setLegacyIncubateTargetUps(1.0);
    assertTrue(!step() && captures.size() == capturesBeforeNoRender && simulation.completedUpdateCount() == 4,
            "Incubate No Render updates simulation without source capture");

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    shadowSnapshot = simulationState.schedulerSnapshot();
    assertTrue(shadowSnapshot.has_value() && shadowSnapshot->policy == cellularity::simulation::SimulationRatePolicyKind::MaxThroughput && shadowSnapshot->observedBatchCount == 2 && shadowSnapshot->observedUpdateCount == 2 && simulation.completedUpdateCount() == 4,
            "No Render remains legacy-authoritative while shadow learning "
            "continues");
    simulationState.disableScheduler();
    assertTrue(!simulationState.schedulerSnapshot().has_value(),
            "Scheduler shadow can be removed without lifecycle work");
#endif
}

void testQueuedTapUsesItsEventTimeViewport() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    SimulationWorld world(false, environment);
    const int cell = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[cell] = 0.0;
    world.mCellData.mPositionY[cell] = 0.0;
    world.mCellData.mRadius[cell] = 0.04;
    world.updateGrids();

    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(100, 100);
    viewport.setCamera(0.0f, 0.0f, 1.0f);
    MicroscopeInputController input;
    MicroscopeSelectionController selection;
    MicroscopeTapController tap;

    input.queueTap(viewport, 50.0f, 50.0f, nullptr);
    viewport.setCamera(0.5f, 0.0f, 1.0f);
    const bool changed = tap.applyQueuedTap(NativeWorldContext{world, viewport},
            input, selection, 4);

    assertTrue(changed && world.mCellData.getIdFromSelectTag(1) == cell,
            "Queued tap resolves through the viewport captured at event time");
}

void testInputWatermarksSeparateReceivedFromResolvedCommands() {
    using cellularity::microscope::InputKind;
    using cellularity::microscope::InputLane;
    cellularity::microscope::MicroscopeInputTimeline timeline;

    const std::uint64_t worldSequence =
            timeline.beginInput(InputLane::WorldOwner, InputKind::Tap, 7);
    timeline.enqueueWorldInput(worldSequence, true, 1);
    assertTrue(timeline.resolvedState().lastResolvedInputSequence == 0,
            "Queued world input does not pretend to be owner-resolved");

    const std::uint64_t panSequence =
            timeline.beginInput(InputLane::Presentation, InputKind::Pan, 7);
    timeline.resolvePresentationInput(panSequence, true, 8);
    assertTrue(panSequence == worldSequence + 1 && timeline.resolvedState().lastResolvedInputSequence == 0,
            "A newer presentation event cannot jump the contiguous resolved "
            "watermark");

    timeline.resolveWorldInputs(true);
    const auto resolved = timeline.resolvedState();
    assertTrue(resolved.lastResolvedInputSequence == panSequence && resolved.appliedWorldRevision == 1,
            "Owner safe point resolves the contiguous range and advances "
            "world revision");

#ifdef CELLULARITY_RENDER_TIMELINE
    const auto beforePresentation = timeline.snapshot();
    assertTrue(beforePresentation.samples.size() == 2 && beforePresentation.counters.received == 2 && beforePresentation.counters.accepted == 2 && beforePresentation.counters.worldQueueHighWater == 1,
            "Opt-in input timeline records bounded queue diagnostics");
    const std::int64_t ownerResolvedNanos =
            beforePresentation.samples.front().ownerResolvedNanos;
    timeline.markFramePresented(panSequence, resolved.appliedWorldRevision,
            ownerResolvedNanos + 10, ownerResolvedNanos + 20,
            8, ownerResolvedNanos + 30);
    const auto presented = timeline.snapshot();
    assertTrue(
            presented.samples[0].eventReceivedNanos > 0 && presented.samples[0].eventReceivedNanos <= presented.samples[0].enqueuedOrPublishedNanos && presented.samples[0].enqueuedOrPublishedNanos <= presented.samples[0].ownerResolvedNanos && presented.samples[0].sourceCapturedNanos == ownerResolvedNanos + 10 && presented.samples[0].framePreparedNanos == ownerResolvedNanos + 20 && presented.samples[0].firstPresentedNanos == ownerResolvedNanos + 30 && presented.samples[1].firstPresentedNanos == ownerResolvedNanos + 30,
            "Opt-in timeline preserves monotonic world and presentation lanes");
#endif

    const std::uint64_t cancelledSequence =
            timeline.beginInput(InputLane::WorldOwner, InputKind::TouchDown, 8);
    timeline.enqueueWorldInput(cancelledSequence, true, 1);
    timeline.cancelWorldInputs();
    assertTrue(timeline.resolvedState().lastResolvedInputSequence == cancelledSequence && timeline.resolvedState().appliedWorldRevision == 1,
            "Cancelled owner input resolves its sequence without changing "
            "world revision");

    const std::uint64_t firstTap =
            timeline.beginInput(InputLane::WorldOwner, InputKind::Tap, 8);
    timeline.enqueueWorldInput(firstTap, true, 1);
    const std::uint64_t replacementTap =
            timeline.beginInput(InputLane::WorldOwner, InputKind::Tap, 8);
    timeline.enqueueWorldInput(replacementTap, true, 1, true);
    timeline.resolveWorldInputs(false);
    assertTrue(
            timeline.resolvedState().lastResolvedInputSequence == replacementTap,
            "Tap replacement resolves without leaving a false pending watermark");
#ifdef CELLULARITY_RENDER_TIMELINE
    const auto coalesced = timeline.snapshot();
    assertTrue(coalesced.counters.coalesced == 1,
            "Opt-in timeline counts replaced taps as coalesced input");
#endif
}

void testInputTimelineReportsCurrentBoundedQueueBehavior() {
#ifdef CELLULARITY_RENDER_TIMELINE
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(100, 100);
    controller.resetMicroscopeInputTimelineMeasurements();

    for (int event = 0; event < 201; ++event) {
        controller.queueMicroscopeTouchMove(1, 50.0f, 50.0f);
    }
    controller.queueMicroscopeTap(50.0f, 50.0f, nullptr);
    controller.queueMicroscopeTap(50.0f, 50.0f, nullptr);

    const auto queued = controller.microscopeInputTimelineSnapshot();
    assertTrue(
            queued.counters.received == 203 && queued.counters.accepted == 202 && queued.counters.rejected == 1 && queued.counters.coalesced == 1 && queued.counters.worldQueueHighWater == 201,
            "Timeline reports fixed queue capacity, rejection, and tap coalescing");

    controller.stepMicroscopeFrame();
    const auto resolved = controller.microscopeInputTimelineSnapshot();
    assertTrue(resolved.state.lastResolvedInputSequence == 203 && resolved.counters.sampleOverwrites == 0,
            "Safe point resolves the full measured input burst without "
            "telemetry loss");
#endif
}

void testNativeCellularityControllerDiagnosisTapEmptyDeselectsCell() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(100, 100);
    controller.setMicroscopeToolOrdinal(4);

    auto worldAccess = controller.writeWorld();
    SimulationWorld& world = worldAccess.world();
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    cellData.mSelectTag[cell] = 1;
    world.updateGrids();

    controller.queueMicroscopeTap(90.0f, 90.0f, nullptr);
    controller.stepMicroscopeFrame();

    assertTrue(
            cellData.getIdFromSelectTag(1) == -1,
            "Native controller diagnosis tap on empty space deselects selected cell");
}

void testNativeCellularityControllerForwardsUpdateStages() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    NativeCellularityController controller(false, environment);

    {
        auto access = controller.readWorld();
        assertTrue(access.world().getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH,
                "Native controller initial update stage");
    }
    controller.update1(false, 1.0);
    {
        auto access = controller.readWorld();
        assertTrue(access.world().getUpdateStage() == 2,
                "Native controller forwards update1");
    }
    controller.update2(false, 1.0);
    {
        auto access = controller.readWorld();
        assertTrue(access.world().getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH,
                "Native controller forwards update2");
    }

    controller.update(false, 1.0);
    {
        auto access = controller.readWorld();
        assertTrue(access.world().getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH,
                "Native controller full update restores touch stage");
    }
}

void testNativeCellularityControllerCountsCompletedUpdates() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    NativeCellularityController controller(false, environment);

    {
        auto access = controller.readWorld();
        assertTrue(access.environmentRevision() == 0,
                "Native world session starts at environment revision zero");
    }
    {
        auto access = controller.writeWorld();
        assertTrue(access.markEnvironmentChanged() == 1,
                "Native world write access owns revision advancement");
    }
    {
        auto access = controller.readWorld();
        assertTrue(
                access.environmentRevision() == 1,
                "Native world read access observes revision under the same lock");
    }

    assertTrue(controller.completedUpdateCount() == 0,
            "Native controller initial completed update count");
    controller.update1(false, 1.0);
    assertTrue(controller.completedUpdateCount() == 0,
            "Native controller update1 is not a completed update");
    controller.update2(false, 1.0);
    assertTrue(controller.completedUpdateCount() == 1,
            "Native controller update2 completes split update");
    controller.update(false, 1.0);
    assertTrue(controller.completedUpdateCount() == 2,
            "Native controller full update increments completed update count");
    controller.resetCompletedUpdateCount();
    assertTrue(controller.completedUpdateCount() == 0,
            "Native controller reset completed update count");

    NativeCellularityController standaloneUpdate2Controller(false, environment);
    {
        auto access = standaloneUpdate2Controller.writeWorld();
        access.world().update1(false, 1.0);
    }
    standaloneUpdate2Controller.update2(false, 1.0);
    assertTrue(standaloneUpdate2Controller.completedUpdateCount() == 0,
            "Native controller standalone update2 is not counted as full "
            "controller update");
}

} // namespace

int main() {
    testViewportSnapshotOwnsCompleteImmutableValues();
    testWorldEpochAndViewportRevisionAreMonotonicIdentities();
    testGenomePreviewResetClearsStaleTopology();
    testWorldResetContentsClearsStaleTopology();
    testUserAddedCellsStartWithZeroAge();
    testWorldEditingAcceptsDynamicGenomeRecords();
    testCellTapOwnsDynamicGenomeUntilApplied();
    testAddUserCellAtCapacityKeepsTopologyValid();
    testNativeCellularityControllerFreezeRedrawDoesNotAdvanceSimulation();
    testNativeCellularityControllerMicroscopeStepCountsCompletedUpdate();
    testMicroscopeModeContractsMatchCaptureTable();
    testMicroscopeFrameControllerCapturesAtExactModeSemanticPoints();
    testQueuedTapUsesItsEventTimeViewport();
    testInputWatermarksSeparateReceivedFromResolvedCommands();
    testInputTimelineReportsCurrentBoundedQueueBehavior();
    testNativeCellularityControllerDiagnosisTapEmptyDeselectsCell();
    testNativeCellularityControllerForwardsUpdateStages();
    testNativeCellularityControllerCountsCompletedUpdates();
    std::cout << "native interaction parity tests passed\n";
    return 0;
}
