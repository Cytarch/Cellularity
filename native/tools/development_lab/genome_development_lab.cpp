#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "app/NativeWorldEditing.h"
#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "formats/release/ReleaseGenomeCodec.h"

namespace {

using cellularity::GeneBuilder;
using cellularity::GeneIndex;
using cellularity::GeneView;

constexpr int kRootGene = 0;
constexpr int kBodyLeftGene = 1;
constexpr int kBodyRightGene = 2;
constexpr int kTentacleLeftGene = 3;
constexpr int kTentacleRightGene = 4;
constexpr int kCiliaTipGene = 5;
constexpr int kDomeGene = 6;
constexpr int kTailGene = 7;
constexpr int kPhotoBeadGene = 8;

constexpr int kSwimmerRootGene = 0;
constexpr int kSwimmerFlagellocyteGene = 1;
constexpr int kSwimmerAdultBodyGene = 2;
constexpr int kSwimmerJuvenileBodyGene = 3;
constexpr int kShapeProfileBins = 9;
constexpr int kShapeBranchSlots = 8;

struct LabConfig {
    int maxCells = 96;
    int sampleSteps = 25;
    double previewTime = 8.0;
    uint64_t seed = 1337;
    std::string genomeName = "segmented_medusa_v1";
    std::string mode = "preview";
    std::string exportGenomePath;
    std::vector<double> checkpointTimes;
    int minCells = -1;
    int maxCellsAssert = -1;
    int maxCollisions = -1;
    int minEdges = -1;
    int minGeneIndex = -1;
    int minGeneCount = -1;
    double minWidth = -1.0;
    double minHeight = -1.0;
    double minDisplacement = -1.0;
    double surviveUntil = -1.0;
    bool requireConnected = false;
    bool dump = false;
    bool json = false;
    bool analysis = false;
};

struct SplitEventInfo {
    SimulationWorld::SplitEvent event;
};

struct Bounds {
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
};

struct ShapeDescriptor {
    bool valid = false;
    double centerX = 0.0;
    double centerY = 0.0;
    double axisX = 1.0;
    double axisY = 0.0;
    double perpX = 0.0;
    double perpY = 1.0;
    double majorMin = 0.0;
    double majorMax = 0.0;
    double minorMin = 0.0;
    double minorMax = 0.0;
    double majorExtent = 0.0;
    double minorExtent = 0.0;
    double elongation = 0.0;
    double pcaSymmetry = 0.0;
    int endpoints = 0;
    int junctions = 0;
    int highDegree = 0;
    int branchCenterCell = -1;
    int branchCount = 0;
    int strongRays = 0;
    int shortBranches = 0;
    int clusteredBranches = 0;
    double branchMaxLength = 0.0;
    double branchMeanLength = 0.0;
    double branchMeanStraightness = 0.0;
    double branchBalance = 1.0;
    double centralLoad = 0.0;
    std::array<int, kShapeProfileBins> binCounts{};
    std::array<double, kShapeProfileBins> binWidths{};
    std::array<int, kShapeBranchSlots> branchCells{};
    std::array<int, kShapeBranchSlots> branchDepth{};
    std::array<double, kShapeBranchSlots> branchLength{};
    std::array<double, kShapeBranchSlots> branchStraightness{};
};

struct Metrics {
    double time = 0.0;
    int cells = 0;
    int edges = 0;
    int components = 0;
    int splitEvents = 0;
    int collisions = 0;
    int dead = 0;
    int terminalGeneCells = 0;
    int lowMassCells = 0;
    int outOfBoundsCells = 0;
    int toxicCells = 0;
    int lethalOverlapPairs = 0;
    int lostSincePreviousSample = 0;
    int structuralPrototypeCells = 0;
    int functionalCells = 0;
    int devorocyteCells = 0;
    int motorCells = 0;
    int actuatorCells = 0;
    double width = 0.0;
    double height = 0.0;
    double aspect = 0.0;
    double minCenterDistance = 0.0;
    double minSurfaceGap = 0.0;
    double centerX = 0.0;
    double centerY = 0.0;
    double displacement = 0.0;
    double meanDegree = 0.0;
    double mirrorScore = 0.0;
    double shapeMajor = 0.0;
    double shapeMinor = 0.0;
    double shapeElongation = 0.0;
    double shapeSymmetry = 0.0;
    int shapeEndpoints = 0;
    int shapeJunctions = 0;
    int shapeHighDegree = 0;
    int shapeBranchCenterCell = -1;
    int shapeBranchCount = 0;
    int shapeStrongRays = 0;
    int shapeShortBranches = 0;
    int shapeClusteredBranches = 0;
    double shapeBranchMaxLength = 0.0;
    double shapeBranchMeanLength = 0.0;
    double shapeBranchMeanStraightness = 0.0;
    double shapeBranchBalance = 1.0;
    double shapeCentralLoad = 0.0;
    int shapeCycleRank = 0;
    double score = 0.0;
    std::array<int, CellTypes::length> typeCounts{};
    std::array<int, CellData::MAX_GENES_COUNT> geneCounts{};
    std::array<int, kShapeProfileBins> shapeBinCounts{};
    std::array<double, kShapeProfileBins> shapeBinWidths{};
    std::array<int, kShapeBranchSlots> shapeBranchCells{};
    std::array<int, kShapeBranchSlots> shapeBranchDepth{};
    std::array<double, kShapeBranchSlots> shapeBranchLength{};
    std::array<double, kShapeBranchSlots> shapeBranchStraightness{};
};

GeneBuilder editGene(Gene& gene) {
    return GeneBuilder(gene);
}

void setColor(Gene& gene, float r, float g, float b) {
    GeneBuilder builder(gene);
    builder.setColor(0, r);
    builder.setColor(1, g);
    builder.setColor(2, b);
}

Gene baseGene(CellType type, float r, float g, float b) {
    Gene gene;
    setColor(gene, r, g, b);
    editGene(gene).setCellType(type);
    editGene(gene).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(gene).setSplitRatio(0.5f);
    editGene(gene).setSplitAngle(0.0f);
    editGene(gene).setChild1Angle(0.0f);
    editGene(gene).setChild2Angle(0.0f);
    editGene(gene).setMassFlowPriority(1.0f);
    editGene(gene).setChild1GeneIndex(GeneIndex(0));
    editGene(gene).setChild2GeneIndex(GeneIndex(0));
    editGene(gene).setMakesLink(false);
    editGene(gene).setChild1KeepsLinks(true);
    editGene(gene).setChild2KeepsLinks(true);
    editGene(gene).setStaysAlive(true);
    editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.35f);
    editGene(gene).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, CellData::MAX_NEIGHBORS - 1);
    editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.005f);
    return gene;
}

Gene builderGene(CellType type, float r, float g, float b) {
    Gene gene = baseGene(type, r, g, b);
    editGene(gene).setSplitMass(CellData::MIN_SPLIT_MASS);
    editGene(gene).setSplitRatio(0.55f);
    editGene(gene).setMassFlowPriority(7.0f);
    editGene(gene).setMakesLink(true);
    editGene(gene).setChild1KeepsLinks(true);
    editGene(gene).setChild2KeepsLinks(true);
    editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.60f);
    editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.15f);
    editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.008f);
    return gene;
}

Gene terminalGene(CellType type, float r, float g, float b) {
    Gene gene = baseGene(type, r, g, b);
    editGene(gene).setSplitMass(CellData::MAX_SPLIT_MASS);
    return gene;
}

void setConstantParametricValue(Gene& gene, int valueIndex, float value) {
    editGene(gene).setParametricFormulaType(valueIndex, 0);
    editGene(gene).setParametricSignalChannel(valueIndex, 0);
    editGene(gene).setParametricVariableA(valueIndex, 0.0f);
    editGene(gene).setParametricVariableB(valueIndex, value);
    editGene(gene).setParametricVariableC(valueIndex, 0.0f);
}

Gene tentacleGene(CellType type, float r, float g, float b, int selfGene, float outwardAngle) {
    Gene gene = builderGene(type, r, g, b);
    editGene(gene).setSplitRatio(0.62f);
    editGene(gene).setChild1GeneIndex(GeneIndex(selfGene));
    editGene(gene).setChild2GeneIndex(GeneIndex(kPhotoBeadGene));
    editGene(gene).setChild1Angle(outwardAngle * 0.28f);
    editGene(gene).setChild2Angle(outwardAngle);
    editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.45f);
    editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 0.85f);
    editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0065f);
    return gene;
}

bool isStructuralPrototypeType(CellType type) {
    return type == CellType::PHAGOCYTE || type == CellType::PHOTOCYTE;
}

bool isFunctionalLayerType(CellType type) {
    return type == CellType::FLAGELLOCYTE
            || type == CellType::MYOCYTE
            || type == CellType::CILIOCYTE
            || type == CellType::STEREOCYTE
            || type == CellType::SENSEOCYTE
            || type == CellType::BUOYOCYTE
            || type == CellType::GLUEOCYTE
            || type == CellType::NITROCYTE
            || type == CellType::NEUROCYTE
            || type == CellType::SECROCYTE
            || type == CellType::STEMOCYTE
            || type == CellType::GAMETE;
}

bool isMotorType(CellType type) {
    return type == CellType::FLAGELLOCYTE || type == CellType::CILIOCYTE;
}

bool isActuatorType(CellType type) {
    return type == CellType::MYOCYTE || isMotorType(type);
}

// Organism design notes from the editor-made reference genomes:
//
// 1. Start from a tiny verified motif, not from a large planned organism.
//    ordinary_swimmer works because it is just m1 -> (motor, body), then
//    body -> (body, new m1). The reproduction loop is visible in one line of
//    gene graph, so failures are easy to locate in split_events.
//
// 2. Preview and swim tests answer different questions.
//    Preview is valid for split order, links, components, collisions, and final
//    geometry. It is not valid for flagellocyte motion because preview skips the
//    active swimming forces. Any swimmer must also pass a non-preview displacement
//    check.
//
// 3. The first useful shape should be a closed or mechanically meaningful module.
//    rsh-0 builds a four-cell square ring at 1.2s: stereocyte, two flagellocytes,
//    and a phagocyte body. The ring is much easier to reason about than a line:
//    links constrain both axes, motor forces have a frame, and geometry can be
//    checked by width/height/components instead of visual guessing.
//
// 4. Use split tree roles deliberately.
//    Founder genes should build; terminal genes should specialize. A body that
//    also emits a fresh founder is a clean reproduction mechanism. A lipocyte
//    founder/reserve can stage a larger organism before normal feeding matters.
//
// 5. Preserve old simple parameters unless a measurement forces a change.
//    The old files use zero rest length and zero link stiffness, yet produce stable
//    paired/ring modules in this runtime. Changing those values early can create
//    artificial link tearing or overlap bugs that are not part of the design.
//
// 6. Every edit should be checked geometrically.
//    Useful checkpoints: early first pair/module, first reproduction event, final
//    component count, edge count, width/height, lethal overlaps, and non-preview
//    center displacement for swimmers.
//
// 7. Treat failed hypotheses as design data, not just bad genomes.
//    In kite_seed_swimmer_v1 the first plan was "rsh-like square + reproductive
//    corner". That was too vague. The useful conclusions came from specific
//    failed checks:
//      - a preview-stable module can still be invalid as a swimmer until tested
//        in non-preview mode;
//      - child link inheritance is easiest to learn from split_events and edge
//        dumps, not from intuition about parent/child naming;
//      - free lipocyte seeds are a trap: without inherited lipids they can look
//        full-sized while their scaled split mass is too low to divide;
//      - turning every builder into a phagocyte can preserve the shape but change
//        the life-cycle pressure, so check the next split event after each type
//        change, not only the final geometry.
//      - a non-ring topology is not enough by itself: outrigger_swimmer_v1 forms
//        the intended V/star module in preview, but in swim mode the first motor
//        can move the two-cell stage away before the second builder split. For
//        multi-step swimmers, motor activation must be delayed, weakened, or
//        staged behind a non-motor builder phase.
//      - delayed motor activation is not the same thing as a serious organism:
//        larval_glider_v1 is still only a three-cell swimmer with a side ballast.
//        Complexity must be measured by durable differentiated modules,
//        feeding/reproduction, and mature structure size.
//    The practical workflow is: change one causal assumption, run early/final
//    preview checkpoints, run one non-preview displacement check, then name the
//    observed phenotype honestly. Kite ended as an rsh-derived rotating module,
//    not as a genuinely new organism: changing cell types while keeping the same
//    ring topology and tangential motor placement mostly preserves the old design.
//    A new design must change the force geometry, not just the Cellularityels.
//
// 8. worm.genome is a different class of reference: it is slow and recursive.
//    The first 8-10 preview seconds only show a small linked larval module, so
//    judging it there gives the wrong conclusion. The useful phenotype appears
//    after the photocyte/repeater bottleneck: m9 and m15 both emit fresh m1
//    founders, and by 120 preview seconds the organism is a single elongated
//    chain-like component with repeated myocyte, flagellocyte, stereocyte, and
//    photocyte modules. For serious organisms, add long checkpoints before
//    calling a genome "small"; early geometry and adult geometry can be very
//    different.
//
// 9. General pattern extracted from the three good user genomes:
//     Think in motifs and life-cycle roles, not in Cellularityels.
//       ordinary swimmer: seed -> functional pair, body -> next seed.
//       rsh-0: lipid seed -> two builders -> compact mechanical module.
//       worm: seed -> segment builder -> terminal workers + next seed.
//     A useful gene graph usually has three role classes:
//       founders: short-lived entry points that place a motif or segment;
//       builders: intermediate genes that unfold geometry and link topology;
//       workers: mostly terminal cells that move, feed, photosynthesize, anchor,
//                sense, or bend the body.
//     New organisms should be designed by changing the motif grammar and force
//     geometry, not by renaming cells inside an existing motif.
//
// 11. Reproduction is a structural contract, not just "some gene emits m1".
//     The emitted founder must inherit enough mass/energy context, be placed far
//     enough from the parent, and have the correct link inheritance. Ordinary
//     swimmer emits free two-cell offspring; worm emits new founders inside an
//     existing connected body. Those are different reproductive semantics and
//     should be checked with components/edges over time.
//
// 12. Motors must have a frame.
//     A flagellocyte attached to a single side of a small body mostly creates
//     rotation. rsh-0 works better because two motors push against a compact
//     linked module. worm distributes motors and myocytes along a body axis.
//     Before inventing a new swimmer, define the body frame first: where force
//     enters, what resists torque, and whether repeated segments align or curl.
//
// 13. Adult-time matters.
//     Some organisms are fast constructors (rsh-0), some are population emitters
//     (ordinary swimmer), and some have slow bottlenecks (worm photocytes). A
//     single final time is not a real evaluation. Use at least: first split,
//     first complete motif, first reproduction event, expected adult time, and
//     a later overgrowth/stability checkpoint.
//
// 14. Design shape before function.
//     Human genome editing usually starts from the visible architecture: topology,
//     link graph, angles, proportions, repeated modules, and adult silhouette.
//     That structural prototype is often built from phagocytes and photocytes
//     because they can bring energy in the editor/preview and do not destroy the
//     shape. Do not use devorocytes in the structural pass: they are functional
//     destructive cells and can invalidate the geometry while it is still being
//     designed. Only after the form is stable should structural cells be replaced
//     by motors, myocytes, sensors, armor, buoyancy, secretion, and other roles.
//     The lab analysis therefore reports structural-prototype cells separately
//     from the functional layer.

