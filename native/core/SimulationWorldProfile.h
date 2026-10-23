#pragma once

#include <array>

struct SimulationWorldProfile {
    enum Stage {
        TIME_LIGHT = 0,
        RANDOM_FOOD_CELLS,
        KINEMATICS,
        SPLIT,
        RESET_FORCES,
        FLAGELLOCYTES,
        REMOVE_DEAD,
        UPDATE_GRIDS,
        EAT_FOODS,
        DEAD_SPLASHES,
        OVERLAPS,
        FERTILIZATION,
        CELL_CELL_COLLISIONS,
        LINK_INTERACTIONS,
        WALL_COLLISIONS,
        SMELL_INTERACTIONS,
        CELL_FORCES,
        VELOCITIES,
        CILIOCYTES,
        METABOLISM,
        APPLY_METABOLISM,
        SIGNALS,
        STAGE_COUNT
    };

    std::array<long long, STAGE_COUNT> stageNanos{};

    long long gridCacheInvalidationNanos = 0;
    long long gridClearNanos = 0;
    long long gridCellCollisionClearNanos = 0;
    long long gridSmellCollisionClearNanos = 0;
    long long gridRenderOverflowClearNanos = 0;
    long long gridRenderOccupancyClearNanos = 0;
    long long gridConsumerDiscoveryNanos = 0;
    long long gridFoodSmellMaskNanos = 0;
    long long gridCellAndSmellBuildNanos = 0;
    long long gridCellPositionConversionNanos = 0;
    long long gridSmellCellInsertionNanos = 0;
    long long gridPhysicalCellInsertionNanos = 0;
    long long gridRenderOccupancyNanos = 0;
    long long gridRenderOverflowNanos = 0;
    long long gridFoodMappingRefreshNanos = 0;
    long long gridFoodRenderBuildNanos = 0;
    long long foodRenderIndexBuildNanos = 0;
    long long foodRenderHeadClearNanos = 0;
    long long foodRenderMappingNanos = 0;
    long long foodRenderInsertionNanos = 0;
    long long gridCellTouchedBucketsCleared = 0;
    long long gridSmellTouchedBucketsCleared = 0;
    long long gridRenderOverflowHeadsCleared = 0;
    long long gridRenderOccupancyWordsCleared = 0;
    long long gridCellPositionConversions = 0;
    long long gridCellIndexAttempts = 0;
    long long gridCellIndexAdmissions = 0;
    long long gridCellIndexCapacitySkips = 0;
    long long gridCellRenderOccupancyMarks = 0;
    long long gridCellRenderOverflowAdmissions = 0;
    long long gridConsumerCellsVisited = 0;
    long long gridAnySmellConsumerMatches = 0;
    long long gridSmellGridConsumerMatches = 0;
    long long gridFoodSmellConsumerMatches = 0;
    long long gridFoodPhysicsConsumerMatches = 0;
    long long gridFoodSmellMaskSlotsCleared = 0;
    long long gridFoodSmellMaskCellsVisited = 0;
    long long gridFoodSmellMaskConsumerCells = 0;
    long long gridFoodSmellMaskWrites = 0;
    long long gridSmellCellAttempts = 0;
    long long gridSmellCellAdmissions = 0;
    long long gridSmellCellOutOfBoundsSkips = 0;
    long long gridSmellCellCapacitySkips = 0;
    long long gridSmellSecretionAdmissions = 0;
    long long gridFoodCollisionRefreshCalls = 0;
    long long gridFoodCollisionRefreshVisits = 0;
    long long foodIndexMappingVisits = 0;
    long long foodIndexMappingCacheHits = 0;
    long long foodIndexMappingCacheMisses = 0;
    long long foodIndexMappingWrites = 0;
    long long foodRenderIndexBuildCalls = 0;
    long long foodRenderHeadSlotsCleared = 0;
    long long foodRenderMappingVisits = 0;
    long long foodRenderMappingCacheHits = 0;
    long long foodRenderMappingCacheMisses = 0;
    long long foodRenderMappingWrites = 0;
    long long foodRenderInsertions = 0;
    long long foodRenderOutOfBoundsSkips = 0;
    long long foodSpatialGenerationHits = 0;
    long long foodSpatialGenerationMisses = 0;
    long long foodSpatialCaptureEnsureCalls = 0;

