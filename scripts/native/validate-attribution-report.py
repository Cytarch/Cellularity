#!/usr/bin/env python3
"""Validate coarse native attribution reports and their counter invariants."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


class ValidationError(RuntimeError):
    pass


def read_report(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as source:
            value = json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"Could not read {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValidationError(f"Expected a JSON object in {path}")
    return value


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate_attribution(
    name: str, value: Any, errors: list[str]
) -> None:
    require(isinstance(value, dict), f"{name}: missing attribution object", errors)
    if not isinstance(value, dict):
        return
    if value.get("available") is False:
        require(bool(value.get("reason")), f"{name}: unavailable without reason", errors)
        return
    require(value.get("available") is True, f"{name}: availability is ambiguous", errors)
    require(
        value.get("timerGranularity") == "coarse-region",
        f"{name}: timer granularity is not coarse-region",
        errors,
    )
    require("residualNanos" in value, f"{name}: residual is not explicit", errors)
    require(
        value.get("withinAcceptance") is True,
        f"{name}: child phases do not explain the parent within budget",
        errors,
    )
    parent = value.get("parentTotalNanos")
    children = value.get("measuredChildTotalNanos")
    residual = value.get("residualNanos")
    if all(isinstance(item, int) for item in (parent, children, residual)):
        require(
            parent == children + residual,
            f"{name}: parent != measured children + residual",
            errors,
        )


def validate_stage_report(report: dict[str, Any], errors: list[str]) -> None:
    schema = report.get("schemaVersion")
    require(schema in (3, 4), "stage: unexpected schema", errors)
    attribution = report.get("attribution")
    require(isinstance(attribution, dict), "stage: missing attribution", errors)
    if isinstance(attribution, dict):
        for name in (
            "updateGrids",
            "eatFoods",
            "overlaps",
            "cellCellCollisions",
            "signals",
        ):
            validate_attribution(f"stage.{name}", attribution.get(name), errors)

        worker = attribution.get("workerExecution")
        require(isinstance(worker, dict), "stage.workerExecution: missing", errors)
        if isinstance(worker, dict) and worker.get("available") is False:
            require(
                worker.get("reason") == "no-production-simulation-worker-pool",
                "stage.workerExecution: unavailable reason is not explicit",
                errors,
            )
            for field in (
                "dispatchNanos",
                "usefulWorkNanos",
                "barrierNanos",
                "reductionNanos",
                "autotuneNanos",
                "warmupNanos",
                "dispatches",
                "autotuneRuns",
                "warmupRuns",
            ):
                require(
                    worker.get(field) == 0,
                    f"stage.workerExecution.{field}: unavailable metric is non-zero",
                    errors,
                )

    collision = report.get("collisionIntentCounters", {})
    if isinstance(collision, dict):
        collected = collision.get("intentsCollected")
        applied = collision.get("intentsApplied")
        geometry = collision.get("geometryEvaluations")
        forces = collision.get("ordinaryForceApplications")
        require(
            collected == applied == geometry == forces,
            "stage.collisions: collect/apply/geometry/force counts differ",
            errors,
        )
        require(
            collision.get("typedIntentsCollected")
            == collision.get("typedIntentsCommitted"),
            "stage.collisions: typed collect/commit counts differ",
            errors,
        )
        require(
            collision.get("applyPhaseIsOrderedForceAndTypedCommit") is True,
            "stage.collisions: ordered force/typed commit contract missing",
            errors,
        )

    overlap = report.get("overlapCounters", {})
    if isinstance(overlap, dict):
        semantic = overlap.get("avgSemanticPairsPerStep")
        attempts = overlap.get("avgEndpointAdmissionAttemptsPerStep")
        directed = overlap.get("avgDirectedAdmissionsPerStep")
        capacity_skips = overlap.get("avgEndpointCapacitySkipsPerStep")
        if all(isinstance(item, (int, float)) for item in (semantic, attempts)):
            require(
                attempts <= semantic * 2.0,
                "stage.overlaps: endpoint attempts exceed two per semantic pair",
                errors,
            )
        if all(
            isinstance(item, (int, float))
            for item in (attempts, directed, capacity_skips)
        ):
            require(
                abs(attempts - directed - capacity_skips) < 1.0e-9,
                "stage.overlaps: attempts are not admissions + capacity skips",
                errors,
            )

    capabilities = report.get("capabilityStatus", {})
    if isinstance(capabilities, dict):
        pair_stream = capabilities.get("pairStream", {})
        if isinstance(pair_stream, dict):
            require(
                pair_stream.get("mode") == "fused-no-materialized-stream",
                "stage.pairStream: current fused mode is not reported",
                errors,
            )
            require(pair_stream.get("writes") == 0, "stage.pairStream: unexpected writes", errors)
            require(
                pair_stream.get("rawBudgetAvailable") is False,
                "stage.pairStream: raw budget should be explicitly unavailable",
                errors,
            )
        for capability in (
            "foodIndexGeneration",
            "foodPostCaptureRefresh",
            "foodStaleLinearCaptureFallback",
        ):
            value = capabilities.get(capability)
            require(isinstance(value, dict), f"stage.{capability}: missing", errors)
            if isinstance(value, dict) and value.get("available") is False:
                require(bool(value.get("reason")), f"stage.{capability}: missing reason", errors)

    if schema == 4:
        spatial = report.get("spatialAttribution")
        require(isinstance(spatial, dict), "stage.spatialAttribution: missing", errors)
        if isinstance(spatial, dict):
            for name in ("gridClear", "cellSpatialBuild", "foodRenderIndex"):
                validate_attribution(
                    f"stage.spatialAttribution.{name}", spatial.get(name), errors
                )
            model = spatial.get("measurementModel")
            require(isinstance(model, dict), "stage.spatial measurement model missing", errors)
            if isinstance(model, dict):
                require(
                    model.get("productionCellSpatialPath") == "single-fused-cell-loop",
                    "stage.spatial production path is not explicit",
                    errors,
                )
                require(
                    model.get("profiledCellSpatialPath")
                    == "order-preserving-profile-only-phase-split",
                    "stage.spatial profile decomposition model is not explicit",
                    errors,
                )
                require(
                    model.get("profileOffPathChanged") is False,
                    "stage.spatial claims a Profile-OFF path change",
                    errors,
                )
                require(
                    model.get("perCellClocks") is False,
                    "stage.spatial uses per-Cell clocks",
                    errors,
                )

        counters = report.get("spatialIndexCounters")
        require(isinstance(counters, dict), "stage.spatialIndexCounters: missing", errors)
        if isinstance(counters, dict):
            require(
                counters.get("cellIndexAttempts")
                == counters.get("cellIndexAdmissions", 0)
                + counters.get("cellIndexCapacitySkips", 0),
                "stage.spatial: physical admissions + skips != attempts",
                errors,
            )
            require(
                counters.get("cellRenderOccupancyMarks")
                == counters.get("cellIndexAdmissions", 0)
                + counters.get("cellRenderOverflowAdmissions", 0),
                "stage.spatial: render occupancy marks != physical + overflow admissions",
                errors,
            )
            require(
                counters.get("cellPositionConversions", 0)
                >= counters.get("cellIndexAttempts", 0),
                "stage.spatial: fewer position conversions than live insertion attempts",
                errors,
            )
            require(
                counters.get("foodRenderMappingVisits")
                == counters.get("foodRenderMappingCacheHits", 0)
                + counters.get("foodRenderMappingCacheMisses", 0),
                "stage.spatial: food render mapping visits != hits + misses",
                errors,
            )
            require(
                counters.get("foodRenderMappingVisits")
                == counters.get("foodRenderInsertions", 0)
                + counters.get("foodRenderOutOfBoundsSkips", 0),
                "stage.spatial: food render visits != insertions + out-of-bounds",
                errors,
            )
            require(
                counters.get("foodMappingVisits")
                == counters.get("foodCollisionRefreshVisits", 0)
                + counters.get("foodRenderMappingVisits", 0),
                "stage.spatial: aggregate food mapping visits have an unknown source",
                errors,
            )
            require(
                counters.get("foodMappingCacheHits")
                == counters.get("foodRenderMappingCacheHits"),
                "stage.spatial: aggregate food mapping hits differ from render hits",
                errors,
            )
            require(
                counters.get("foodMappingCacheMisses")
                == counters.get("foodRenderMappingCacheMisses"),
                "stage.spatial: aggregate food mapping misses differ from render misses",
                errors,
            )
            require(
                counters.get("foodMappingWrites")
                == counters.get("foodCollisionRefreshVisits", 0)
                + counters.get("foodRenderMappingWrites", 0),
                "stage.spatial: aggregate food mapping writes have an unknown source",
                errors,
            )


def validate_overlap_report(report: dict[str, Any], errors: list[str]) -> None:
    require(report.get("schemaVersion") == 3, "overlap: unexpected schema", errors)
    require(report.get("innerPairTimers") is False, "overlap: inner pair timers enabled", errors)
    require(
        report.get("timerGranularity") == "coarse-region",
        "overlap: timer granularity is not coarse-region",
        errors,
    )
    overlap = report.get("overlap", {})
    require(
        isinstance(overlap, dict) and overlap.get("withinAcceptance") is True,
        "overlap: child phases do not explain parent within budget",
        errors,
    )
    if isinstance(overlap, dict):
        parent = overlap.get("parent", {})
        phases = overlap.get("phases", {})
        residual = overlap.get("residualNanos")
        if isinstance(parent, dict) and isinstance(phases, dict) and isinstance(residual, int):
            parent_nanos = parent.get("totalNanos")
            child_nanos = sum(
                value.get("totalNanos", 0)
                for value in phases.values()
                if isinstance(value, dict)
            )
            require(
                parent_nanos == child_nanos + residual,
                "overlap: parent != phases + residual",
                errors,
            )

    counters = report.get("counters", {})
    if isinstance(counters, dict):
        require(
            counters.get("endpointAdmissionAttempts")
            == counters.get("directedAdmissions") + counters.get("endpointCapacitySkips"),
            "overlap: admission attempts are not admissions + capacity skips",
            errors,
        )
        require(
            counters.get("endpointAdmissionAttempts") <= counters.get("semanticPairs") * 2,
            "overlap: endpoint attempts exceed two per semantic pair",
            errors,
        )
        require(counters.get("pairStreamWrites") == 0, "overlap: unexpected pair stream writes", errors)


def validate_render_report(report: dict[str, Any], errors: list[str]) -> None:
    require(report.get("schemaVersion") == 6, "render: unexpected schema", errors)
    attribution = report.get("attribution", {})
    if isinstance(attribution, dict):
        validate_attribution("render.copyToDraw", attribution.get("copyToDraw"), errors)
        validate_attribution("render.sourceCapture", attribution.get("sourceCapture"), errors)
    else:
        errors.append("render: missing attribution")

    steps = report.get("profileSteps")
    counters = report.get("counters", {})
    if isinstance(steps, int) and isinstance(counters, dict):
        require(
            counters.get("freshPhysicsContactCaptures", 0)
            + counters.get("staleGeometryContactFallbacks", 0)
            == steps,
            "render: fresh + stale contact captures != profile steps",
            errors,
        )
        require(
            counters.get("foodGridCaptureCalls", 0)
            + counters.get("foodLinearCaptureCalls", 0)
            == steps,
            "render: grid + linear food captures != profile steps",
            errors,
        )


def validate_typed_report(
    path: Path, report: dict[str, Any], errors: list[str]
) -> None:
    workload = report.get("workload")
    require(isinstance(workload, str), f"{path.name}: missing workload", errors)
    if not isinstance(workload, str):
        return
    expected_actor = workload.removeprefix("typed_")
    collision = report.get("collisionIntentCounters", {})
    presence = collision.get("typedActorPresence", {}) if isinstance(collision, dict) else {}
    expected = presence.get(expected_actor, {}) if isinstance(presence, dict) else {}
    require(
        isinstance(expected, dict) and expected.get("present") is True,
        f"{path.name}: expected actor {expected_actor} is absent",
        errors,
    )
    if isinstance(expected, dict):
        require(
            isinstance(expected.get("collected"), int) and expected.get("collected", 0) > 0,
            f"{path.name}: no typed intents collected for {expected_actor}",
            errors,
        )
        require(
            expected.get("collected") == expected.get("committed"),
            f"{path.name}: typed collect/commit mismatch for {expected_actor}",
            errors,
        )
    if isinstance(presence, dict):
        unexpected = [
            actor
            for actor, value in presence.items()
            if actor != expected_actor
            and isinstance(value, dict)
            and value.get("present") is True
        ]
        require(not unexpected, f"{path.name}: unexpected typed actors {unexpected}", errors)
    if isinstance(collision, dict):
        require(
            collision.get("intentsCollected")
            == collision.get("intentsApplied")
            == collision.get("geometryEvaluations")
            == collision.get("ordinaryForceApplications"),
            f"{path.name}: collision counter mismatch",
            errors,
        )
    attribution = report.get("attribution", {})
    if isinstance(attribution, dict):
        validate_attribution(
            f"{path.name}.cellCellCollisions",
            attribution.get("cellCellCollisions"),
            errors,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-report", type=Path, required=True)
    parser.add_argument("--overlap-report", type=Path, required=True)
    parser.add_argument("--render-report", type=Path, required=True)
    parser.add_argument("--typed-report", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    errors: list[str] = []
    try:
        stage = read_report(arguments.stage_report)
        overlap = read_report(arguments.overlap_report)
        render = read_report(arguments.render_report)
        typed = [(path, read_report(path)) for path in arguments.typed_report]
    except ValidationError as error:
        print(f"attribution validation failed: {error}", file=sys.stderr)
        return 2

    validate_stage_report(stage, errors)
    validate_overlap_report(overlap, errors)
    validate_render_report(render, errors)
    for path, report in typed:
        validate_typed_report(path, report, errors)

    result = {
        "schemaVersion": 1,
        "reports": {
            "stage": str(arguments.stage_report.resolve()),
            "overlap": str(arguments.overlap_report.resolve()),
            "render": str(arguments.render_report.resolve()),
            "typed": [str(path.resolve()) for path in arguments.typed_report],
        },
        "errors": errors,
        "passed": not errors,
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