std::array<GeneData, CellData::MAX_GENES_COUNT> createTwoCellSwimmerGenome() {
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.32f, 0.34f, 0.36f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
    }

    // Design note 1:
    // The editor starts from one initial m1 cell. This first gene is only a founder:
    // after the first split it should leave a compact two-cell organism, not remain
    // as a permanent cell type. Child1 becomes the adult body, child2 becomes the
    // flagellocyte, and the split creates exactly one link between them.
    Gene founder = builderGene(CellType::PHAGOCYTE, 0.25f, 0.72f, 0.95f);
    editGene(founder).setInitialCompatibilityFlag(true);
    editGene(founder).setSplitMass(CellData::MIN_SPLIT_MASS);
    // Iteration note:
    // The first swim run killed the pair after a lethal overlap, and the second
    // run starved tiny full-force motors. Keep the motor separated, but give it
    // enough mass and only moderate thrust. In normal simulation the flagellocyte
    // does not eat, so it must start heavier than a disposable preview bud.
    editGene(founder).setSplitRatio(0.45f);
    editGene(founder).setSplitAngle(0.0f);
    editGene(founder).setChild1Angle(0.0f);
    editGene(founder).setChild2Angle(static_cast<float>(M_PI));
    editGene(founder).setChild1GeneIndex(GeneIndex(kSwimmerAdultBodyGene));
    editGene(founder).setChild2GeneIndex(GeneIndex(kSwimmerFlagellocyteGene));
    editGene(founder).setMakesLink(true);
    editGene(founder).setChild1KeepsLinks(true);
    editGene(founder).setChild2KeepsLinks(true);
    editGene(founder).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.28f);
    editGene(founder).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0065f);
    genome[kSwimmerRootGene] = founder;

    // Design note 2:
    // The flagellocyte is terminal. It should not reproduce by itself; it spends
    // mass on swimming and stays as the motor attached to one phagocyte body.
    Gene flagellocyte = terminalGene(CellType::FLAGELLOCYTE, 0.95f, 0.88f, 0.22f);
    editGene(flagellocyte).setSplitMass(CellData::MAX_SPLIT_MASS);
    // Iteration note:
    // Mass flow priority is demand, not generosity. The motor needs high demand
    // so the eating body keeps it alive through the link while it spends mass on
    // swimming.
    editGene(flagellocyte).setMassFlowPriority(9.0f);
    editGene(flagellocyte).setFloatValue(Gene::FloatType::CYTOSKELETON, 0.7f);
    editGene(flagellocyte).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.004f);
    // Iteration note:
    // 0.45 thrust moved the pair almost across the whole substrate by 2 seconds.
    // 0.15 still starved the motor by roughly 3 seconds. Use low sustained thrust;
    // the lab checks displacement geometrically instead of relying on visual motion.
    setConstantParametricValue(flagellocyte, Gene::SWIM_FORCE_VALUE_INDEX, 0.07f);
    genome[kSwimmerFlagellocyteGene] = flagellocyte;

    // Design note 3:
    // The adult body is the reusable reproductive half of the organism. In preview
    // it should eventually bud off a juvenile body while keeping its old flagellocyte.
    // The juvenile is not linked to the adult; it must make its own flagellocyte.
    Gene adultBody = builderGene(CellType::PHAGOCYTE, 0.20f, 0.66f, 0.95f);
    editGene(adultBody).setSplitMass(CellData::MAX_SPLIT_MASS * 0.92f);
    editGene(adultBody).setSplitRatio(0.58f);
    editGene(adultBody).setSplitAngle(static_cast<float>(M_PI * 0.5));
    editGene(adultBody).setChild1Angle(0.0f);
    editGene(adultBody).setChild2Angle(0.0f);
    editGene(adultBody).setChild1GeneIndex(GeneIndex(kSwimmerAdultBodyGene));
    editGene(adultBody).setChild2GeneIndex(GeneIndex(kSwimmerJuvenileBodyGene));
    editGene(adultBody).setMakesLink(false);
    editGene(adultBody).setChild1KeepsLinks(true);
    editGene(adultBody).setChild2KeepsLinks(false);
    editGene(adultBody).setMassFlowPriority(4.0f);
    editGene(adultBody).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kSwimmerAdultBodyGene] = adultBody;

    // Design note 4:
    // The juvenile body repeats the founder program once it grows enough. This is
    // the minimal reproduction loop: adult body buds juvenile, juvenile turns into
    // a new adult+flagellocyte pair.
    Gene juvenileBody = builderGene(CellType::PHAGOCYTE, 0.48f, 0.86f, 1.0f);
    editGene(juvenileBody).setSplitMass(CellData::MIN_SPLIT_MASS);
    editGene(juvenileBody).setSplitRatio(0.45f);
    editGene(juvenileBody).setSplitAngle(0.0f);
    editGene(juvenileBody).setChild1Angle(0.0f);
    editGene(juvenileBody).setChild2Angle(static_cast<float>(M_PI));
    editGene(juvenileBody).setChild1GeneIndex(GeneIndex(kSwimmerAdultBodyGene));
    editGene(juvenileBody).setChild2GeneIndex(GeneIndex(kSwimmerFlagellocyteGene));
    editGene(juvenileBody).setMakesLink(true);
    editGene(juvenileBody).setChild1KeepsLinks(true);
    editGene(juvenileBody).setChild2KeepsLinks(true);
    editGene(juvenileBody).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.28f);
    editGene(juvenileBody).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0065f);
    genome[kSwimmerJuvenileBodyGene] = juvenileBody;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createOrdinarySwimmerReferenceGenome() {
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.32f, 0.34f, 0.36f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    // Reference note:
    // This is the user's editor-made ordinary swimmer genome reduced to code.
    // It is intentionally much smaller than the experimental four-gene swimmer:
    // m1 makes the first linked motor/body pair, m3 later emits a fresh m1 seed,
    // and that seed repeats the same two-cell construction.
    Gene root = terminalGene(CellType::PHAGOCYTE, 0.25f, 0.72f, 0.95f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.253800f);
    editGene(root).setSplitRatio(0.5f);
    editGene(root).setSplitAngle(0.0f);
    editGene(root).setChild1Angle(0.0f);
    editGene(root).setChild2Angle(0.0f);
    editGene(root).setMassFlowPriority(1.0f);
    editGene(root).setChild1GeneIndex(GeneIndex(1));
    editGene(root).setChild2GeneIndex(GeneIndex(2));
    editGene(root).setMakesLink(true);
    editGene(root).setChild1KeepsLinks(true);
    editGene(root).setChild2KeepsLinks(true);
    editGene(root).setLinkStrength(0.0f);
    editGene(root).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(root).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[0] = root;

    // In the old genome file the raw swim variable is 0.0. Through getValue()
    // that maps to the middle of the swim-force range, so the real thrust is 0.5.
    Gene motor = terminalGene(CellType::FLAGELLOCYTE, 0.95f, 0.88f, 0.22f);
    editGene(motor).setSplitMass(0.367200f);
    editGene(motor).setSplitRatio(0.5f);
    editGene(motor).setMassFlowPriority(1.0f);
    editGene(motor).setChild1GeneIndex(GeneIndex(1));
    editGene(motor).setChild2GeneIndex(GeneIndex(1));
    editGene(motor).setLinkStrength(0.0f);
    editGene(motor).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(motor).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    setConstantParametricValue(motor, Gene::SWIM_FORCE_VALUE_INDEX, 0.0f);
    genome[1] = motor;

    // Reproduction note:
    // m3 is the body. It keeps itself as child1 and emits a new m1 as child2.
    // The emitted m1 then creates the next linked two-cell swimmer.
    Gene body = terminalGene(CellType::PHAGOCYTE, 0.20f, 0.66f, 0.95f);
    editGene(body).setSplitMass(0.252000f);
    editGene(body).setSplitRatio(0.5f);
    editGene(body).setSplitAngle(0.0f);
    editGene(body).setChild1Angle(0.0f);
    editGene(body).setChild2Angle(0.0f);
    editGene(body).setMassFlowPriority(1.0f);
    editGene(body).setChild1GeneIndex(GeneIndex(2));
    editGene(body).setChild2GeneIndex(GeneIndex(0));
    editGene(body).setMakesLink(false);
    editGene(body).setChild1KeepsLinks(true);
    editGene(body).setChild2KeepsLinks(true);
    editGene(body).setLinkStrength(0.0f);
    editGene(body).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(body).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[2] = body;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createRsh0ReferenceGenome() {
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.36f, 0.38f, 0.40f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(0.252000f);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    // Reference note:
    // rsh-0.genome is a compact old-format (v96) organism. It starts as a lipid
    // reserve, splits into two lipid builder branches, then produces a stereocyte,
    // two flagellocyte motors, and one phagocyte body. There is no development
    // state usage here; the complexity is purely in the split tree.
    Gene root = terminalGene(CellType::LIPOCYTE, 0.94f, 0.60f, 0.22f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.153000f);
    editGene(root).setSplitRatio(0.5f);
    editGene(root).setSplitAngle(0.0f);
    editGene(root).setChild1Angle(0.0f);
    editGene(root).setChild2Angle(0.0f);
    editGene(root).setMassFlowPriority(1.0f);
    editGene(root).setChild1GeneIndex(GeneIndex(1));
    editGene(root).setChild2GeneIndex(GeneIndex(2));
    editGene(root).setMakesLink(true);
    editGene(root).setChild1KeepsLinks(false);
    editGene(root).setChild2KeepsLinks(true);
    editGene(root).setLinkStrength(0.0f);
    editGene(root).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(root).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[0] = root;

    Gene leftReserve = terminalGene(CellType::LIPOCYTE, 0.92f, 0.52f, 0.20f);
    editGene(leftReserve).setSplitMass(0.077400f);
    editGene(leftReserve).setSplitRatio(0.5f);
    editGene(leftReserve).setSplitAngle(static_cast<float>(M_PI * 0.5));
    editGene(leftReserve).setChild1Angle(5.4978f);
    editGene(leftReserve).setChild2Angle(5.4978f);
    editGene(leftReserve).setMassFlowPriority(1.0f);
    editGene(leftReserve).setChild1GeneIndex(GeneIndex(3));
    editGene(leftReserve).setChild2GeneIndex(GeneIndex(4));
    editGene(leftReserve).setMakesLink(true);
    editGene(leftReserve).setChild1KeepsLinks(true);
    editGene(leftReserve).setChild2KeepsLinks(true);
    editGene(leftReserve).setLinkStrength(0.0f);
    editGene(leftReserve).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(leftReserve).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[1] = leftReserve;

    Gene rightReserve(leftReserve);
    editGene(rightReserve).setSplitMass(0.064800f);
    editGene(rightReserve).setChild1GeneIndex(GeneIndex(5));
    editGene(rightReserve).setChild2GeneIndex(GeneIndex(6));
    genome[2] = rightReserve;

    Gene anchor = terminalGene(CellType::STEREOCYTE, 0.75f, 0.75f, 0.82f);
    editGene(anchor).setSplitMass(0.252000f);
    editGene(anchor).setSplitRatio(0.5f);
    editGene(anchor).setMassFlowPriority(1.0f);
    editGene(anchor).setChild1GeneIndex(GeneIndex(0));
    editGene(anchor).setChild2GeneIndex(GeneIndex(3));
    editGene(anchor).setMakesLink(true);
    editGene(anchor).setChild1KeepsLinks(true);
    editGene(anchor).setChild2KeepsLinks(true);
    editGene(anchor).setLinkStrength(0.0f);
    editGene(anchor).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(anchor).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[3] = anchor;

    Gene leftMotor = terminalGene(CellType::FLAGELLOCYTE, 0.95f, 0.88f, 0.22f);
    editGene(leftMotor).setSplitMass(0.367200f);
    editGene(leftMotor).setSplitRatio(0.5f);
    editGene(leftMotor).setMassFlowPriority(1.0f);
    editGene(leftMotor).setChild1GeneIndex(GeneIndex(4));
    editGene(leftMotor).setChild2GeneIndex(GeneIndex(4));
    editGene(leftMotor).setLinkStrength(0.0f);
    editGene(leftMotor).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(leftMotor).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    setConstantParametricValue(leftMotor, Gene::SWIM_FORCE_VALUE_INDEX, 0.0f);
    genome[4] = leftMotor;

    Gene rightMotor(leftMotor);
    editGene(rightMotor).setChild1GeneIndex(GeneIndex(5));
    editGene(rightMotor).setChild2GeneIndex(GeneIndex(5));
    genome[5] = rightMotor;

    Gene body = terminalGene(CellType::PHAGOCYTE, 0.28f, 0.70f, 0.95f);
    editGene(body).setSplitMass(0.367200f);
    editGene(body).setSplitRatio(0.5f);
    editGene(body).setMassFlowPriority(1.0f);
    editGene(body).setChild1GeneIndex(GeneIndex(6));
    editGene(body).setChild2GeneIndex(GeneIndex(6));
    editGene(body).setLinkStrength(0.0f);
    editGene(body).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(body).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[6] = body;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createWormReferenceGenome() {
    constexpr int kRoot = 0;
    constexpr int kStemLeft = 1;
    constexpr int kReserve = 2;
    constexpr int kLightBuilder = 3;
    constexpr int kStemRight = 4;
    constexpr int kStereocyteA = 5;
    constexpr int kSegmentReserve = 6;
    constexpr int kMyocyte = 7;
    constexpr int kRepeater = 8;
    constexpr int kTailReserve = 9;
    constexpr int kLightRepeater = 10;
    constexpr int kLeftFlagellocyte = 11;
    constexpr int kRightFlagellocyte = 12;
    constexpr int kPhagocyte = 13;
    constexpr int kLightSeed = 14;
    constexpr int kStereocyteB = 15;

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    auto makeOldTerminal = [](CellType type, float r, float g, float b) {
        Gene gene = terminalGene(type, r, g, b);
        editGene(gene).setSplitMass(0.253800f);
        editGene(gene).setSplitRatio(0.5f);
        editGene(gene).setMassFlowPriority(1.0f);
        editGene(gene).setChild1KeepsLinks(true);
        editGene(gene).setChild2KeepsLinks(true);
        editGene(gene).setLinkStrength(0.0f);
        editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
        editGene(gene).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, CellData::MAX_NEIGHBORS - 1);
        setConstantParametricValue(gene, Gene::SWIM_FORCE_VALUE_INDEX, -0.1667f);
        return gene;
    };

    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = makeOldTerminal(CellType::PHAGOCYTE, 0.32f, 0.34f, 0.36f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
    }

    // Reference note:
    // worm.genome is the first genuinely useful "large organism" reference in
    // this lab. It is old format v95, but it is far
    // more serious than the small swimmers:
    //   - recursive return edges to m1 extend the body;
    //   - stemocytes switch into left/right flagellocytes using threshold genes;
    //   - myocytes use parametric bending/lifting from signal channel 0;
    //   - repeated lipocyte/photocyte/phagocyte pieces build a long linked chain.
    // The design lesson is that size comes from recursive segment production plus
    // differentiated local roles, not from adding a ballast cell to a swimmer.
    // It is also slow: in preview the adult-like long chain starts emerging only
    // around 80-120 seconds, after the first m11/m15 photocyte bottleneck has had
    // enough time to grow and emit more m1 founders.

    auto setLinkBuilderDefaults = [](Gene& gene) {
        editGene(gene).setMakesLink(true);
        editGene(gene).setChild1KeepsLinks(true);
        editGene(gene).setChild2KeepsLinks(true);
        editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS);
        editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    };

    Gene root = makeOldTerminal(CellType::LIPOCYTE, 0.94f, 0.58f, 0.22f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.140400f);
    editGene(root).setSplitRatio(0.5f);
    editGene(root).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(root).setChild2Angle(0.0f);
    editGene(root).setMassFlowPriority(3.831f);
    editGene(root).setChild1GeneIndex(GeneIndex(kStemLeft));
    editGene(root).setChild2GeneIndex(GeneIndex(kReserve));
    setLinkBuilderDefaults(root);
    genome[kRoot] = root;

    Gene stemLeft = makeOldTerminal(CellType::STEMOCYTE, 0.65f, 0.92f, 1.0f);
    editGene(stemLeft).setSplitMass(0.367200f);
    editGene(stemLeft).setChild1GeneIndex(GeneIndex(kStemLeft));
    editGene(stemLeft).setChild2GeneIndex(GeneIndex(kStemLeft));
    editGene(stemLeft).setIntegerValue(Gene::VIRUS_FROM_INTEGER_INDEX, kLeftFlagellocyte);
    editGene(stemLeft).setIntegerValue(Gene::VIRUS_TO_INTEGER_INDEX, kStemLeft);
    setConstantParametricValue(stemLeft, Gene::STEM_THRESHOLD_1_VALUE_INDEX, 1.0f);
    editGene(stemLeft).setParametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX, 1);
    editGene(stemLeft).setParametricVariableA(Gene::SWIM_FORCE_VALUE_INDEX, -2.2974f);
    editGene(stemLeft).setParametricVariableB(Gene::SWIM_FORCE_VALUE_INDEX, -1.0f);
    genome[kStemLeft] = stemLeft;

    Gene reserve = makeOldTerminal(CellType::LIPOCYTE, 0.92f, 0.56f, 0.20f);
    editGene(reserve).setSplitMass(0.064800f);
    editGene(reserve).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(reserve).setChild2Angle(static_cast<float>(M_PI * 0.5));
    editGene(reserve).setChild1GeneIndex(GeneIndex(kLightBuilder));
    editGene(reserve).setChild2GeneIndex(GeneIndex(kStemRight));
    setLinkBuilderDefaults(reserve);
    genome[kReserve] = reserve;

    Gene lightBuilder = makeOldTerminal(CellType::PHOTOCYTE, 0.42f, 0.95f, 0.66f);
    editGene(lightBuilder).setSplitMass(0.354600f);
    editGene(lightBuilder).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(lightBuilder).setChild2Angle(0.0f);
    editGene(lightBuilder).setChild1GeneIndex(GeneIndex(kTailReserve));
    editGene(lightBuilder).setChild2GeneIndex(GeneIndex(kSegmentReserve));
    setLinkBuilderDefaults(lightBuilder);
    genome[kLightBuilder] = lightBuilder;

    Gene stemRight(stemLeft);
    editGene(stemRight).setChild1GeneIndex(GeneIndex(kStemRight));
    editGene(stemRight).setChild2GeneIndex(GeneIndex(kStemRight));
    editGene(stemRight).setIntegerValue(Gene::VIRUS_FROM_INTEGER_INDEX, kRightFlagellocyte);
    editGene(stemRight).setIntegerValue(Gene::VIRUS_TO_INTEGER_INDEX, kStemRight);
    editGene(stemRight).setParametricVariableA(Gene::SWIM_FORCE_VALUE_INDEX, 2.2974f);
    genome[kStemRight] = stemRight;

    Gene stereocyteA = makeOldTerminal(CellType::STEREOCYTE, 0.72f, 0.72f, 0.82f);
    editGene(stereocyteA).setSplitMass(0.367200f);
    editGene(stereocyteA).setChild1GeneIndex(GeneIndex(kStereocyteA));
    editGene(stereocyteA).setChild2GeneIndex(GeneIndex(kStereocyteA));
    editGene(stereocyteA).setIntegerValue(Gene::SMELL_TYPE_INTEGER_INDEX, 1);
    genome[kStereocyteA] = stereocyteA;

    Gene segmentReserve = makeOldTerminal(CellType::LIPOCYTE, 0.90f, 0.52f, 0.22f);
    editGene(segmentReserve).setSplitMass(0.064800f);
    editGene(segmentReserve).setChild1GeneIndex(GeneIndex(kMyocyte));
    editGene(segmentReserve).setChild2GeneIndex(GeneIndex(kRepeater));
    setLinkBuilderDefaults(segmentReserve);
    genome[kSegmentReserve] = segmentReserve;

    Gene myocyte = makeOldTerminal(CellType::MYOCYTE, 0.86f, 0.42f, 0.94f);
    editGene(myocyte).setSplitMass(0.367200f);
    editGene(myocyte).setChild1GeneIndex(GeneIndex(kMyocyte));
    editGene(myocyte).setChild2GeneIndex(GeneIndex(kMyocyte));
    editGene(myocyte).setParametricFormulaType(Gene::MYOCYTE_BENDING_VALUE_INDEX, 1);
    editGene(myocyte).setParametricVariableA(Gene::MYOCYTE_BENDING_VALUE_INDEX, 2.2974f);
    editGene(myocyte).setParametricVariableB(Gene::MYOCYTE_BENDING_VALUE_INDEX, 0.0f);
    editGene(myocyte).setParametricFormulaType(Gene::MYOCYTE_LIFTING_VALUE_INDEX, 1);
    editGene(myocyte).setParametricVariableA(Gene::MYOCYTE_LIFTING_VALUE_INDEX, 8.0f);
    editGene(myocyte).setParametricVariableB(Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0.0f);
    genome[kMyocyte] = myocyte;

    Gene repeater = makeOldTerminal(CellType::PHAGOCYTE, 0.24f, 0.70f, 0.96f);
    editGene(repeater).setSplitMass(0.064800f);
    editGene(repeater).setChild1Angle(0.0f);
    editGene(repeater).setChild2Angle(static_cast<float>(M_PI * 1.5));
    editGene(repeater).setChild1GeneIndex(GeneIndex(kLightRepeater));
    editGene(repeater).setChild2GeneIndex(GeneIndex(kRoot));
    editGene(repeater).setChild1KeepsLinks(true);
    editGene(repeater).setChild2KeepsLinks(false);
    editGene(repeater).setMakesLink(true);
    editGene(repeater).setLinkStrength(CellData::MAX_LINK_STIFFNESS);
    editGene(repeater).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    genome[kRepeater] = repeater;

    Gene tailReserve = makeOldTerminal(CellType::LIPOCYTE, 0.92f, 0.60f, 0.24f);
    editGene(tailReserve).setSplitMass(0.064800f);
    editGene(tailReserve).setChild1Angle(4.4506f);
    editGene(tailReserve).setChild2Angle(4.9742f);
    editGene(tailReserve).setChild1GeneIndex(GeneIndex(kStereocyteA));
    editGene(tailReserve).setChild2GeneIndex(GeneIndex(kStereocyteB));
    editGene(tailReserve).setMakesLink(true);
    editGene(tailReserve).setChild1KeepsLinks(true);
    editGene(tailReserve).setChild2KeepsLinks(true);
    editGene(tailReserve).setLinkStrength(0.0f);
    editGene(tailReserve).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    genome[kTailReserve] = tailReserve;

    Gene lightRepeater = makeOldTerminal(CellType::PHOTOCYTE, 0.44f, 0.96f, 0.68f);
    editGene(lightRepeater).setSplitMass(0.354600f);
    editGene(lightRepeater).setChild1GeneIndex(GeneIndex(kPhagocyte));
    editGene(lightRepeater).setChild2GeneIndex(GeneIndex(kLightSeed));
    setLinkBuilderDefaults(lightRepeater);
    genome[kLightRepeater] = lightRepeater;

    Gene leftFlagellocyte = makeOldTerminal(CellType::FLAGELLOCYTE, 0.96f, 0.88f, 0.22f);
    editGene(leftFlagellocyte).setSplitMass(0.367200f);
    editGene(leftFlagellocyte).setChild1GeneIndex(GeneIndex(kLeftFlagellocyte));
    editGene(leftFlagellocyte).setChild2GeneIndex(GeneIndex(kLeftFlagellocyte));
    editGene(leftFlagellocyte).setParametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX, 1);
    editGene(leftFlagellocyte).setParametricVariableA(Gene::SWIM_FORCE_VALUE_INDEX, -2.2974f);
    editGene(leftFlagellocyte).setParametricVariableB(Gene::SWIM_FORCE_VALUE_INDEX, -1.0f);
    genome[kLeftFlagellocyte] = leftFlagellocyte;

    Gene rightFlagellocyte(leftFlagellocyte);
    editGene(rightFlagellocyte).setChild1GeneIndex(GeneIndex(kRightFlagellocyte));
    editGene(rightFlagellocyte).setChild2GeneIndex(GeneIndex(kRightFlagellocyte));
    editGene(rightFlagellocyte).setParametricVariableA(Gene::SWIM_FORCE_VALUE_INDEX, 2.2974f);
    genome[kRightFlagellocyte] = rightFlagellocyte;

    Gene phagocyte = makeOldTerminal(CellType::PHAGOCYTE, 0.20f, 0.68f, 0.95f);
    editGene(phagocyte).setSplitMass(0.367200f);
    editGene(phagocyte).setChild1GeneIndex(GeneIndex(kPhagocyte));
    editGene(phagocyte).setChild2GeneIndex(GeneIndex(kPhagocyte));
    genome[kPhagocyte] = phagocyte;

    Gene lightSeed = makeOldTerminal(CellType::PHOTOCYTE, 0.48f, 0.95f, 0.72f);
    editGene(lightSeed).setSplitMass(0.354600f);
    editGene(lightSeed).setChild1Angle(0.0f);
    editGene(lightSeed).setChild2Angle(static_cast<float>(M_PI * 1.5));
    editGene(lightSeed).setChild1GeneIndex(GeneIndex(kLightSeed));
    editGene(lightSeed).setChild2GeneIndex(GeneIndex(kRoot));
    editGene(lightSeed).setChild1KeepsLinks(true);
    editGene(lightSeed).setChild2KeepsLinks(false);
    editGene(lightSeed).setMakesLink(true);
    editGene(lightSeed).setLinkStrength(0.0f);
    editGene(lightSeed).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(lightSeed).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kLightSeed] = lightSeed;

    Gene stereocyteB = makeOldTerminal(CellType::STEREOCYTE, 0.70f, 0.72f, 0.80f);
    editGene(stereocyteB).setSplitMass(0.367200f);
    editGene(stereocyteB).setChild1GeneIndex(GeneIndex(kStereocyteB));
    editGene(stereocyteB).setChild2GeneIndex(GeneIndex(kStereocyteB));
    genome[kStereocyteB] = stereocyteB;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createKiteSeedSwimmerGenome() {
    constexpr int kSeed = 0;
    constexpr int kLeftBuilder = 1;
    constexpr int kRightBuilder = 2;
    constexpr int kAnchor = 3;
    constexpr int kLeftMotor = 4;
    constexpr int kBody = 5;
    constexpr int kBudBuilder = 6;
    constexpr int kRearMotor = 7;
    constexpr int kFreeSeed = 8;

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.30f, 0.32f, 0.34f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(0.252000f);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    // New organism design:
    // Build a kite-shaped module instead of a line or the symmetric rsh-0 square.
    // m1 is a reusable seed. It first creates two linked lipid builders. Those
    // builders unfold into a four-cell mechanically closed module:
    //   anchor + left motor + body + bud-builder.
    // The bud-builder then adds a rear motor and emits a fresh phagocyte seed.
    // The first seed is a lipocyte reserve, but repeated seeds are phagocytes:
    // a free lipocyte without inherited lipid reserve looks massive but will not
    // split again because lipocyte split uses scaled mass.
    Gene seed = terminalGene(CellType::LIPOCYTE, 0.95f, 0.58f, 0.22f);
    editGene(seed).setInitialCompatibilityFlag(true);
    editGene(seed).setSplitMass(0.153000f);
    editGene(seed).setSplitRatio(0.5f);
    editGene(seed).setSplitAngle(0.0f);
    editGene(seed).setChild1Angle(0.0f);
    editGene(seed).setChild2Angle(0.0f);
    editGene(seed).setMassFlowPriority(1.0f);
    editGene(seed).setChild1GeneIndex(GeneIndex(kLeftBuilder));
    editGene(seed).setChild2GeneIndex(GeneIndex(kRightBuilder));
    editGene(seed).setMakesLink(true);
    editGene(seed).setChild1KeepsLinks(false);
    editGene(seed).setChild2KeepsLinks(false);
    editGene(seed).setLinkStrength(0.0f);
    editGene(seed).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(seed).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kSeed] = seed;

    Gene freeSeed = terminalGene(CellType::PHAGOCYTE, 0.32f, 0.78f, 0.98f);
    editGene(freeSeed).setSplitMass(0.153000f);
    editGene(freeSeed).setSplitRatio(0.5f);
    editGene(freeSeed).setSplitAngle(0.0f);
    editGene(freeSeed).setChild1Angle(0.0f);
    editGene(freeSeed).setChild2Angle(0.0f);
    editGene(freeSeed).setMassFlowPriority(2.0f);
    editGene(freeSeed).setChild1GeneIndex(GeneIndex(kLeftBuilder));
    editGene(freeSeed).setChild2GeneIndex(GeneIndex(kRightBuilder));
    editGene(freeSeed).setMakesLink(true);
    editGene(freeSeed).setChild1KeepsLinks(false);
    editGene(freeSeed).setChild2KeepsLinks(false);
    editGene(freeSeed).setLinkStrength(0.0f);
    editGene(freeSeed).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(freeSeed).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kFreeSeed] = freeSeed;

    // Left builder: creates an inert stereocyte anchor and the first motor.
    // The diagonal angles are inherited from the editor references because they
    // reliably unfold a 2-cell builder pair into a 2D module instead of a line.
    Gene leftBuilder = terminalGene(CellType::PHAGOCYTE, 0.30f, 0.74f, 0.96f);
    editGene(leftBuilder).setSplitMass(0.077400f);
    editGene(leftBuilder).setSplitRatio(0.5f);
    editGene(leftBuilder).setSplitAngle(static_cast<float>(M_PI * 0.5));
    editGene(leftBuilder).setChild1Angle(5.4978f);
    editGene(leftBuilder).setChild2Angle(5.4978f);
    editGene(leftBuilder).setMassFlowPriority(1.0f);
    editGene(leftBuilder).setChild1GeneIndex(GeneIndex(kAnchor));
    editGene(leftBuilder).setChild2GeneIndex(GeneIndex(kLeftMotor));
    editGene(leftBuilder).setMakesLink(true);
    editGene(leftBuilder).setChild1KeepsLinks(true);
    editGene(leftBuilder).setChild2KeepsLinks(true);
    editGene(leftBuilder).setLinkStrength(0.0f);
    editGene(leftBuilder).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(leftBuilder).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kLeftBuilder] = leftBuilder;

    // Right builder: creates the feeding body and a bud-builder. This is the key
    // difference from rsh-0: one corner stays reproductive instead of immediately
    // becoming terminal motor/body material.
    Gene rightBuilder(leftBuilder);
    editGene(rightBuilder).setSplitMass(0.064800f);
    editGene(rightBuilder).setChild1GeneIndex(GeneIndex(kBody));
    editGene(rightBuilder).setChild2GeneIndex(GeneIndex(kBudBuilder));
    genome[kRightBuilder] = rightBuilder;

    Gene anchor = terminalGene(CellType::STEREOCYTE, 0.76f, 0.76f, 0.84f);
    editGene(anchor).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(anchor).setMassFlowPriority(0.8f);
    editGene(anchor).setChild1GeneIndex(GeneIndex(kAnchor));
    editGene(anchor).setChild2GeneIndex(GeneIndex(kAnchor));
    editGene(anchor).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kAnchor] = anchor;

    Gene leftMotor = terminalGene(CellType::FLAGELLOCYTE, 0.96f, 0.90f, 0.24f);
    editGene(leftMotor).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(leftMotor).setMassFlowPriority(1.2f);
    editGene(leftMotor).setChild1GeneIndex(GeneIndex(kLeftMotor));
    editGene(leftMotor).setChild2GeneIndex(GeneIndex(kLeftMotor));
    // Raw -0.10 maps to about 0.45 actual swim force.
    setConstantParametricValue(leftMotor, Gene::SWIM_FORCE_VALUE_INDEX, -0.10f);
    genome[kLeftMotor] = leftMotor;

    Gene body = terminalGene(CellType::PHAGOCYTE, 0.24f, 0.72f, 0.96f);
    editGene(body).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(body).setMassFlowPriority(2.0f);
    editGene(body).setChild1GeneIndex(GeneIndex(kBody));
    editGene(body).setChild2GeneIndex(GeneIndex(kBody));
    editGene(body).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kBody] = body;

    // Bud builder: after the first module is stable, it emits a fresh seed plus
    // a rear motor. In this split topology old links stay with the child2 side,
    // so the rear motor is child2 and keeps the module frame; the seed is child1
    // and starts the next kite module as a free founder.
    Gene budBuilder = terminalGene(CellType::PHAGOCYTE, 0.42f, 0.86f, 0.98f);
    editGene(budBuilder).setSplitMass(0.230000f);
    editGene(budBuilder).setSplitRatio(0.52f);
    editGene(budBuilder).setSplitAngle(static_cast<float>(M_PI));
    editGene(budBuilder).setChild1Angle(0.0f);
    editGene(budBuilder).setChild2Angle(static_cast<float>(M_PI));
    editGene(budBuilder).setMassFlowPriority(1.0f);
    editGene(budBuilder).setChild1GeneIndex(GeneIndex(kFreeSeed));
    editGene(budBuilder).setChild2GeneIndex(GeneIndex(kRearMotor));
    editGene(budBuilder).setMakesLink(false);
    editGene(budBuilder).setChild1KeepsLinks(false);
    editGene(budBuilder).setChild2KeepsLinks(true);
    editGene(budBuilder).setLinkStrength(0.0f);
    editGene(budBuilder).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(budBuilder).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kBudBuilder] = budBuilder;

    Gene rearMotor(leftMotor);
    editGene(rearMotor).setChild1GeneIndex(GeneIndex(kRearMotor));
    editGene(rearMotor).setChild2GeneIndex(GeneIndex(kRearMotor));
    // Raw 0.20 maps to about 0.60 actual swim force. The rear motor is stronger
    // than the side motor, intentionally making the module curve rather than
    // translate like the ordinary two-cell swimmer.
    setConstantParametricValue(rearMotor, Gene::SWIM_FORCE_VALUE_INDEX, 0.20f);
    genome[kRearMotor] = rearMotor;

    // Verified phenotype:
    // preview 1.2s: stable four-cell kite, one component, four links.
    // preview 2.0-2.8s: transient seed-builder branch appears.
    // preview 3s+: weak branch is gone and the main kite has metamorphosed into
    // anchor + body + side motor + rear motor.
    // swim 3s: the four-cell kite stays connected and displaces by about 0.05.
    // Treat this as a metamorphic swimmer, not a reproducer.
    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createOutriggerSwimmerGenome() {
    constexpr int kSeed = 0;
    constexpr int kBodyBuilder = 1;
    constexpr int kLeftMotor = 2;
    constexpr int kBody = 3;
    constexpr int kRightMotor = 4;
    constexpr int kReproductiveSeed = 5;

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.30f, 0.32f, 0.34f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(CellData::MAX_SPLIT_MASS);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    // New attempt after kite_seed_swimmer_v1:
    // Do not reuse the rsh ring. Build a V/star module instead:
    //   left motor -- body -- right motor
    // There is intentionally no motor-motor link. The test target is not "more
    // cells", but different force geometry: two motors attached to one body,
    // with roughly parallel thrust so the organism should translate more than it
    // spins. If this rotates, the next change should be motor angle/thrust, not
    // another cell type swap.
    Gene seed = terminalGene(CellType::PHAGOCYTE, 0.28f, 0.76f, 0.98f);
    editGene(seed).setInitialCompatibilityFlag(true);
    editGene(seed).setSplitMass(0.153000f);
    editGene(seed).setSplitRatio(0.50f);
    editGene(seed).setSplitAngle(0.0f);
    editGene(seed).setChild1Angle(0.0f);
    editGene(seed).setChild2Angle(static_cast<float>(M_PI));
    editGene(seed).setMassFlowPriority(2.0f);
    editGene(seed).setChild1GeneIndex(GeneIndex(kBodyBuilder));
    editGene(seed).setChild2GeneIndex(GeneIndex(kLeftMotor));
    editGene(seed).setMakesLink(true);
    editGene(seed).setChild1KeepsLinks(true);
    editGene(seed).setChild2KeepsLinks(true);
    editGene(seed).setLinkStrength(0.0f);
    editGene(seed).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(seed).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kSeed] = seed;

    // Body builder keeps the old left-motor link on child1 and creates one new
    // link to the right motor. child2 does not inherit old links, preventing the
    // rsh-style closed ring.
    Gene bodyBuilder = terminalGene(CellType::PHAGOCYTE, 0.26f, 0.72f, 0.96f);
    editGene(bodyBuilder).setSplitMass(0.230000f);
    editGene(bodyBuilder).setSplitRatio(0.48f);
    editGene(bodyBuilder).setSplitAngle(static_cast<float>(M_PI * 0.5));
    editGene(bodyBuilder).setChild1Angle(0.0f);
    editGene(bodyBuilder).setChild2Angle(static_cast<float>(M_PI));
    editGene(bodyBuilder).setMassFlowPriority(2.0f);
    editGene(bodyBuilder).setChild1GeneIndex(GeneIndex(kBody));
    editGene(bodyBuilder).setChild2GeneIndex(GeneIndex(kRightMotor));
    editGene(bodyBuilder).setMakesLink(true);
    editGene(bodyBuilder).setChild1KeepsLinks(true);
    editGene(bodyBuilder).setChild2KeepsLinks(false);
    editGene(bodyBuilder).setLinkStrength(0.0f);
    editGene(bodyBuilder).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(bodyBuilder).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kBodyBuilder] = bodyBuilder;

    Gene leftMotor = terminalGene(CellType::FLAGELLOCYTE, 0.96f, 0.90f, 0.24f);
    editGene(leftMotor).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(leftMotor).setMassFlowPriority(1.1f);
    editGene(leftMotor).setChild1GeneIndex(GeneIndex(kLeftMotor));
    editGene(leftMotor).setChild2GeneIndex(GeneIndex(kLeftMotor));
    // Raw 0.0 maps to about 0.5 actual swim force.
    setConstantParametricValue(leftMotor, Gene::SWIM_FORCE_VALUE_INDEX, 0.0f);
    genome[kLeftMotor] = leftMotor;

    Gene body = terminalGene(CellType::PHAGOCYTE, 0.20f, 0.68f, 0.96f);
    editGene(body).setSplitMass(0.330000f);
    editGene(body).setSplitRatio(0.55f);
    editGene(body).setSplitAngle(static_cast<float>(M_PI));
    editGene(body).setChild1Angle(0.0f);
    editGene(body).setChild2Angle(static_cast<float>(M_PI));
    editGene(body).setMassFlowPriority(3.0f);
    editGene(body).setChild1GeneIndex(GeneIndex(kBody));
    editGene(body).setChild2GeneIndex(GeneIndex(kReproductiveSeed));
    editGene(body).setMakesLink(false);
    editGene(body).setChild1KeepsLinks(true);
    editGene(body).setChild2KeepsLinks(false);
    editGene(body).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    genome[kBody] = body;

    Gene rightMotor(leftMotor);
    editGene(rightMotor).setChild1GeneIndex(GeneIndex(kRightMotor));
    editGene(rightMotor).setChild2GeneIndex(GeneIndex(kRightMotor));
    setConstantParametricValue(rightMotor, Gene::SWIM_FORCE_VALUE_INDEX, 0.0f);
    genome[kRightMotor] = rightMotor;

    Gene reproductiveSeed(seed);
    editGene(reproductiveSeed).setInitialCompatibilityFlag(false);
    editGene(reproductiveSeed).setChild1GeneIndex(GeneIndex(kBodyBuilder));
    editGene(reproductiveSeed).setChild2GeneIndex(GeneIndex(kLeftMotor));
    genome[kReproductiveSeed] = reproductiveSeed;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createLarvalGliderGenome() {
    constexpr int kRoot = 0;
    constexpr int kSpineBuilder = 1;
    constexpr int kBallast = 2;
    constexpr int kBody = 3;
    constexpr int kMotorBud = 4;
    constexpr int kMotor = 5;

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.30f, 0.32f, 0.34f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(CellData::MAX_SPLIT_MASS);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    // Failed serious-organism attempt:
    // larval_glider_v1 separates morphogenesis from propulsion. The first stages
    // are non-motor builders, so swim mode cannot eject a half-built two-cell
    // organism. Only the final motor-bud split creates motor precursors.
    //
    // Intended graph:
    //   m1 root -> m2 spine builder + m3 ballast
    //   m2 -> m4 body + m5 motor bud
    //   m5 -> m6 delayed motor
    //
    // This is intentionally not an rsh ring, but it still failed the user's goal:
    // it became a small three-cell swimmer, not a large differentiated organism.
    // The mistake was treating delayed motor staging alone as enough "serious"
    // structure.
    Gene root = terminalGene(CellType::LIPOCYTE, 0.92f, 0.56f, 0.24f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.153000f);
    editGene(root).setSplitRatio(0.50f);
    editGene(root).setSplitAngle(0.0f);
    editGene(root).setChild1Angle(0.0f);
    editGene(root).setChild2Angle(static_cast<float>(M_PI));
    editGene(root).setMassFlowPriority(2.0f);
    editGene(root).setChild1GeneIndex(GeneIndex(kSpineBuilder));
    editGene(root).setChild2GeneIndex(GeneIndex(kBallast));
    editGene(root).setMakesLink(true);
    editGene(root).setChild1KeepsLinks(true);
    editGene(root).setChild2KeepsLinks(true);
    editGene(root).setLinkStrength(0.0f);
    editGene(root).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    genome[kRoot] = root;

    Gene spineBuilder = terminalGene(CellType::PHAGOCYTE, 0.20f, 0.68f, 0.96f);
    editGene(spineBuilder).setSplitMass(0.130000f);
    editGene(spineBuilder).setSplitRatio(0.48f);
    editGene(spineBuilder).setSplitAngle(static_cast<float>(M_PI * 0.5));
    editGene(spineBuilder).setChild1Angle(0.0f);
    editGene(spineBuilder).setChild2Angle(static_cast<float>(M_PI));
    editGene(spineBuilder).setMassFlowPriority(2.5f);
    editGene(spineBuilder).setChild1GeneIndex(GeneIndex(kBody));
    editGene(spineBuilder).setChild2GeneIndex(GeneIndex(kMotorBud));
    editGene(spineBuilder).setMakesLink(true);
    editGene(spineBuilder).setChild1KeepsLinks(true);
    editGene(spineBuilder).setChild2KeepsLinks(false);
    editGene(spineBuilder).setLinkStrength(0.0f);
    editGene(spineBuilder).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    genome[kSpineBuilder] = spineBuilder;

    Gene ballast = terminalGene(CellType::STEREOCYTE, 0.78f, 0.78f, 0.86f);
    editGene(ballast).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(ballast).setMassFlowPriority(0.8f);
    editGene(ballast).setChild1GeneIndex(GeneIndex(kBallast));
    editGene(ballast).setChild2GeneIndex(GeneIndex(kBallast));
    genome[kBallast] = ballast;

    Gene body = terminalGene(CellType::PHAGOCYTE, 0.16f, 0.66f, 0.96f);
    editGene(body).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(body).setMassFlowPriority(3.0f);
    editGene(body).setChild1GeneIndex(GeneIndex(kBody));
    editGene(body).setChild2GeneIndex(GeneIndex(kBody));
    genome[kBody] = body;

    Gene motorBud = terminalGene(CellType::PHAGOCYTE, 0.38f, 0.88f, 1.0f);
    editGene(motorBud).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(motorBud).setMassFlowPriority(1.5f);
    genome[kMotorBud] = motorBud;

    Gene motor = terminalGene(CellType::FLAGELLOCYTE, 0.96f, 0.90f, 0.24f);
    editGene(motor).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(motor).setMassFlowPriority(1.2f);
    editGene(motor).setChild1GeneIndex(GeneIndex(kMotor));
    editGene(motor).setChild2GeneIndex(GeneIndex(kMotor));
    // Raw -0.55 maps to about 0.225 actual swim force.
    setConstantParametricValue(motor, Gene::SWIM_FORCE_VALUE_INDEX, -0.55f);
    genome[kMotor] = motor;

    // Verified phenotype:
    // preview 1.2s: three-cell larva, one component, body linked to ballast and
    // delayed motor precursor.
    // preview 2s+: motor precursor stays attached to the body.
    // swim 3s: organism stays alive as a connected three-cell glider and displaces
    // by about 0.12. Postmortem: this is not a successful serious organism; it is
    // a minimal phase-switching swimmer. Next serious attempts must target at
    // least 6-10 stable adult cells, multiple functional modules, and a checked
    // reproduction or feeding path before accepting the design.
    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createFeatherFanShapeGenome() {
    constexpr int kRoot = 0;
    constexpr int kSpineLeft = 1;
    constexpr int kSpineRight = 2;
    constexpr int kRibLeft = 3;
    constexpr int kRibRight = 4;
    constexpr int kPhotoNode = 5;
    constexpr int kSpineTip = 6;
    constexpr int kRootPad = 7;

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHOTOCYTE, 0.32f, 0.72f, 0.58f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(CellData::MAX_SPLIT_MASS);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    auto structuralBuilder = [](CellType type, float r, float g, float b) {
        Gene gene = terminalGene(type, r, g, b);
        editGene(gene).setSplitMass(0.220f);
        editGene(gene).setSplitRatio(0.50f);
        editGene(gene).setMassFlowPriority(3.0f);
        editGene(gene).setMakesLink(true);
        editGene(gene).setChild1KeepsLinks(true);
        editGene(gene).setChild2KeepsLinks(false);
        editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.18f);
        editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.006f);
        editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
        editGene(gene).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
        return gene;
    };

    // Structural design goal:
    // Build a large visible form before adding any functional cells. The intended
    // adult geometry is a feather/leaf skeleton: one central spine and alternating
    // ribs. All construction cells are phagocytes/photocytes so the shape can be
    // tuned safely in preview/editor conditions. No devorocytes, no motors.
    //
    // V3 design note:
    // The previous version accidentally made a dense mesh because side branches
    // inherited spine links. For a readable feather, only the continuing builder
    // inherits old links. The emitted child is attached by exactly one new link
    // and then grows its own outward rib. That should produce spine+ribs instead
    // of a collision-prone net.
    //
    // Verified shape lesson:
    // V3 became the first useful structural result because it traded "many cells"
    // for readable topology: one connected center, three long rays, one short
    // root-pad ray, zero collisions, and no high-degree mesh. Do not increase the
    // spine connection cap just to get more cells: that produced 79 cells but
    // fragmented the organism into 14 components. The next improvement should
    // make the root-pad side a real fourth builder/ray, not globally loosen link
    // inheritance.
    //
    // V4 direction:
    // The fourth side is now a local root-pad builder. This is intentionally
    // narrow and local: it should extend the missing ray while preserving V3's
    // low-collision connected topology.
    //
    // Verified V4 lesson:
    // A four-step root-pad ray with child2 link inheritance made the center too
    // dense. Disabling that inheritance fragmented the organism. The accepted
    // compromise is a three-step root-pad ray with inherited local links: it keeps
    // one component, zero collisions, and a richer fan/leaf silhouette. This is
    // more interesting than V3, but still has only three strong endpoints; the
    // fourth side reads as a shorter central branch rather than a full ray.
    //
    // Verified V5 lesson:
    // The missing fourth ray was not fixed by "more growth". The decisive change
    // was geometric: rotate the root-pad split seam so the continuing builder
    // unfolds into the missing quadrant. With child2 link inheritance disabled,
    // this produced four equal rays but fragmented into five components. Keeping
    // child2 link inheritance restored connectivity while preserving the corrected
    // four-ray geometry: radial_fan, one component, zero collisions, zero cycles,
    // and no shape defects. For future shape work, treat split seam orientation as
    // the primary geometry control and link inheritance as the connectivity
    // control. Do not chase balance by globally adding connections or cells.
    //
    // Failed V6 symmetric-cap lesson:
    // A simple one-shot leafBud is not a safe way to make the symmetric cross
    // more organic. Putting buds on every emitted ray node either fragmented the
    // organism or made an overconnected mesh. Putting buds only at endpoints was
    // cleaner, but the rootPad side can still lose one inherited scaffold link
    // during the cap split. The next symmetric improvement should use explicit
    // stage-specific builders or a deliberate membrane grammar, not a generic
    // terminal bud bolted onto the V5 skeleton.
    Gene root = structuralBuilder(CellType::PHOTOCYTE, 0.22f, 0.82f, 0.62f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.240f);
    editGene(root).setSplitRatio(0.50f);
    editGene(root).setChild1GeneIndex(GeneIndex(kSpineLeft));
    editGene(root).setChild2GeneIndex(GeneIndex(kRootPad));
    editGene(root).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(root).setChild2Angle(static_cast<float>(-M_PI * 0.5));
    editGene(root).setChild2KeepsLinks(false);
    genome[kRoot] = root;

    Gene spineLeft = structuralBuilder(CellType::PHOTOCYTE, 0.18f, 0.76f, 0.92f);
    editGene(spineLeft).setSplitMass(0.220f);
    editGene(spineLeft).setSplitRatio(0.50f);
    editGene(spineLeft).setChild1GeneIndex(GeneIndex(kSpineRight));
    editGene(spineLeft).setChild2GeneIndex(GeneIndex(kRibLeft));
    editGene(spineLeft).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(spineLeft).setChild2Angle(0.0f);
    editGene(spineLeft).setChild2KeepsLinks(false);
    genome[kSpineLeft] = spineLeft;

    Gene spineRight(spineLeft);
    setColor(spineRight, 0.22f, 0.84f, 0.96f);
    editGene(spineRight).setChild1GeneIndex(GeneIndex(kSpineLeft));
    editGene(spineRight).setChild2GeneIndex(GeneIndex(kRibRight));
    editGene(spineRight).setChild1Angle(static_cast<float>(M_PI * 0.5));
    editGene(spineRight).setChild2Angle(static_cast<float>(M_PI));
    genome[kSpineRight] = spineRight;

    Gene ribLeft = structuralBuilder(CellType::PHAGOCYTE, 0.30f, 0.88f, 0.72f);
    editGene(ribLeft).setSplitMass(0.210f);
    editGene(ribLeft).setSplitRatio(0.50f);
    editGene(ribLeft).setChild1GeneIndex(GeneIndex(kRibLeft));
    editGene(ribLeft).setChild2GeneIndex(GeneIndex(kPhotoNode));
    editGene(ribLeft).setChild1Angle(0.0f);
    editGene(ribLeft).setChild2Angle(0.12f);
    editGene(ribLeft).setChild2KeepsLinks(true);
    editGene(ribLeft).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kRibLeft] = ribLeft;

    Gene ribRight(ribLeft);
    setColor(ribRight, 0.28f, 0.78f, 0.96f);
    editGene(ribRight).setChild1GeneIndex(GeneIndex(kRibRight));
    editGene(ribRight).setChild1Angle(static_cast<float>(M_PI));
    editGene(ribRight).setChild2Angle(static_cast<float>(M_PI - 0.12));
    genome[kRibRight] = ribRight;

    Gene photoNode = terminalGene(CellType::PHOTOCYTE, 0.80f, 0.96f, 0.42f);
    editGene(photoNode).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(photoNode).setChild1GeneIndex(GeneIndex(kPhotoNode));
    editGene(photoNode).setChild2GeneIndex(GeneIndex(kPhotoNode));
    editGene(photoNode).setLinkStrength(0.0f);
    editGene(photoNode).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(photoNode).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.25f);
    editGene(photoNode).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kPhotoNode] = photoNode;

    Gene spineTip = terminalGene(CellType::PHOTOCYTE, 0.95f, 0.98f, 0.52f);
    editGene(spineTip).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(spineTip).setChild1GeneIndex(GeneIndex(kSpineTip));
    editGene(spineTip).setChild2GeneIndex(GeneIndex(kSpineTip));
    editGene(spineTip).setLinkStrength(0.0f);
    editGene(spineTip).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(spineTip).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.35f);
    editGene(spineTip).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kSpineTip] = spineTip;

    Gene rootPad = structuralBuilder(CellType::PHAGOCYTE, 0.18f, 0.62f, 0.42f);
    editGene(rootPad).setSplitMass(0.210f);
    editGene(rootPad).setSplitRatio(0.50f);
    editGene(rootPad).setSplitAngle(static_cast<float>(-M_PI * 0.5));
    editGene(rootPad).setChild1GeneIndex(GeneIndex(kRootPad));
    editGene(rootPad).setChild2GeneIndex(GeneIndex(kPhotoNode));
    editGene(rootPad).setChild1Angle(static_cast<float>(-M_PI * 0.5));
    editGene(rootPad).setChild2Angle(static_cast<float>(-M_PI * 0.5 + 0.10));
    editGene(rootPad).setChild2KeepsLinks(true);
    editGene(rootPad).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.35f);
    editGene(rootPad).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kRootPad] = rootPad;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createStarfishShapeGenome() {
    constexpr int kRoot = 0;
    constexpr int kHubEmitter = 1;
    constexpr int kArmA = 2;
    constexpr int kArmB = 3;
    constexpr int kSideFlesh = 4;
    constexpr int kArmTip = 5;
    constexpr int kHubTerminal = 6;

    constexpr float kTurn72 = static_cast<float>(M_PI * 2.0 / 5.0);

    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHOTOCYTE, 0.42f, 0.78f, 0.56f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
        editGene(genome[i]).setSplitMass(CellData::MAX_SPLIT_MASS);
        editGene(genome[i]).setLinkStrength(0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        editGene(genome[i]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }

    auto builder = [](CellType type, float r, float g, float b) {
        Gene gene = terminalGene(type, r, g, b);
        editGene(gene).setSplitMass(0.205f);
        editGene(gene).setSplitRatio(0.50f);
        editGene(gene).setMassFlowPriority(3.2f);
        editGene(gene).setMakesLink(true);
        editGene(gene).setChild1KeepsLinks(true);
        editGene(gene).setChild2KeepsLinks(false);
        editGene(gene).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.16f);
        editGene(gene).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.006f);
        editGene(gene).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.05f);
        editGene(gene).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 4);
        return gene;
    };

    // Starfish design notes:
    // This is intentionally not "V5 cross plus decoration". A sea-star shape
    // needs a new grammar: first split creates a durable central hub and a
    // rotating emitter. The emitter stays linked to the hub and releases five
    // equivalent arms, turning 72 degrees after each release. Each emitted arm
    // inherits the hub link, so the adult graph should be a radial body rather
    // than the single serpentine path seen in the first failed attempt.
    //
    // Iteration result:
    // The connected version is mechanically healthy, but still reads as
    // partial_radial rather than a true sea star: the rotating emitter makes a
    // crescent/ring grammar, and the shape eyes find only 2-3 real branches.
    // Turning emitted arms 90 degrees outward produced a better conceptual
    // direction but fragmented into 20+ components because the arm seeds stopped
    // inheriting a usable scaffold link. The next serious sea-star attempt should
    // use explicit stage-specific fork genes that place five arm seeds around a
    // compact hub, instead of one rotating emitter walking around the perimeter.
    Gene root = builder(CellType::PHOTOCYTE, 0.98f, 0.76f, 0.38f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setSplitMass(0.215f);
    editGene(root).setSplitRatio(0.50f);
    editGene(root).setChild1GeneIndex(GeneIndex(kHubTerminal));
    editGene(root).setChild2GeneIndex(GeneIndex(kHubEmitter));
    editGene(root).setSplitAngle(0.0f);
    editGene(root).setChild1Angle(static_cast<float>(M_PI));
    editGene(root).setChild2Angle(0.0f);
    editGene(root).setChild1KeepsLinks(true);
    editGene(root).setChild2KeepsLinks(true);
    editGene(root).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.25f);
    genome[kRoot] = root;

    Gene hub = builder(CellType::PHOTOCYTE, 0.96f, 0.70f, 0.36f);
    editGene(hub).setSplitMass(0.215f);
    editGene(hub).setSplitRatio(0.50f);
    editGene(hub).setChild1GeneIndex(GeneIndex(kHubEmitter));
    editGene(hub).setChild2GeneIndex(GeneIndex(kArmA));
    editGene(hub).setSplitAngle(0.0f);
    editGene(hub).setChild1Angle(kTurn72);
    editGene(hub).setChild2Angle(0.0f);
    editGene(hub).setChild1KeepsLinks(true);
    editGene(hub).setChild2KeepsLinks(true);
    editGene(hub).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.25f);
    editGene(hub).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 6);
    genome[kHubEmitter] = hub;

    Gene armA = builder(CellType::PHAGOCYTE, 0.82f, 0.52f, 0.34f);
    editGene(armA).setSplitMass(0.195f);
    editGene(armA).setSplitRatio(0.50f);
    editGene(armA).setChild1GeneIndex(GeneIndex(kArmB));
    editGene(armA).setChild2GeneIndex(GeneIndex(kSideFlesh));
    editGene(armA).setChild1Angle(0.0f);
    editGene(armA).setChild2Angle(0.55f);
    editGene(armA).setChild2KeepsLinks(true);
    editGene(armA).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.2f);
    genome[kArmA] = armA;

    Gene armB(armA);
    setColor(armB, 0.92f, 0.58f, 0.38f);
    editGene(armB).setChild1GeneIndex(GeneIndex(kArmA));
    editGene(armB).setChild2Angle(-0.55f);
    genome[kArmB] = armB;

    Gene sideFlesh = terminalGene(CellType::PHOTOCYTE, 1.0f, 0.78f, 0.44f);
    editGene(sideFlesh).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(sideFlesh).setChild1GeneIndex(GeneIndex(kSideFlesh));
    editGene(sideFlesh).setChild2GeneIndex(GeneIndex(kSideFlesh));
    editGene(sideFlesh).setLinkStrength(0.0f);
    editGene(sideFlesh).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(sideFlesh).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.3f);
    editGene(sideFlesh).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kSideFlesh] = sideFlesh;

    Gene armTip = terminalGene(CellType::PHOTOCYTE, 1.0f, 0.90f, 0.50f);
    editGene(armTip).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(armTip).setChild1GeneIndex(GeneIndex(kArmTip));
    editGene(armTip).setChild2GeneIndex(GeneIndex(kArmTip));
    editGene(armTip).setLinkStrength(0.0f);
    editGene(armTip).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(armTip).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.45f);
    editGene(armTip).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 3);
    genome[kArmTip] = armTip;

    Gene hubTerminal = terminalGene(CellType::PHOTOCYTE, 0.96f, 0.66f, 0.32f);
    editGene(hubTerminal).setSplitMass(CellData::MAX_SPLIT_MASS);
    editGene(hubTerminal).setChild1GeneIndex(GeneIndex(kHubTerminal));
    editGene(hubTerminal).setChild2GeneIndex(GeneIndex(kHubTerminal));
    editGene(hubTerminal).setLinkStrength(0.0f);
    editGene(hubTerminal).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    editGene(hubTerminal).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.6f);
    editGene(hubTerminal).setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX, 6);
    genome[kHubTerminal] = hubTerminal;

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createSegmentedMedusaGenome() {
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        genome[i] = terminalGene(CellType::PHAGOCYTE, 0.35f, 0.35f, 0.38f);
        editGene(genome[i]).setChild1GeneIndex(GeneIndex(i));
        editGene(genome[i]).setChild2GeneIndex(GeneIndex(i));
        editGene(genome[i]).setInitialCompatibilityFlag(false);
    }

    Gene root = builderGene(CellType::STEMOCYTE, 0.72f, 0.92f, 1.0f);
    editGene(root).setInitialCompatibilityFlag(true);
    editGene(root).setChild1GeneIndex(GeneIndex(kBodyLeftGene));
    editGene(root).setChild2GeneIndex(GeneIndex(kDomeGene));
    editGene(root).setChild1Angle(0.0f);
    editGene(root).setChild2Angle(static_cast<float>(M_PI));
    editGene(root).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.6f);
    genome[kRootGene] = root;

    Gene bodyLeft = builderGene(CellType::MYOCYTE, 0.22f, 0.72f, 0.95f);
    editGene(bodyLeft).setChild1GeneIndex(GeneIndex(kBodyRightGene));
    editGene(bodyLeft).setChild2GeneIndex(GeneIndex(kTentacleLeftGene));
    editGene(bodyLeft).setChild1Angle(0.23f);
    editGene(bodyLeft).setChild2Angle(1.33f);
    editGene(bodyLeft).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.7f);
    genome[kBodyLeftGene] = bodyLeft;

    Gene bodyRight = builderGene(CellType::NEUROCYTE, 0.18f, 0.82f, 0.78f);
    editGene(bodyRight).setChild1GeneIndex(GeneIndex(kBodyLeftGene));
    editGene(bodyRight).setChild2GeneIndex(GeneIndex(kTentacleRightGene));
    editGene(bodyRight).setChild1Angle(-0.23f);
    editGene(bodyRight).setChild2Angle(-1.33f);
    editGene(bodyRight).setLinkStrength(CellData::MAX_LINK_STIFFNESS * 0.7f);
    genome[kBodyRightGene] = bodyRight;

    genome[kTentacleLeftGene] = tentacleGene(CellType::SECROCYTE, 0.95f, 0.42f, 0.62f, kTentacleLeftGene, 1.16f);
    genome[kTentacleRightGene] = tentacleGene(CellType::SENSEOCYTE, 0.76f, 0.48f, 1.0f, kTentacleRightGene, -1.16f);

    genome[kCiliaTipGene] = terminalGene(CellType::CILIOCYTE, 0.95f, 0.95f, 0.45f);
    editGene(genome[kCiliaTipGene]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.7f);
    editGene(genome[kCiliaTipGene]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.003f);

    genome[kDomeGene] = terminalGene(CellType::BUOYOCYTE, 0.85f, 0.96f, 1.0f);
    editGene(genome[kDomeGene]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.8f);
    editGene(genome[kDomeGene]).setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.009f);

    genome[kTailGene] = terminalGene(CellType::LIPOCYTE, 0.95f, 0.62f, 0.18f);
    editGene(genome[kTailGene]).setFloatValue(Gene::FloatType::CYTOSKELETON, 1.4f);

    genome[kPhotoBeadGene] = terminalGene(CellType::PHOTOCYTE, 0.2f, 0.95f, 0.68f);
    editGene(genome[kPhotoBeadGene]).setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 8.0f);
    editGene(genome[kPhotoBeadGene]).setFloatValue(Gene::FloatType::SMELL_RED, 0.2f);
    editGene(genome[kPhotoBeadGene]).setFloatValue(Gene::FloatType::SMELL_GREEN, 0.95f);
    editGene(genome[kPhotoBeadGene]).setFloatValue(Gene::FloatType::SMELL_BLUE, 0.68f);

    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> createGenomeByName(const std::string& name) {
    if (name == "segmented_medusa_v1" || name == "medusa") {
        return createSegmentedMedusaGenome();
    }
    if (name == "two_cell_swimmer_v1" || name == "swimmer") {
        return createTwoCellSwimmerGenome();
    }
    if (name == "ordinary_swimmer_reference" || name == "ordinary_swimmer") {
        return createOrdinarySwimmerReferenceGenome();
    }
    if (name == "rsh_0_reference" || name == "rsh-0" || name == "rsh") {
        return createRsh0ReferenceGenome();
    }
    if (name == "worm_reference" || name == "worm") {
        return createWormReferenceGenome();
    }
    if (name == "kite_seed_swimmer_v1" || name == "kite_seed") {
        return createKiteSeedSwimmerGenome();
    }
    if (name == "outrigger_swimmer_v1" || name == "outrigger") {
        return createOutriggerSwimmerGenome();
    }
    if (name == "larval_glider_v1" || name == "larval_glider") {
        return createLarvalGliderGenome();
    }
    if (name == "feather_fan_shape_v1" || name == "feather_fan" || name == "shape") {
        return createFeatherFanShapeGenome();
    }
    if (name == "starfish_shape_v1" || name == "starfish") {
        return createStarfishShapeGenome();
    }
    throw std::runtime_error("Unknown genome design: " + name);
}