    long long targetCells = 0;
    long long skippedDeadTargets = 0;
    long long readyForFertilizationTargets = 0;
    long long skippedSelfOrDead = 0;
    long long potentialChecks = 0;
    long long skippedNoContact = 0;
    long long collisionWrites = 0;
    long long collisionWriteCapacitySkips = 0;
    long long overlapDeathIntents = 0;
    long long fertilizationTargets = 0;
    long long earlyStops = 0;
    long long deathsApplied = 0;
    long long cleanupCollisionReads = 0;
    long long cleanupCollisionWrites = 0;
    long long overlapSetupNanos = 0;
    long long overlapBroadphaseBuildNanos = 0;
    long long overlapCompactProjectionNanos = 0;
    long long overlapBroadphaseScanNanos = 0;
    long long overlapPairEffectsNanos = 0;
    long long overlapDeathCleanupNanos = 0;
    long long overlapPairBufferWrites = 0;
    long long overlapSemanticPairs = 0;
    long long overlapEndpointAdmissionAttempts = 0;
    long long overlapRawBudgetOverflows = 0;
    long long overlapCandidatePairs = 0;
    long long overlapSameRowCandidatePairs = 0;
    long long overlapNextRowCandidatePairs = 0;
    long long overlapSameRowCandidateRanges = 0;
    long long overlapNextRowCandidateRanges = 0;
    long long overlapExactPairs = 0;
    long long overlapScalarChecks = 0;
    long long overlapSimdChecks = 0;
    long long overlapRowsReused = 0;
    long long overlapRowsRelayouts = 0;
    long long overlapRowChangedCells = 0;
    long long overlapLayoutRebuilds = 0;
    long long overlapInsertionSortRows = 0;
    long long overlapFullSortRows = 0;
    long long overlapInsertionMoves = 0;
    long long overlapCompactClassifyNanos = 0;
    long long overlapCompactLayoutNanos = 0;
    long long overlapCompactScatterNanos = 0;
    long long overlapCompactMaterializeNanos = 0;
    long long overlapCompactSortNanos = 0;
    long long overlapCompactFinalizeNanos = 0;
    long long overlapCompactPayloadRefreshes = 0;
    long long overlapCompactScatterIdWrites = 0;
    long long overlapCompactRecordMaterializations = 0;
    long long overlapCompactRecordSortWrites = 0;
    long long overlapCompactFullSortInputRecords = 0;

    long long collisionIntentCollectNanos = 0;
    long long collisionIntentApplyNanos = 0;
    long long collisionIntentCollected = 0;
    long long collisionIntentApplied = 0;
    long long collisionGeometryEvaluations = 0;
    long long collisionOrdinaryForceApplications = 0;
    long long collisionTypedIntentCollected = 0;
    long long collisionTypedIntentCommitted = 0;
    long long collisionBiologicalIntentDispatched = 0;
    long long collisionCiliocyteIntentDispatched = 0;
    std::array<long long, 18> collisionTypedIntentCollectedByActorType{};
    std::array<long long, 18> collisionTypedIntentCommittedByActorType{};

    long long linkAngleCacheNanos = 0;
    long long linkTopologyCountScanNanos = 0;
    long long linkBoundaryLoopNanos = 0;
    long long linkNormalLoopNanos = 0;
    long long linkTargetCells = 0;
    long long linkCellsWithLinks = 0;
    long long linkTotalLinks = 0;
    long long linkTopologyLinkCountQueries = 0;
    long long linkBoundaryLinks = 0;
    long long linkNormalLinks = 0;
    long long linkRenderOffsetWrites = 0;
    long long linkTargetMyocyteBranches = 0;
    long long linkLinkedMyocyteBranches = 0;
    long long linkRemovedBoundaryLinks = 0;
    long long linkRemovedNormalLinks = 0;

    long long cellForcesVisitedCells = 0;
    long long cellForcesLiveCells = 0;
    long long cellForcesDeadSkips = 0;
    long long cellForcesBuoyocytes = 0;
    long long cellForcesPreviewSkips = 0;