std::string availableGenomeNames() {
    return "segmented_medusa_v1,two_cell_swimmer_v1,ordinary_swimmer_reference,rsh_0_reference,worm_reference,kite_seed_swimmer_v1,outrigger_swimmer_v1,larval_glider_v1,feather_fan_shape_v1,starfish_shape_v1";
}

Bounds boundsFor(const CellData& cellData) {
    Bounds bounds;
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        bounds.minX = std::min(bounds.minX, cellData.mPositionX[i]);
        bounds.maxX = std::max(bounds.maxX, cellData.mPositionX[i]);
        bounds.minY = std::min(bounds.minY, cellData.mPositionY[i]);
        bounds.maxY = std::max(bounds.maxY, cellData.mPositionY[i]);
    }
    if (!std::isfinite(bounds.minX)) {
        bounds = {};
    }
    return bounds;
}

int countComponents(const CellData& cellData) {
    std::vector<char> visited(cellData.mCellCount, false);
    int components = 0;
    std::vector<int> stack;
    for (int start = 0; start < cellData.mCellCount; start++) {
        if (visited[start] || cellData.mIsDead[start]) {
            continue;
        }
        components++;
        visited[start] = true;
        stack.push_back(start);
        while (!stack.empty()) {
            int cell = stack.back();
            stack.pop_back();
            for (int link = 0; link < cellData.mTopology.linkCount(cell); link++) {
                int other = cellData.mTopology.adjacentCell(cell, link);
                if (other < 0 || other >= cellData.mCellCount || visited[other] || cellData.mIsDead[other]) {
                    continue;
                }
                visited[other] = true;
                stack.push_back(other);
            }
        }
    }
    return components;
}

double mirrorScore(const CellData& cellData) {
    if (cellData.mCellCount <= 1) {
        return 1.0;
    }
    double cx = 0.0;
    for (int i = 0; i < cellData.mCellCount; i++) {
        cx += cellData.mPositionX[i];
    }
    cx /= static_cast<double>(cellData.mCellCount);
    double total = 0.0;
    int samples = 0;
    for (int i = 0; i < cellData.mCellCount; i++) {
        double targetX = 2.0 * cx - cellData.mPositionX[i];
        double targetY = cellData.mPositionY[i];
        double best = std::numeric_limits<double>::infinity();
        for (int j = 0; j < cellData.mCellCount; j++) {
            if (i == j) {
                continue;
            }
            double dx = targetX - cellData.mPositionX[j];
            double dy = targetY - cellData.mPositionY[j];
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        total += best;
        samples++;
    }
    double mean = total / std::max(1, samples);
    return 1.0 / (1.0 + mean / CellData::MAX_CELL_RADIUS);
}

double projectMajor(const ShapeDescriptor& shape, double x, double y) {
    double dx = x - shape.centerX;
    double dy = y - shape.centerY;
    return dx * shape.axisX + dy * shape.axisY;
}

double projectMinor(const ShapeDescriptor& shape, double x, double y) {
    double dx = x - shape.centerX;
    double dy = y - shape.centerY;
    return dx * shape.perpX + dy * shape.perpY;
}

ShapeDescriptor describeShape(const CellData& cellData) {
    ShapeDescriptor shape;
    int alive = 0;
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        shape.centerX += cellData.mPositionX[i];
        shape.centerY += cellData.mPositionY[i];
        alive++;
    }
    if (alive == 0) {
        return shape;
    }
    shape.valid = true;
    shape.centerX /= static_cast<double>(alive);
    shape.centerY /= static_cast<double>(alive);

    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        double dx = cellData.mPositionX[i] - shape.centerX;
        double dy = cellData.mPositionY[i] - shape.centerY;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
    }
    xx /= static_cast<double>(alive);
    xy /= static_cast<double>(alive);
    yy /= static_cast<double>(alive);

    double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    shape.axisX = std::cos(angle);
    shape.axisY = std::sin(angle);
    shape.perpX = -shape.axisY;
    shape.perpY = shape.axisX;

    auto fillExtents = [&]() {
        shape.majorMin = std::numeric_limits<double>::infinity();
        shape.majorMax = -std::numeric_limits<double>::infinity();
        shape.minorMin = std::numeric_limits<double>::infinity();
        shape.minorMax = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < cellData.mCellCount; i++) {
            if (cellData.mIsDead[i]) {
                continue;
            }
            double major = projectMajor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
            double minor = projectMinor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
            shape.majorMin = std::min(shape.majorMin, major - cellData.mRadius[i]);
            shape.majorMax = std::max(shape.majorMax, major + cellData.mRadius[i]);
            shape.minorMin = std::min(shape.minorMin, minor - cellData.mRadius[i]);
            shape.minorMax = std::max(shape.minorMax, minor + cellData.mRadius[i]);
        }
        shape.majorExtent = std::max(0.0, shape.majorMax - shape.majorMin);
        shape.minorExtent = std::max(0.0, shape.minorMax - shape.minorMin);
    };
    fillExtents();
    if (shape.minorExtent > shape.majorExtent) {
        std::swap(shape.axisX, shape.perpX);
        std::swap(shape.axisY, shape.perpY);
        fillExtents();
    }
    shape.elongation = shape.minorExtent > 0.000001
            ? shape.majorExtent / shape.minorExtent
            : (shape.majorExtent > 0.0 ? 999.0 : 0.0);

    std::array<double, kShapeProfileBins> minMinor{};
    std::array<double, kShapeProfileBins> maxMinor{};
    minMinor.fill(std::numeric_limits<double>::infinity());
    maxMinor.fill(-std::numeric_limits<double>::infinity());
    double majorRange = std::max(shape.majorExtent, 0.000001);
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        int degree = cellData.mTopology.linkCount(i);
        if (degree <= 1) {
            shape.endpoints++;
        }
        if (degree >= 3) {
            shape.junctions++;
        }
        if (degree >= 5) {
            shape.highDegree++;
        }
        double major = projectMajor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        double minor = projectMinor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        int bin = static_cast<int>((major - shape.majorMin) / majorRange * kShapeProfileBins);
        bin = std::clamp(bin, 0, kShapeProfileBins - 1);
        shape.binCounts[bin]++;
        minMinor[bin] = std::min(minMinor[bin], minor - cellData.mRadius[i]);
        maxMinor[bin] = std::max(maxMinor[bin], minor + cellData.mRadius[i]);
    }
    for (int bin = 0; bin < kShapeProfileBins; bin++) {
        if (shape.binCounts[bin] > 0 && std::isfinite(minMinor[bin])) {
            shape.binWidths[bin] = std::max(0.0, maxMinor[bin] - minMinor[bin]);
        }
    }
    shape.centralLoad = alive > 0
            ? static_cast<double>(shape.binCounts[kShapeProfileBins / 2]) / static_cast<double>(alive)
            : 0.0;

    double totalMirrorError = 0.0;
    int mirrorSamples = 0;
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        double major = projectMajor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        double minor = projectMinor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        double mirroredX = shape.centerX + major * shape.axisX - minor * shape.perpX;
        double mirroredY = shape.centerY + major * shape.axisY - minor * shape.perpY;
        double best = std::numeric_limits<double>::infinity();
        for (int j = 0; j < cellData.mCellCount; j++) {
            if (cellData.mIsDead[j]) {
                continue;
            }
            double dx = mirroredX - cellData.mPositionX[j];
            double dy = mirroredY - cellData.mPositionY[j];
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        totalMirrorError += best;
        mirrorSamples++;
    }
    double meanMirrorError = mirrorSamples > 0 ? totalMirrorError / static_cast<double>(mirrorSamples) : 0.0;
    shape.pcaSymmetry = 1.0 / (1.0 + meanMirrorError / CellData::MAX_CELL_RADIUS);

    int centerCell = -1;
    int bestDegree = -1;
    double bestCenterDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        int degree = cellData.mTopology.linkCount(i);
        double dx = cellData.mPositionX[i] - shape.centerX;
        double dy = cellData.mPositionY[i] - shape.centerY;
        double centerDistance = dx * dx + dy * dy;
        if (degree > bestDegree || (degree == bestDegree && centerDistance < bestCenterDistance)) {
            centerCell = i;
            bestDegree = degree;
            bestCenterDistance = centerDistance;
        }
    }
    shape.branchCenterCell = centerCell;
    if (centerCell >= 0 && bestDegree > 0) {
        std::vector<int> assigned(cellData.mCellCount, -1);
        assigned[centerCell] = -2;

        struct BranchRecord {
            int cells = 0;
            int depth = 0;
            double length = 0.0;
            double pathLength = 0.0;
            double straightness = 0.0;
        };

        std::vector<BranchRecord> branches;
        for (int link = 0; link < cellData.mTopology.linkCount(centerCell); link++) {
            int neighbor = cellData.mTopology.adjacentCell(centerCell, link);
            if (neighbor < 0
                    || neighbor >= cellData.mCellCount
                    || cellData.mIsDead[neighbor]
                    || assigned[neighbor] != -1) {
                continue;
            }
            int branchIndex = static_cast<int>(branches.size());
            BranchRecord record;
            std::vector<int> stack;
            std::vector<int> depth(cellData.mCellCount, 0);
            std::vector<double> pathLength(cellData.mCellCount, 0.0);
            assigned[neighbor] = branchIndex;
            depth[neighbor] = 1;
            double ndx = cellData.mPositionX[neighbor] - cellData.mPositionX[centerCell];
            double ndy = cellData.mPositionY[neighbor] - cellData.mPositionY[centerCell];
            pathLength[neighbor] = std::sqrt(ndx * ndx + ndy * ndy);
            stack.push_back(neighbor);
            while (!stack.empty()) {
                int cell = stack.back();
                stack.pop_back();
                record.cells++;
                record.depth = std::max(record.depth, depth[cell]);
                double dx = cellData.mPositionX[cell] - cellData.mPositionX[centerCell];
                double dy = cellData.mPositionY[cell] - cellData.mPositionY[centerCell];
                record.length = std::max(record.length, std::sqrt(dx * dx + dy * dy));
                record.pathLength = std::max(record.pathLength, pathLength[cell]);
                for (int nextLink = 0; nextLink < cellData.mTopology.linkCount(cell); nextLink++) {
                    int other = cellData.mTopology.adjacentCell(cell, nextLink);
                    if (other < 0
                            || other >= cellData.mCellCount
                            || other == centerCell
                            || cellData.mIsDead[other]
                            || assigned[other] != -1) {
                        continue;
                    }
                    double odx = cellData.mPositionX[other] - cellData.mPositionX[cell];
                    double ody = cellData.mPositionY[other] - cellData.mPositionY[cell];
                    assigned[other] = branchIndex;
                    depth[other] = depth[cell] + 1;
                    pathLength[other] = pathLength[cell] + std::sqrt(odx * odx + ody * ody);
                    stack.push_back(other);
                }
            }
            record.straightness = record.pathLength > 0.000001 ? record.length / record.pathLength : 0.0;
            branches.push_back(record);
        }
        std::sort(branches.begin(), branches.end(), [](const BranchRecord& a, const BranchRecord& b) {
            return a.length > b.length;
        });
        shape.branchCount = static_cast<int>(branches.size());
        double totalBranchLength = 0.0;
        double totalBranchStraightness = 0.0;
        for (int i = 0; i < static_cast<int>(branches.size()); i++) {
            const BranchRecord& branch = branches[i];
            totalBranchLength += branch.length;
            totalBranchStraightness += branch.straightness;
            shape.branchMaxLength = std::max(shape.branchMaxLength, branch.length);
            if (i < kShapeBranchSlots) {
                shape.branchCells[i] = branch.cells;
                shape.branchDepth[i] = branch.depth;
                shape.branchLength[i] = branch.length;
                shape.branchStraightness[i] = branch.straightness;
            }
        }
        double strongRayLengthThreshold = std::max(CellData::MAX_CELL_RADIUS * 4.0,
                shape.branchMaxLength * 0.65);
        for (const BranchRecord& branch : branches) {
            if (branch.depth >= 3
                    && branch.length >= strongRayLengthThreshold
                    && branch.straightness >= 0.45) {
                shape.strongRays++;
            } else {
                shape.shortBranches++;
            }
            if (branch.cells >= 3
                    && (branch.depth <= 2
                            || branch.length < strongRayLengthThreshold
                            || branch.straightness < 0.45)) {
                shape.clusteredBranches++;
            }
        }
        if (!branches.empty()) {
            shape.branchMeanLength = totalBranchLength / static_cast<double>(branches.size());
            shape.branchMeanStraightness = totalBranchStraightness / static_cast<double>(branches.size());
            double shortestBranch = branches.back().length;
            shape.branchBalance = shape.branchMaxLength > 0.000001
                    ? shortestBranch / shape.branchMaxLength
                    : 1.0;
        }
    }
    return shape;
}