    std::array<long long, 18> metabolismTypeVisits{};
    long long metabolismVisitedCells = 0;
    long long metabolismNitrateUptakeCells = 0;
    long long metabolismPreviewGrowthCells = 0;
    long long metabolismFlagellocytes = 0;
    long long metabolismStemocytes = 0;
    long long metabolismStemSwitches = 0;
    long long metabolismMyocytes = 0;
    long long metabolismNitrocytes = 0;
    long long metabolismPhotocytes = 0;
    long long metabolismCytoplasmViscosityCells = 0;
    long long metabolismGlueocytes = 0;
    long long metabolismCiliocytes = 0;
    long long metabolismCiliocyteAnimationWraps = 0;
    long long metabolismSecrocytes = 0;
    long long metabolismSecrocyteNitrateDrainCells = 0;
    long long metabolismSalinitySamples = 0;
    long long metabolismAgingDeaths = 0;
    long long metabolismMutationPeriodHits = 0;
    long long metabolismMutationRollHits = 0;
    long long metabolismMutationAttempts = 0;
    long long metabolismMutationsRecorded = 0;
    long long metabolismLinkIterations = 0;
    long long metabolismLinkPairsProcessed = 0;
    long long metabolismBoundaryLinkSkips = 0;
    long long metabolismTopologyEdgeLoopNanos = 0;
    long long metabolismTopologyEdgeReads = 0;
    long long metabolismLipidCells = 0;
    long long metabolismLipidStoreEvents = 0;
    long long metabolismLipidMoveEvents = 0;
    long long metabolismMassClampEvents = 0;
    long long metabolismNitrateClampEvents = 0;
    long long metabolismNitrateDeaths = 0;

    long long eatFoodVisits = 0;
    long long eatFoodDecayRemovals = 0;
    long long eatFoodGridIndexCacheHits = 0;
    long long eatFoodGridIndexCacheMisses = 0;
    long long eatFoodNeighborGridVisits = 0;
    long long eatFoodNeighborGridEntries = 0;
    long long eatFoodCandidateChecks = 0;
    long long eatFoodContactChecks = 0;
    long long eatFoodMobileInnerPushes = 0;
    long long eatFoodCiliocyteContacts = 0;
    long long eatFoodSecrocyteContacts = 0;
    long long eatFoodPhagocyteContacts = 0;
    long long eatFoodPhagocyteEatEvents = 0;
    long long eatFoodConsumedRemovals = 0;
    long long eatFoodMovedFoods = 0;
    long long eatFoodBoundaryRemovals = 0;
    long long eatFoodBoundaryReflections = 0;
    long long eatFoodSmellGridAttempts = 0;
    long long eatFoodSmellGridMaskSkips = 0;
    long long eatFoodSmellGridCapacitySkips = 0;
    long long eatFoodSmellGridAdds = 0;
    long long eatFoodDecayNanos = 0;
    long long eatFoodPreQueryIndexBuildNanos = 0;
    long long eatFoodSmellIndexBuildNanos = 0;
    long long eatFoodQueryAndUpdateNanos = 0;
    long long eatFoodRemovalNanos = 0;
    long long eatFoodMotionNanos = 0;
    long long eatFoodPostUpdateIndexBuildNanos = 0;
    double foodMassSpawned = 0.0;
    double foodMassSpawnedFromCorpses = 0.0;
    double foodMassConsumedByCells = 0.0;
    double foodMassDecayed = 0.0;
    double foodMassRemovedAtBoundary = 0.0;

    long long signalTopologyLinkCountQueries = 0;
    long long signalTopologyAdjacentCellQueries = 0;
    long long signalTopologyEdgeIterations = 0;
    long long signalTopologyEndpointUpdates = 0;
    long long signalLinkIterations = 0;
    long long signalBoundaryLinkSkips = 0;
    long long signalLipocyteLinkSkips = 0;
    long long signalFeatureScanNanos = 0;
    long long signalCellPhaseNanos = 0;
    long long signalEdgePhaseNanos = 0;
    long long signalClampPhaseNanos = 0;
    long long signalAdjacencyBuildNanos = 0;
    long long signalFusedCellPhaseNanos = 0;
    long long signalCellPhaseVisits = 0;
    long long signalNeurocyteEvaluations = 0;
    long long signalClampEvaluations = 0;
    long long signalClampLowWrites = 0;
    long long signalClampHighWrites = 0;
    long long signalAdjacencyCacheHits = 0;
    long long signalAdjacencyCacheRebuilds = 0;
    long long signalCsrReferenceVisits = 0;

    // Reserved so future worker-backed stages can keep one stable report
    // schema. These stay at zero while the production simulation has no
    // persistent worker pool or reduction backend.
    long long workerWarmupNanos = 0;
    long long workerAutotuneNanos = 0;
    long long workerDispatchNanos = 0;
    long long workerUsefulWorkNanos = 0;
    long long workerBarrierNanos = 0;
    long long workerReductionNanos = 0;
    long long workerWarmupRuns = 0;
    long long workerAutotuneRuns = 0;
    long long workerDispatches = 0;

    void clear() {
        *this = SimulationWorldProfile{};
    }
};