std::string compactShapeProfile(const Metrics& metrics) {
    std::ostringstream out;
    out << "counts=";
    for (int i = 0; i < kShapeProfileBins; i++) {
        if (i > 0) {
            out << ",";
        }
        out << metrics.shapeBinCounts[i];
    }
    out << " widths=";
    out << std::fixed << std::setprecision(3);
    for (int i = 0; i < kShapeProfileBins; i++) {
        if (i > 0) {
            out << ",";
        }
        out << metrics.shapeBinWidths[i];
    }
    return out.str();
}

std::string compactBranchProfile(const Metrics& metrics) {
    std::ostringstream out;
    out << "center=" << metrics.shapeBranchCenterCell
        << " branches=" << metrics.shapeBranchCount
        << " strongRays=" << metrics.shapeStrongRays
        << " shortBranches=" << metrics.shapeShortBranches
        << " clusteredBranches=" << metrics.shapeClusteredBranches
        << " lengths=";
    out << std::fixed << std::setprecision(3);
    for (int i = 0; i < std::min(metrics.shapeBranchCount, kShapeBranchSlots); i++) {
        if (i > 0) {
            out << ",";
        }
        out << metrics.shapeBranchLength[i];
    }
    out << " depths=";
    for (int i = 0; i < std::min(metrics.shapeBranchCount, kShapeBranchSlots); i++) {
        if (i > 0) {
            out << ",";
        }
        out << metrics.shapeBranchDepth[i];
    }
    out << " straightness=";
    for (int i = 0; i < std::min(metrics.shapeBranchCount, kShapeBranchSlots); i++) {
        if (i > 0) {
            out << ",";
        }
        out << metrics.shapeBranchStraightness[i];
    }
    return out.str();
}

std::string shapeReadingLabel(const Metrics& metrics) {
    if (metrics.cells == 0) {
        return "empty";
    }
    if (metrics.components > 1) {
        return "fragmented_shape";
    }
    if (metrics.shapeElongation >= 2.25) {
        return "long_body";
    }
    if (metrics.shapeBranchCount >= 4 && metrics.shapeStrongRays == 3) {
        return "unbalanced_radial_fan";
    }
    if (metrics.shapeBranchCount >= 4 && metrics.shapeStrongRays >= 4) {
        return "radial_fan";
    }
    if (metrics.shapeBranchCount >= 3 && metrics.shapeClusteredBranches > 0) {
        return "radial_cluster";
    }
    if (metrics.shapeHighDegree > 0 || metrics.meanDegree >= 3.0) {
        return "overconnected_mesh";
    }
    if (metrics.shapeEndpoints >= 5 && metrics.shapeJunctions >= 2) {
        return "branching_tree";
    }
    if (metrics.shapeElongation >= 1.35 && metrics.shapeEndpoints >= 3) {
        return "fan_or_leaf";
    }
    if (metrics.shapeElongation < 1.25) {
        return "round_mass";
    }
    return "compact_body";
}

std::string silhouetteLabel(const Metrics& metrics) {
    if (metrics.components > 1) {
        return "fragmented";
    }
    if (metrics.shapeElongation >= 2.25) {
        return "long";
    }
    if (metrics.shapeElongation >= 1.45) {
        return "stretched";
    }
    if (metrics.shapeCentralLoad >= 0.35 && metrics.shapeStrongRays < 4) {
        return "center_heavy";
    }
    if (metrics.shapeElongation < 1.20) {
        return "round";
    }
    return "compact";
}

std::string topologyLabel(const Metrics& metrics) {
    if (metrics.components > 1) {
        return "fragmented";
    }
    if (metrics.shapeHighDegree > 0 || metrics.meanDegree >= 3.0) {
        return "mesh";
    }
    if (metrics.shapeCycleRank > 0) {
        return "loopy";
    }
    if (metrics.shapeJunctions >= 2) {
        return "branched";
    }
    return "tree";
}

std::string skeletonLabel(const Metrics& metrics) {
    if (metrics.shapeBranchCount >= 4 && metrics.shapeStrongRays >= 4) {
        return "balanced_radial";
    }
    if (metrics.shapeBranchCount >= 4 && metrics.shapeStrongRays == 3) {
        return "three_rays_plus_short_branch";
    }
    if (metrics.shapeBranchCount >= 3 && metrics.shapeStrongRays >= 2) {
        return "partial_radial";
    }
    if (metrics.shapeStrongRays == 2 && metrics.shapeElongation >= 1.8) {
        return "axis";
    }
    if (metrics.shapeClusteredBranches > 0) {
        return "clustered";
    }
    return "compact";
}

std::string balanceLabel(const Metrics& metrics) {
    if (metrics.shapeBranchCount <= 1) {
        return "none";
    }
    if (metrics.shapeBranchBalance >= 0.75) {
        return "balanced";
    }
    if (metrics.shapeBranchBalance >= 0.45) {
        return "uneven";
    }
    return "lopsided";
}

std::string healthLabel(const Metrics& metrics) {
    if (metrics.cells == 0) {
        return "dead";
    }
    if (metrics.components > 1) {
        return "broken";
    }
    if (metrics.lethalOverlapPairs > 0) {
        return "overlapping";
    }
    if (metrics.collisions > 0) {
        return "crowded";
    }
    return "clean";
}

std::string shapeSignature(const Metrics& metrics) {
    std::ostringstream out;
    out << "class=" << shapeReadingLabel(metrics)
        << " silhouette=" << silhouetteLabel(metrics)
        << " topology=" << topologyLabel(metrics)
        << " skeleton=" << skeletonLabel(metrics)
        << " balance=" << balanceLabel(metrics)
        << " centerLoad=" << std::fixed << std::setprecision(2) << metrics.shapeCentralLoad
        << " cycles=" << metrics.shapeCycleRank
        << " health=" << healthLabel(metrics);
    return out.str();
}

std::string shapeDefects(const Metrics& metrics) {
    std::vector<std::string> defects;
    if (metrics.components > 1) {
        defects.push_back("fragmented_components");
    }
    if (metrics.lethalOverlapPairs > 0) {
        defects.push_back("lethal_overlaps");
    } else if (metrics.collisions > 0) {
        defects.push_back("collisions");
    }
    if (metrics.shapeHighDegree > 0 || metrics.meanDegree >= 3.0) {
        defects.push_back("overconnected_mesh");
    }
    if (metrics.cells >= 12 && metrics.shapeCycleRank > std::max(2, metrics.cells / 8)) {
        defects.push_back("too_many_cycles");
    }
    if (metrics.shapeBranchCount >= 4 && metrics.shapeStrongRays < 4) {
        defects.push_back("missing_full_radial_ray");
    }
    if (metrics.shapeBranchCount >= 3 && metrics.shapeBranchBalance < 0.65) {
        defects.push_back("branch_length_imbalance");
    }
    if (metrics.shapeBranchCount >= 3 && metrics.shapeClusteredBranches > 0) {
        defects.push_back("clustered_branch");
    }
    if (metrics.shapeCentralLoad >= 0.35 && metrics.shapeStrongRays < 4) {
        defects.push_back("center_heavy");
    }
    if (defects.empty()) {
        return "none";
    }
    std::ostringstream out;
    for (size_t i = 0; i < defects.size(); i++) {
        if (i > 0) {
            out << ",";
        }
        out << defects[i];
    }
    return out.str();
}

char shapeCellMarker(CellType type) {
    if (type == CellType::DEVOROCYTE) {
        return 'D';
    }
    if (isMotorType(type)) {
        return 'M';
    }
    if (type == CellType::MYOCYTE) {
        return 'A';
    }
    if (isFunctionalLayerType(type)) {
        return 'F';
    }
    if (type == CellType::PHOTOCYTE) {
        return 'P';
    }
    if (type == CellType::PHAGOCYTE) {
        return '#';
    }
    if (type == CellType::LIPOCYTE) {
        return 'L';
    }
    return 'o';
}

void printShapeMap(const CellData& cellData) {
    ShapeDescriptor shape = describeShape(cellData);
    if (!shape.valid) {
        std::cout << "shape_map empty\n";
        return;
    }
    constexpr int gridWidth = 61;
    constexpr int gridHeight = 23;
    std::array<std::string, gridHeight> grid;
    for (std::string& row : grid) {
        row.assign(gridWidth, '.');
    }

    double majorRange = std::max(shape.majorExtent, CellData::MAX_CELL_RADIUS);
    double minorRange = std::max(shape.minorExtent, CellData::MAX_CELL_RADIUS);
    double majorPadding = majorRange * 0.08;
    double minorPadding = minorRange * 0.12;
    double majorMin = shape.majorMin - majorPadding;
    double majorMax = shape.majorMax + majorPadding;
    double minorMin = shape.minorMin - minorPadding;
    double minorMax = shape.minorMax + minorPadding;
    double paddedMajorRange = std::max(majorMax - majorMin, CellData::MAX_CELL_RADIUS);
    double paddedMinorRange = std::max(minorMax - minorMin, CellData::MAX_CELL_RADIUS);

    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            continue;
        }
        double major = projectMajor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        double minor = projectMinor(shape, cellData.mPositionX[i], cellData.mPositionY[i]);
        int gx = static_cast<int>(std::round((major - majorMin) / paddedMajorRange * (gridWidth - 1)));
        int gy = static_cast<int>(std::round((minorMax - minor) / paddedMinorRange * (gridHeight - 1)));
        gx = std::clamp(gx, 0, gridWidth - 1);
        gy = std::clamp(gy, 0, gridHeight - 1);
        char marker = shapeCellMarker(cellData.mActiveCellType[i]);
        int degree = cellData.mTopology.linkCount(i);
        if (i == shape.branchCenterCell) {
            marker = 'C';
        } else if (degree >= 3 && !isFunctionalLayerType(cellData.mActiveCellType[i])) {
            marker = '+';
        }
        if (degree <= 1 && marker == '#') {
            marker = 'e';
        }
        grid[gy][gx] = grid[gy][gx] == '.' ? marker : '*';
    }

    std::cout << "shape_map pca_major_axis=left_to_right markers C=branch_center #=phagocyte P=photocyte L=lipocyte M=motor A=myocyte F=functional D=devorocyte +=junction e=structural_endpoint *=overlap\n";
    for (const std::string& row : grid) {
        std::cout << "  " << row << "\n";
    }
}

Metrics measure(const SimulationWorld& world, int splitEvents) {
    const CellData& cellData = world.mCellData;
    Metrics metrics;
    metrics.time = world.mTime;
    metrics.cells = cellData.mCellCount;
    metrics.edges = cellData.mTopology.edgeCount();
    metrics.components = countComponents(cellData);
    metrics.splitEvents = splitEvents;
    metrics.mirrorScore = mirrorScore(cellData);
    ShapeDescriptor shape = describeShape(cellData);
    metrics.shapeMajor = shape.majorExtent;
    metrics.shapeMinor = shape.minorExtent;
    metrics.shapeElongation = shape.elongation;
    metrics.shapeSymmetry = shape.pcaSymmetry;
    metrics.shapeEndpoints = shape.endpoints;
    metrics.shapeJunctions = shape.junctions;
    metrics.shapeHighDegree = shape.highDegree;
    metrics.shapeBranchCenterCell = shape.branchCenterCell;
    metrics.shapeBranchCount = shape.branchCount;
    metrics.shapeStrongRays = shape.strongRays;
    metrics.shapeShortBranches = shape.shortBranches;
    metrics.shapeClusteredBranches = shape.clusteredBranches;
    metrics.shapeBranchMaxLength = shape.branchMaxLength;
    metrics.shapeBranchMeanLength = shape.branchMeanLength;
    metrics.shapeBranchMeanStraightness = shape.branchMeanStraightness;
    metrics.shapeBranchBalance = shape.branchBalance;
    metrics.shapeCentralLoad = shape.centralLoad;
    metrics.shapeCycleRank = std::max(0, metrics.edges - metrics.cells + metrics.components);
    metrics.shapeBinCounts = shape.binCounts;
    metrics.shapeBinWidths = shape.binWidths;
    metrics.shapeBranchCells = shape.branchCells;
    metrics.shapeBranchDepth = shape.branchDepth;
    metrics.shapeBranchLength = shape.branchLength;
    metrics.shapeBranchStraightness = shape.branchStraightness;
    Bounds bounds = boundsFor(cellData);
    metrics.width = std::max(0.0, bounds.maxX - bounds.minX);
    metrics.height = std::max(0.0, bounds.maxY - bounds.minY);
    metrics.aspect = metrics.height > 0.0 ? metrics.width / metrics.height : 0.0;
    metrics.meanDegree = metrics.cells > 0 ? 2.0 * metrics.edges / static_cast<double>(metrics.cells) : 0.0;
    if (metrics.cells > 0) {
        for (int i = 0; i < cellData.mCellCount; i++) {
            metrics.centerX += cellData.mPositionX[i];
            metrics.centerY += cellData.mPositionY[i];
        }
        metrics.centerX /= static_cast<double>(metrics.cells);
        metrics.centerY /= static_cast<double>(metrics.cells);
    }

    metrics.minCenterDistance = std::numeric_limits<double>::infinity();
    metrics.minSurfaceGap = std::numeric_limits<double>::infinity();
    for (int i = 0; i < cellData.mCellCount; i++) {
        if (cellData.mIsDead[i]) {
            metrics.dead++;
        }
        if (cellData.mMass[i] <= CellData::MIN_MASS * 1.02) {
            metrics.lowMassCells++;
        }
        double radiusLimit = world.mEnvironment.mRadius;
        if (cellData.mPositionX[i] * cellData.mPositionX[i] + cellData.mPositionY[i] * cellData.mPositionY[i]
                > radiusLimit * radiusLimit) {
            metrics.outOfBoundsCells++;
        }
        if (cellData.mToxication[i] > 0.5f || cellData.mToxin[i] > 0.5f) {
            metrics.toxicCells++;
        }
        int gene = cellData.activeGeneIndex(i).value();
        if (gene >= 0 && gene < CellData::MAX_GENES_COUNT) {
            metrics.geneCounts[gene]++;
            if (gene == kCiliaTipGene || gene == kDomeGene || gene == kTailGene || gene == kPhotoBeadGene) {
                metrics.terminalGeneCells++;
            }
        }
        CellType type = cellData.mActiveCellType[i];
        metrics.typeCounts[CellTypes::ordinal(type)]++;
        if (isStructuralPrototypeType(type)) {
            metrics.structuralPrototypeCells++;
        }
        if (isFunctionalLayerType(type)) {
            metrics.functionalCells++;
        }
        if (type == CellType::DEVOROCYTE) {
            metrics.devorocyteCells++;
        }
        if (isMotorType(type)) {
            metrics.motorCells++;
        }
        if (isActuatorType(type)) {
            metrics.actuatorCells++;
        }
        for (int j = i + 1; j < cellData.mCellCount; j++) {
            double dx = cellData.mPositionX[i] - cellData.mPositionX[j];
            double dy = cellData.mPositionY[i] - cellData.mPositionY[j];
            double centerDistance = std::sqrt(dx * dx + dy * dy);
            double gap = centerDistance - cellData.mRadius[i] - cellData.mRadius[j];
            metrics.minCenterDistance = std::min(metrics.minCenterDistance, centerDistance);
            metrics.minSurfaceGap = std::min(metrics.minSurfaceGap, gap);
            if (gap < -CellData::MAX_CELL_RADIUS * 0.35) {
                metrics.collisions++;
            }
            if (gap < -std::min(cellData.mRadius[i], cellData.mRadius[j]) * 0.65) {
                metrics.lethalOverlapPairs++;
            }
        }
    }
    if (!std::isfinite(metrics.minCenterDistance)) {
        metrics.minCenterDistance = 0.0;
    }
    if (!std::isfinite(metrics.minSurfaceGap)) {
        metrics.minSurfaceGap = 0.0;
    }

    double targetCells = 32.0;
    double cellScore = std::max(0.0, 1.0 - std::abs(metrics.cells - targetCells) / targetCells);
    double shapeScore = std::min(metrics.width, metrics.height) > 0.0
            ? std::min(metrics.width, metrics.height) / std::max(metrics.width, metrics.height)
            : 0.0;
    double linkScore = metrics.components == 1 ? 1.0 : 0.0;
    double terminalScore = metrics.cells > 0 ? static_cast<double>(metrics.terminalGeneCells) / metrics.cells : 0.0;
    double collisionPenalty = std::min(1.0, metrics.collisions / 8.0);
    metrics.score = cellScore * 0.25
            + shapeScore * 0.20
            + linkScore * 0.20
            + metrics.mirrorScore * 0.20
            + terminalScore * 0.15
            - collisionPenalty * 0.25;
    return metrics;
}

void printCounts(const char* label, const std::array<int, CellData::MAX_GENES_COUNT>& counts) {
    std::cout << label;
    for (int i = 0; i < CellData::MAX_GENES_COUNT; i++) {
        if (counts[i] > 0) {
            std::cout << " m" << (i + 1) << "=" << counts[i];
        }
    }
    std::cout << "\n";
}

void printTextReport(const Metrics& metrics, const std::vector<Metrics>& samples) {
    std::cout << std::fixed << std::setprecision(5);
    std::cout << "final"
              << " time=" << metrics.time
              << " cells=" << metrics.cells
              << " edges=" << metrics.edges
              << " components=" << metrics.components
              << " splitEvents=" << metrics.splitEvents
              << " collisions=" << metrics.collisions
              << " lethalOverlaps=" << metrics.lethalOverlapPairs
              << " lostSincePrev=" << metrics.lostSincePreviousSample
              << " lowMass=" << metrics.lowMassCells
              << " outOfBounds=" << metrics.outOfBoundsCells
              << " toxic=" << metrics.toxicCells
              << " structural=" << metrics.structuralPrototypeCells
              << " functional=" << metrics.functionalCells
              << " devorocytes=" << metrics.devorocyteCells
              << " actuators=" << metrics.actuatorCells
              << " width=" << metrics.width
              << " height=" << metrics.height
              << " centerX=" << metrics.centerX
              << " centerY=" << metrics.centerY
              << " displacement=" << metrics.displacement
              << " aspect=" << metrics.aspect
              << " minGap=" << metrics.minSurfaceGap
              << " mirror=" << metrics.mirrorScore
              << " shape=" << shapeReadingLabel(metrics)
              << " shapeMajor=" << metrics.shapeMajor
              << " shapeMinor=" << metrics.shapeMinor
              << " shapeElongation=" << metrics.shapeElongation
              << " shapeEndpoints=" << metrics.shapeEndpoints
              << " shapeJunctions=" << metrics.shapeJunctions
              << " shapeHighDegree=" << metrics.shapeHighDegree
              << " shapeBranches=" << metrics.shapeBranchCount
              << " strongRays=" << metrics.shapeStrongRays
              << " clusteredBranches=" << metrics.shapeClusteredBranches
              << " branchBalance=" << metrics.shapeBranchBalance
              << " centralLoad=" << metrics.shapeCentralLoad
              << " cycles=" << metrics.shapeCycleRank
              << " shapeSymmetry=" << metrics.shapeSymmetry
              << " score=" << metrics.score
              << "\n";
    std::cout << "shape_signature " << shapeSignature(metrics) << "\n";
    std::cout << "shape_defects " << shapeDefects(metrics) << "\n";
    std::cout << "shape_profile " << compactShapeProfile(metrics) << "\n";
    std::cout << "branch_profile " << compactBranchProfile(metrics) << "\n";
    printCounts("genes", metrics.geneCounts);
    std::cout << "samples\n";
    for (size_t i = 0; i < samples.size(); i++) {
        const Metrics& p = samples[i];
        std::cout << "  sample=" << i
                  << " time=" << p.time
                  << " cells=" << p.cells
                  << " edges=" << p.edges
                  << " components=" << p.components
                  << " collisions=" << p.collisions
                  << " lethalOverlaps=" << p.lethalOverlapPairs
                  << " lostSincePrev=" << p.lostSincePreviousSample
                  << " lowMass=" << p.lowMassCells
                  << " structural=" << p.structuralPrototypeCells
                  << " functional=" << p.functionalCells
                  << " devorocytes=" << p.devorocyteCells
                  << " width=" << p.width
                  << " height=" << p.height
                  << " shape=" << shapeReadingLabel(p)
                  << " elongation=" << p.shapeElongation
                  << " endpoints=" << p.shapeEndpoints
                  << " junctions=" << p.shapeJunctions
                  << " strongRays=" << p.shapeStrongRays
                  << " clusteredBranches=" << p.shapeClusteredBranches
                  << " displacement=" << p.displacement
                  << " score=" << p.score
                  << "\n";
    }
}

void printJsonReport(const Metrics& metrics, const std::vector<Metrics>& samples) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "{\n";
    std::cout << "  \"final\": {"
              << "\"time\": " << metrics.time
              << ", \"cells\": " << metrics.cells
              << ", \"edges\": " << metrics.edges
              << ", \"components\": " << metrics.components
              << ", \"splitEvents\": " << metrics.splitEvents
              << ", \"collisions\": " << metrics.collisions
              << ", \"lethalOverlaps\": " << metrics.lethalOverlapPairs
              << ", \"lostSincePreviousSample\": " << metrics.lostSincePreviousSample
              << ", \"lowMassCells\": " << metrics.lowMassCells
              << ", \"outOfBoundsCells\": " << metrics.outOfBoundsCells
              << ", \"toxicCells\": " << metrics.toxicCells
              << ", \"structuralPrototypeCells\": " << metrics.structuralPrototypeCells
              << ", \"functionalCells\": " << metrics.functionalCells
              << ", \"devorocyteCells\": " << metrics.devorocyteCells
              << ", \"actuatorCells\": " << metrics.actuatorCells
              << ", \"width\": " << metrics.width
              << ", \"height\": " << metrics.height
              << ", \"centerX\": " << metrics.centerX
              << ", \"centerY\": " << metrics.centerY
              << ", \"displacement\": " << metrics.displacement
              << ", \"aspect\": " << metrics.aspect
              << ", \"minSurfaceGap\": " << metrics.minSurfaceGap
              << ", \"mirrorScore\": " << metrics.mirrorScore
              << ", \"shapeReading\": \"" << shapeReadingLabel(metrics) << "\""
              << ", \"shapeMajor\": " << metrics.shapeMajor
              << ", \"shapeMinor\": " << metrics.shapeMinor
              << ", \"shapeElongation\": " << metrics.shapeElongation
              << ", \"shapeSymmetry\": " << metrics.shapeSymmetry
              << ", \"shapeEndpoints\": " << metrics.shapeEndpoints
              << ", \"shapeJunctions\": " << metrics.shapeJunctions
              << ", \"shapeHighDegree\": " << metrics.shapeHighDegree
              << ", \"shapeBranchCount\": " << metrics.shapeBranchCount
              << ", \"shapeStrongRays\": " << metrics.shapeStrongRays
              << ", \"shapeShortBranches\": " << metrics.shapeShortBranches
              << ", \"shapeClusteredBranches\": " << metrics.shapeClusteredBranches
              << ", \"shapeBranchMaxLength\": " << metrics.shapeBranchMaxLength
              << ", \"shapeBranchMeanLength\": " << metrics.shapeBranchMeanLength
              << ", \"shapeBranchMeanStraightness\": " << metrics.shapeBranchMeanStraightness
              << ", \"shapeBranchBalance\": " << metrics.shapeBranchBalance
              << ", \"shapeCentralLoad\": " << metrics.shapeCentralLoad
              << ", \"shapeCycleRank\": " << metrics.shapeCycleRank
              << ", \"shapeSignature\": \"" << shapeSignature(metrics) << "\""
              << ", \"shapeDefects\": \"" << shapeDefects(metrics) << "\""
              << ", \"score\": " << metrics.score
              << "},\n";
    std::cout << "  \"samples\": [\n";
    for (size_t i = 0; i < samples.size(); i++) {
        const Metrics& p = samples[i];
        std::cout << "    {\"sample\": " << i
                  << ", \"time\": " << p.time
                  << ", \"cells\": " << p.cells
                  << ", \"edges\": " << p.edges
                  << ", \"components\": " << p.components
                  << ", \"collisions\": " << p.collisions
                  << ", \"lethalOverlaps\": " << p.lethalOverlapPairs
                  << ", \"lostSincePreviousSample\": " << p.lostSincePreviousSample
                  << ", \"lowMassCells\": " << p.lowMassCells
                  << ", \"structuralPrototypeCells\": " << p.structuralPrototypeCells
                  << ", \"functionalCells\": " << p.functionalCells
                  << ", \"devorocyteCells\": " << p.devorocyteCells
                  << ", \"width\": " << p.width
                  << ", \"height\": " << p.height
                  << ", \"shapeReading\": \"" << shapeReadingLabel(p) << "\""
                  << ", \"shapeElongation\": " << p.shapeElongation
                  << ", \"shapeEndpoints\": " << p.shapeEndpoints
                  << ", \"shapeJunctions\": " << p.shapeJunctions
                  << ", \"shapeStrongRays\": " << p.shapeStrongRays
                  << ", \"shapeClusteredBranches\": " << p.shapeClusteredBranches
                  << ", \"displacement\": " << p.displacement
                  << ", \"score\": " << p.score
                  << "}";
        std::cout << (i + 1 == samples.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
}

const Metrics& maxByCells(const std::vector<Metrics>& samples, const Metrics& fallback) {
    if (samples.empty()) {
        return fallback;
    }
    return *std::max_element(samples.begin(), samples.end(), [](const Metrics& a, const Metrics& b) {
        return a.cells < b.cells;
    });
}

const Metrics& maxByLongAxis(const std::vector<Metrics>& samples, const Metrics& fallback) {
    if (samples.empty()) {
        return fallback;
    }
    return *std::max_element(samples.begin(), samples.end(), [](const Metrics& a, const Metrics& b) {
        return std::max(a.width, a.height) < std::max(b.width, b.height);
    });
}

const Metrics* sampleAtOrAfter(const std::vector<Metrics>& samples, double time) {
    for (const Metrics& sample : samples) {
        if (sample.time + SimulationWorld::DELTA_TIME * 0.5 >= time) {
            return &sample;
        }
    }
    return samples.empty() ? nullptr : &samples.back();
}

double axisRatio(const Metrics& metrics) {
    double shortAxis = std::min(metrics.width, metrics.height);
    double longAxis = std::max(metrics.width, metrics.height);
    return shortAxis > 0.000001 ? longAxis / shortAxis : (longAxis > 0.0 ? 999.0 : 0.0);
}

double structuralPrototypeRatio(const Metrics& metrics) {
    return metrics.cells > 0
            ? static_cast<double>(metrics.structuralPrototypeCells) / static_cast<double>(metrics.cells)
            : 0.0;
}

double functionalLayerRatio(const Metrics& metrics) {
    return metrics.cells > 0
            ? static_cast<double>(metrics.functionalCells) / static_cast<double>(metrics.cells)
            : 0.0;
}

std::string designPhaseLabel(const Metrics& metrics) {
    if (metrics.cells == 0) {
        return "extinct";
    }
    if (metrics.devorocyteCells > 0 && structuralPrototypeRatio(metrics) >= 0.5) {
        return "unsafe_shape_prototype";
    }
    if (structuralPrototypeRatio(metrics) >= 0.7 && metrics.actuatorCells == 0) {
        return "shape_prototype";
    }
    if (structuralPrototypeRatio(metrics) >= 0.45 && functionalLayerRatio(metrics) <= 0.45) {
        return "mostly_structural";
    }
    if (metrics.actuatorCells > 0 || functionalLayerRatio(metrics) > 0.45) {
        return "functionalized_body";
    }
    return "mixed_body";
}

int countSamplesWithLoss(const std::vector<Metrics>& samples) {
    int count = 0;
    for (const Metrics& sample : samples) {
        if (sample.lostSincePreviousSample > 0) {
            count++;
        }
    }
    return count;
}

int countConnectedSamples(const std::vector<Metrics>& samples) {
    int count = 0;
    for (const Metrics& sample : samples) {
        if (sample.cells > 0 && sample.components == 1) {
            count++;
        }
    }
    return count;
}

bool hasCompactModuleStage(const std::vector<Metrics>& samples) {
    for (const Metrics& sample : samples) {
        if (sample.cells >= 4
                && sample.cells <= 10
                && sample.components == 1
                && sample.edges >= sample.cells
                && axisRatio(sample) < 2.0) {
            return true;
        }
    }
    return false;
}

int countRootReemitEvents(const std::vector<SplitEventInfo>& events, double& firstTime) {
    int count = 0;
    firstTime = -1.0;
    for (const SplitEventInfo& info : events) {
        const SimulationWorld::SplitEvent& event = info.event;
        if (event.parentGeneIndex != 0 && (event.child1GeneIndex == 0 || event.child2GeneIndex == 0)) {
            if (firstTime < 0.0) {
                firstTime = event.splitTime;
            }
            count++;
        }
    }
    return count;
}

std::string classifyPhenotype(const LabConfig& config,
        const Metrics& finalMetrics,
        const std::vector<Metrics>& samples,
        const std::vector<SplitEventInfo>& events) {
    if (finalMetrics.cells == 0) {
        return config.mode == "swim" ? "failed_swim_extinction" : "extinct";
    }
    const Metrics& largest = maxByCells(samples, finalMetrics);
    const Metrics& longest = maxByLongAxis(samples, finalMetrics);
    double rootReemitTime = -1.0;
    int rootReemitCount = countRootReemitEvents(events, rootReemitTime);
    double finalAxis = axisRatio(finalMetrics);
    double longestAxis = axisRatio(longest);
    bool mostlyConnected = countConnectedSamples(samples) * 2 >= static_cast<int>(samples.size());
    bool compactStage = hasCompactModuleStage(samples);
    bool lateGrowth = samples.size() >= 3 && finalMetrics.cells >= largest.cells * 0.8
            && finalMetrics.time > samples[samples.size() / 2].time
            && finalMetrics.cells > samples[samples.size() / 2].cells;

    if (rootReemitCount >= 1 && finalMetrics.components > 1 && finalAxis >= 2.0) {
        return "population_emitter";
    }
    if (rootReemitCount >= 2 && mostlyConnected && longestAxis >= 2.0 && lateGrowth) {
        return "recursive_segmented_body";
    }
    if (compactStage || (mostlyConnected && finalMetrics.cells >= 4 && finalMetrics.cells <= 10 && finalAxis < 2.0)) {
        return "compact_mechanical_module";
    }
    if (mostlyConnected && longestAxis >= 2.5 && longest.cells >= 6) {
        return "elongated_body";
    }
    if (rootReemitCount >= 1) {
        return "reproductive_but_unresolved";
    }
    return "unclassified_motif";
}

void printAnalysisReport(const LabConfig& config,
        const Metrics& finalMetrics,
        const std::vector<Metrics>& samples,
        const std::vector<SplitEventInfo>& events) {
    const Metrics& largest = maxByCells(samples, finalMetrics);
    const Metrics& longest = maxByLongAxis(samples, finalMetrics);
    double rootReemitTime = -1.0;
    int rootReemitCount = countRootReemitEvents(events, rootReemitTime);
    int connectedSamples = countConnectedSamples(samples);
    int lossSamples = countSamplesWithLoss(samples);
    const Metrics* early = sampleAtOrAfter(samples, std::min(3.0, finalMetrics.time));
    const Metrics* mid = sampleAtOrAfter(samples, finalMetrics.time * 0.5);

    std::string phenotype = classifyPhenotype(config, finalMetrics, samples, events);

    std::cout << "analysis\n";
    std::cout << "  phenotype=" << phenotype
              << " mode=" << config.mode
              << " checkpoints=" << samples.size()
              << "\n";
    std::cout << "  growth"
              << " finalCells=" << finalMetrics.cells
              << " maxCells=" << largest.cells << "@" << largest.time
              << " splitEvents=" << finalMetrics.splitEvents
              << " rootReemit=" << rootReemitCount;
    if (rootReemitTime >= 0.0) {
        std::cout << " firstRootReemit=" << rootReemitTime;
    }
    std::cout << "\n";
    std::cout << "  geometry"
              << " finalAxis=" << axisRatio(finalMetrics)
              << " longestAxis=" << axisRatio(longest) << "@" << longest.time
              << " longestWidth=" << longest.width
              << " longestHeight=" << longest.height
              << " finalComponents=" << finalMetrics.components
              << " connectedSamples=" << connectedSamples << "/" << samples.size()
              << "\n";
    std::cout << "  shape_vision"
              << " reading=" << shapeReadingLabel(finalMetrics)
              << " major=" << finalMetrics.shapeMajor
              << " minor=" << finalMetrics.shapeMinor
              << " elongation=" << finalMetrics.shapeElongation
              << " endpoints=" << finalMetrics.shapeEndpoints
              << " junctions=" << finalMetrics.shapeJunctions
              << " highDegree=" << finalMetrics.shapeHighDegree
              << " branches=" << finalMetrics.shapeBranchCount
              << " strongRays=" << finalMetrics.shapeStrongRays
              << " shortBranches=" << finalMetrics.shapeShortBranches
              << " clusteredBranches=" << finalMetrics.shapeClusteredBranches
              << " branchMax=" << finalMetrics.shapeBranchMaxLength
              << " branchMean=" << finalMetrics.shapeBranchMeanLength
              << " branchStraightness=" << finalMetrics.shapeBranchMeanStraightness
              << " branchBalance=" << finalMetrics.shapeBranchBalance
              << " centralLoad=" << finalMetrics.shapeCentralLoad
              << " cycles=" << finalMetrics.shapeCycleRank
              << " pcaSymmetry=" << finalMetrics.shapeSymmetry
              << "\n";
    std::cout << "  shape_signature " << shapeSignature(finalMetrics) << "\n";
    std::cout << "  shape_defects " << shapeDefects(finalMetrics) << "\n";
    std::cout << "  shape_profile " << compactShapeProfile(finalMetrics) << "\n";
    std::cout << "  branch_profile " << compactBranchProfile(finalMetrics) << "\n";
    std::cout << "  design_phase"
              << " phase=" << designPhaseLabel(finalMetrics)
              << " structural=" << finalMetrics.structuralPrototypeCells << "/" << finalMetrics.cells
              << " functional=" << finalMetrics.functionalCells << "/" << finalMetrics.cells
              << " actuators=" << finalMetrics.actuatorCells
              << " motors=" << finalMetrics.motorCells
              << " devorocytes=" << finalMetrics.devorocyteCells
              << "\n";
    std::cout << "  health"
              << " lossSamples=" << lossSamples
              << " finalCollisions=" << finalMetrics.collisions
              << " finalLethalOverlaps=" << finalMetrics.lethalOverlapPairs
              << " minGap=" << finalMetrics.minSurfaceGap
              << " lowMass=" << finalMetrics.lowMassCells
              << "\n";
    if (config.mode == "swim") {
        std::cout << "  motion"
                  << " displacement=" << finalMetrics.displacement
                  << " survived=" << (finalMetrics.cells > 0 ? "yes" : "no")
                  << " note=swim_mode_tests_motion_and_energy_not_adult_morphogenesis"
                  << "\n";
    }
    if (early != nullptr && mid != nullptr) {
        std::cout << "  stages"
                  << " earlyCells=" << early->cells << "@" << early->time
                  << " midCells=" << mid->cells << "@" << mid->time
                  << " finalCells=" << finalMetrics.cells << "@" << finalMetrics.time
                  << "\n";
    }

    std::cout << "  verdict";
    if (finalMetrics.cells == 0) {
        std::cout << " extinct_before_final_time";
    } else if (config.mode == "preview" && finalMetrics.devorocyteCells > 0) {
        std::cout << " structural_pass_contains_devorocytes";
    } else if (config.mode == "swim" && finalMetrics.displacement > 0.2 && finalMetrics.cells > 0) {
        std::cout << " moves_as_live_module";
    } else if (phenotype == "recursive_segmented_body") {
        std::cout << " recursive_body_confirmed";
    } else if (phenotype == "compact_mechanical_module") {
        std::cout << " compact_module_confirmed";
    } else if (config.mode == "preview" && rootReemitCount == 0 && finalMetrics.cells > 2) {
        std::cout << " no_reproduction_loop_detected";
    } else if (config.mode == "preview" && finalMetrics.components > 1 && rootReemitCount > 0) {
        std::cout << " reproduces_as_population_not_single_body";
    } else if (config.mode == "preview" && axisRatio(longest) >= 2.0 && connectedSamples > 0) {
        std::cout << " has_body_axis";
    } else {
        std::cout << " needs_targeted_followup";
    }
    std::cout << "\n";
}

void printWorldDump(const SimulationWorld& world,
        const std::vector<SplitEventInfo>& events,
        int eventCount) {
    const CellData& cellData = world.mCellData;
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "cell_dump time=" << world.mTime << " count=" << cellData.mCellCount << "\n";
    std::vector<int> sortedCells(cellData.mCellCount);
    for (int i = 0; i < cellData.mCellCount; i++) {
        sortedCells[i] = i;
    }
    std::sort(sortedCells.begin(), sortedCells.end(), [&](int a, int b) {
        if (cellData.mPositionY[a] == cellData.mPositionY[b]) {
            return cellData.mPositionX[a] < cellData.mPositionX[b];
        }
        return cellData.mPositionY[a] > cellData.mPositionY[b];
    });
    Bounds bounds = boundsFor(cellData);
    double centerX = (bounds.minX + bounds.maxX) * 0.5;
    double centerY = (bounds.minY + bounds.maxY) * 0.5;
    for (int i : sortedCells) {
        std::cout << "  cell"
                  << " id=" << i
                  << " gene=m" << (cellData.activeGeneIndex(i).value() + 1)
                  << " type=" << CellTypes::ordinal(cellData.mActiveCellType[i])
                  << " x=" << cellData.mPositionX[i]
                  << " y=" << cellData.mPositionY[i]
                  << " relX=" << (cellData.mPositionX[i] - centerX)
                  << " relY=" << (cellData.mPositionY[i] - centerY)
                  << " r=" << cellData.mRadius[i]
                  << " degree=" << cellData.mTopology.linkCount(i)
                  << " mass=" << cellData.mMass[i]
                  << " age=" << cellData.mAge[i]
                  << " dead=" << (cellData.mIsDead[i] ? 1 : 0)
                  << "\n";
    }

    constexpr int gridWidth = 41;
    constexpr int gridHeight = 21;
    std::array<std::string, gridHeight> grid;
    for (std::string& row : grid) {
        row.assign(gridWidth, '.');
    }
    double width = std::max(bounds.maxX - bounds.minX, CellData::MAX_CELL_RADIUS);
    double height = std::max(bounds.maxY - bounds.minY, CellData::MAX_CELL_RADIUS);
    for (int i = 0; i < cellData.mCellCount; i++) {
        int gx = static_cast<int>(std::round((cellData.mPositionX[i] - bounds.minX) / width * (gridWidth - 1)));
        int gy = static_cast<int>(std::round((bounds.maxY - cellData.mPositionY[i]) / height * (gridHeight - 1)));
        gx = std::clamp(gx, 0, gridWidth - 1);
        gy = std::clamp(gy, 0, gridHeight - 1);
        char marker = static_cast<char>('0' + std::min(9, std::max(0, static_cast<int>(cellData.activeGeneIndex(i).value()))));
        grid[gy][gx] = marker;
    }
    std::cout << "spatial_map gene_digit=m(index+1), top=maxY\n";
    for (const std::string& row : grid) {
        std::cout << "  " << row << "\n";
    }
    printShapeMap(cellData);

    CellTopology::EdgeView edgeView = cellData.mTopology.edgeView();
    std::cout << "links count=" << edgeView.count << "\n";
    for (int edge = 0; edge < edgeView.count; edge++) {
        std::cout << "  link"
                  << " edge=" << edge
                  << " a=" << edgeView.cellA[edge]
                  << " b=" << edgeView.cellB[edge]
                  << " rest=" << edgeView.restLength[edge]
                  << " stiffness=" << edgeView.torsionalStiffness[edge]
                  << "\n";
    }

    std::cout << "split_events count=" << eventCount << "\n";
    int limit = std::min(eventCount, static_cast<int>(events.size()));
    for (int i = 0; i < limit; i++) {
        const SplitEventInfo& info = events[i];
        const SimulationWorld::SplitEvent& event = info.event;
        std::cout << "  split"
                  << " index=" << i
                  << " time=" << event.splitTime
                  << " parent=" << event.parentCellIndex
                  << " child1=" << event.child1CellIndex
                  << " child2=" << event.child2CellIndex
                  << " parentGene=m" << (event.parentGeneIndex + 1)
                  << " child1Gene=m" << (event.child1GeneIndex + 1)
                  << " child2Gene=m" << (event.child2GeneIndex + 1)
                  << "\n";
    }
}

void appendNewSplitEventInfos(const SimulationWorld& world,
        const std::vector<SimulationWorld::SplitEvent>& rawEvents,
        std::vector<SplitEventInfo>& infos,
        int& capturedCount) {
    int rawCount = std::min(world.splitEventCount(), static_cast<int>(rawEvents.size()));
    while (capturedCount < rawCount) {
        const SimulationWorld::SplitEvent& event = rawEvents[capturedCount];
        SplitEventInfo info;
        info.event = event;
        infos.push_back(info);
        capturedCount++;
    }
}

void appendSample(std::vector<Metrics>& samples, const SimulationWorld& world, int splitEventCount) {
    Metrics metrics = measure(world, splitEventCount);
    if (!samples.empty()) {
        if (samples.back().cells > metrics.cells) {
            metrics.lostSincePreviousSample = samples.back().cells - metrics.cells;
        }
        double dx = metrics.centerX - samples.front().centerX;
        double dy = metrics.centerY - samples.front().centerY;
        metrics.displacement = std::sqrt(dx * dx + dy * dy);
    }
    samples.push_back(metrics);
}

Metrics runLab(const LabConfig& config,
        std::vector<Metrics>& samples,
        std::vector<SplitEventInfo>* capturedEventsOut = nullptr) {
    bool previewMode = config.mode != "swim";
    Environment environment = previewMode
            ? Environment::setupPreviewSimulationEnvironment(config.maxCells)
            : Environment::constructDefaultEnvironment(config.maxCells, 0, 3.0);
    if (!previewMode) {
        environment.addRandom = false;
        environment.dieEdge = false;
        environment.mGravity = 0.0;
        environment.syncGravityFieldFromLegacyGravity();
        environment.mDensity = 0.0;
        environment.mDensityGradient = 0.0;
        environment.syncDensityFieldFromLegacyGradient();
        environment.mViscosity = Environment::VISCOSITY_MAX * 0.2;
    }
    SimulationWorld world(previewMode, environment);
    const auto genome = createGenomeByName(config.genomeName);
    const cellularity::GenomeData runtimeGenome(genome.begin(), genome.end());
    if (!addUserCellToWorld(world, 0.0, 0.0, runtimeGenome)) {
        throw std::runtime_error("Failed to add preview root cell");
    }
    world.mEnvironment.mRandom.setSeed(config.seed);

    double maxTime = config.previewTime;
    for (double checkpointTime : config.checkpointTimes) {
        maxTime = std::max(maxTime, checkpointTime);
    }
    int maxTimeSteps = static_cast<int>(std::round(std::max(0.0, maxTime) / SimulationWorld::DELTA_TIME));
    std::vector<SimulationWorld::SplitEvent> events(std::max(1, config.maxCells * 4));
    std::vector<SplitEventInfo> eventInfos;
    int capturedEventCount = 0;
    world.beginSplitEventRecording(events.data(), static_cast<int>(events.size()));
    appendSample(samples, world, world.splitEventCount());

    std::vector<double> checkpointTimes = config.checkpointTimes;
    std::sort(checkpointTimes.begin(), checkpointTimes.end());
    size_t nextCheckpoint = 0;

    int extra = 0;
    int sampleSteps = std::max(1, config.sampleSteps);
    for (int timestepIndex = 0; timestepIndex < maxTimeSteps && extra <= 20; timestepIndex++) {
        world.update(false, 1.0);
        appendNewSplitEventInfos(world, events, eventInfos, capturedEventCount);
        while (nextCheckpoint < checkpointTimes.size()
                && world.mTime + SimulationWorld::DELTA_TIME * 0.5 >= checkpointTimes[nextCheckpoint]) {
            appendSample(samples, world, world.splitEventCount());
            nextCheckpoint++;
        }
        if (checkpointTimes.empty()
                && ((timestepIndex + 1) % sampleSteps == 0 || timestepIndex + 1 == maxTimeSteps)) {
            appendSample(samples, world, world.splitEventCount());
        }
        if (world.mCellData.mCellCount >= world.mEnvironment.maxCellCount) {
            extra++;
        }
    }

    if (samples.empty() || samples.back().time != world.mTime) {
        appendSample(samples, world, world.splitEventCount());
    }
    Metrics finalMetrics = measure(world, world.splitEventCount());
    if (!samples.empty()) {
        double dx = finalMetrics.centerX - samples.front().centerX;
        double dy = finalMetrics.centerY - samples.front().centerY;
        finalMetrics.displacement = std::sqrt(dx * dx + dy * dy);
    }
    if (!samples.empty() && samples.back().cells > finalMetrics.cells) {
        finalMetrics.lostSincePreviousSample = samples.back().cells - finalMetrics.cells;
    }
    if (config.dump && !config.json) {
        printWorldDump(world, eventInfos, static_cast<int>(eventInfos.size()));
    }
    if (capturedEventsOut != nullptr) {
        *capturedEventsOut = eventInfos;
    }
    world.endSplitEventRecording();
    return finalMetrics;
}

double parseDouble(const char* value, const std::string& label) {
    char* end = nullptr;
    double parsed = std::strtod(value, &end);
    if (end == value || *end != '\0') {
        throw std::runtime_error("Bad numeric value for " + label + ": " + value);
    }
    return parsed;
}

void exportGenomeFile(
        const std::array<GeneData, CellData::MAX_GENES_COUNT>& genome,
        const std::string& path) {
    const cellularity::GenomeData dynamicGenome(
            genome.begin(), genome.end());
    const std::vector<std::uint8_t> bytes =
            cellularity::save_format::release::encodeReleaseGenome(
                    dynamicGenome);
    std::ofstream output(
            path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
                "Failed to open Genome export path: " + path);
    }
    output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error(
                "Failed to write Genome export path: " + path);
    }
}

bool evaluateAssertions(const LabConfig& config, const Metrics& metrics, const std::vector<Metrics>& samples) {
    bool ok = true;
    auto fail = [&](const std::string& message) {
        ok = false;
        std::cerr << "ASSERTION_FAILED " << message << "\n";
    };
    if (config.minCells >= 0 && metrics.cells < config.minCells) {
        fail("min-cells expected>=" + std::to_string(config.minCells) + " actual=" + std::to_string(metrics.cells));
    }
    if (config.maxCellsAssert >= 0 && metrics.cells > config.maxCellsAssert) {
        fail("max-cells-assert expected<=" + std::to_string(config.maxCellsAssert) + " actual=" + std::to_string(metrics.cells));
    }
    if (config.maxCollisions >= 0 && metrics.collisions > config.maxCollisions) {
        fail("max-collisions expected<=" + std::to_string(config.maxCollisions) + " actual=" + std::to_string(metrics.collisions));
    }
    if (config.minEdges >= 0 && metrics.edges < config.minEdges) {
        fail("min-edges expected>=" + std::to_string(config.minEdges) + " actual=" + std::to_string(metrics.edges));
    }
    if (config.requireConnected && metrics.components != 1) {
        fail("require-connected components=" + std::to_string(metrics.components));
    }
    if (config.minWidth >= 0.0 && metrics.width < config.minWidth) {
        fail("min-width expected>=" + std::to_string(config.minWidth) + " actual=" + std::to_string(metrics.width));
    }
    if (config.minHeight >= 0.0 && metrics.height < config.minHeight) {
        fail("min-height expected>=" + std::to_string(config.minHeight) + " actual=" + std::to_string(metrics.height));
    }
    if (config.minDisplacement >= 0.0 && metrics.displacement < config.minDisplacement) {
        fail("min-displacement expected>=" + std::to_string(config.minDisplacement)
                + " actual=" + std::to_string(metrics.displacement));
    }
    if (config.minGeneIndex >= 0 && config.minGeneCount >= 0) {
        int count = config.minGeneIndex < CellData::MAX_GENES_COUNT ? metrics.geneCounts[config.minGeneIndex] : 0;
        if (count < config.minGeneCount) {
            fail("require-gene m" + std::to_string(config.minGeneIndex + 1)
                    + " expected>=" + std::to_string(config.minGeneCount)
                    + " actual=" + std::to_string(count));
        }
    }
    if (config.surviveUntil >= 0.0) {
        bool aliveAtTime = false;
        for (const Metrics& sample : samples) {
            if (sample.time + SimulationWorld::DELTA_TIME * 0.5 >= config.surviveUntil && sample.cells > 0) {
                aliveAtTime = true;
                break;
            }
        }
        if (!aliveAtTime) {
            fail("survive-until expected alive at " + std::to_string(config.surviveUntil));
        }
    }
    return ok;
}

std::vector<double> parseTimes(const std::string& value) {
    std::vector<double> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            result.push_back(parseDouble(item.c_str(), "--times"));
        }
    }
    return result;
}

int parseGeneIndex(const std::string& value) {
    if (!value.empty() && (value[0] == 'm' || value[0] == 'M')) {
        int displayIndex = std::atoi(value.c_str() + 1);
        return displayIndex - 1;
    }
    return std::atoi(value.c_str());
}

void applyRunPreset(LabConfig& config, const std::string& preset) {
    config.analysis = true;
    if (preset == "shape") {
        config.mode = "preview";
        config.previewTime = 120.0;
        config.checkpointTimes = {1.2, 3.0, 10.0, 40.0, 80.0, 120.0};
        config.maxCells = std::max(config.maxCells, 384);
    } else if (preset == "quick") {
        config.mode = "preview";
        config.previewTime = 8.0;
        config.checkpointTimes = {0.6, 1.2, 2.0, 4.0, 8.0};
        config.maxCells = std::max(config.maxCells, 96);
    } else if (preset == "adult") {
        config.mode = "preview";
        config.previewTime = 120.0;
        config.checkpointTimes = {3.0, 10.0, 40.0, 80.0, 120.0};
        config.maxCells = std::max(config.maxCells, 256);
    } else if (preset == "long") {
        config.mode = "preview";
        config.previewTime = 240.0;
        config.checkpointTimes = {3.0, 10.0, 40.0, 80.0, 120.0, 180.0, 240.0};
        config.maxCells = std::max(config.maxCells, 384);
    } else if (preset == "swim") {
        config.mode = "swim";
        config.previewTime = 8.0;
        config.checkpointTimes = {1.0, 2.0, 4.0, 6.0, 8.0};
        config.maxCells = std::max(config.maxCells, 96);
    } else {
        throw std::runtime_error("Unknown --preset: " + preset + " (expected shape, quick, adult, long, or swim)");
    }
}

LabConfig parseArgs(int argc, char** argv) {
    LabConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto readInt = [&](int& target) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + arg);
            }
            target = std::atoi(argv[++i]);
        };
        auto readDouble = [&](double& target) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + arg);
            }
            target = parseDouble(argv[++i], arg);
        };
        if (arg == "--json") {
            config.json = true;
        } else if (arg == "--analysis") {
            config.analysis = true;
        } else if (arg == "--dump") {
            config.dump = true;
        } else if (arg == "--require-connected") {
            config.requireConnected = true;
        } else if (arg == "--genome") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --genome");
            }
            config.genomeName = argv[++i];
        } else if (arg == "--mode") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --mode");
            }
            config.mode = argv[++i];
            if (config.mode != "preview" && config.mode != "swim") {
                throw std::runtime_error("Expected --mode preview or --mode swim");
            }
        } else if (arg == "--export-genome") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --export-genome");
            }
            config.exportGenomePath = argv[++i];
        } else if (arg == "--preset") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --preset");
            }
            applyRunPreset(config, argv[++i]);
        } else if (arg == "--max-cells") {
            readInt(config.maxCells);
        } else if (arg == "--min-cells") {
            readInt(config.minCells);
        } else if (arg == "--max-cells-assert") {
            readInt(config.maxCellsAssert);
        } else if (arg == "--max-collisions") {
            readInt(config.maxCollisions);
        } else if (arg == "--min-edges") {
            readInt(config.minEdges);
        } else if (arg == "--sample-steps") {
            readInt(config.sampleSteps);
        } else if (arg == "--time") {
            readDouble(config.previewTime);
        } else if (arg == "--times") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --times");
            }
            config.checkpointTimes = parseTimes(argv[++i]);
        } else if (arg == "--min-width") {
            readDouble(config.minWidth);
        } else if (arg == "--min-height") {
            readDouble(config.minHeight);
        } else if (arg == "--min-displacement") {
            readDouble(config.minDisplacement);
        } else if (arg == "--survive-until") {
            readDouble(config.surviveUntil);
        } else if (arg == "--require-gene") {
            if (i + 2 >= argc) {
                throw std::runtime_error("Expected --require-gene mN COUNT");
            }
            config.minGeneIndex = parseGeneIndex(argv[++i]);
            config.minGeneCount = std::atoi(argv[++i]);
        } else if (arg == "--seed") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --seed");
            }
            config.seed = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--help") {
            std::cout << "Usage: genome_development_lab [--json] [--analysis] [--dump] [--genome NAME] [--mode preview|swim] [--preset shape|quick|adult|long|swim] [--export-genome PATH]\n"
                      << "                              [--time SECONDS] [--times A,B,C] [--sample-steps N] [--max-cells N] [--seed N]\n"
                      << "                              [--min-cells N] [--max-cells-assert N] [--max-collisions N] [--min-edges N]\n"
                      << "                              [--min-width X] [--min-height Y] [--min-displacement D] [--require-connected]\n"
                      << "                              [--require-gene mN COUNT] [--survive-until SECONDS]\n"
                      << "Available genomes: " << availableGenomeNames() << "\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        LabConfig config = parseArgs(argc, argv);
        if (!config.exportGenomePath.empty()) {
            exportGenomeFile(createGenomeByName(config.genomeName), config.exportGenomePath);
            std::cout << "exported_genome path=" << config.exportGenomePath
                      << " format=CLTY schema="
                      << cellularity::save_format::release::
                                 kGenomeContentSchemaMajor
                      << "\n";
        }
        std::vector<Metrics> samples;
        std::vector<SplitEventInfo> events;
        Metrics metrics = runLab(config, samples, &events);
        if (config.json) {
            printJsonReport(metrics, samples);
        } else {
            printTextReport(metrics, samples);
            if (config.analysis) {
                printAnalysisReport(config, metrics, samples, events);
            }
        }
        bool assertionsOk = evaluateAssertions(config, metrics, samples);
        return assertionsOk ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "genome_development_lab: " << error.what() << "\n";
        return 1;
    }
}
