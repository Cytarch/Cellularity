#include "app/genome_preview/GenomeFilePreviewSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "app/NativeWorldEditing.h"
#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"

namespace cellularity::genome_preview {
namespace {

constexpr double kFramingScale = 1.12;
const double kFramingAbsoluteMargin =
        CellData::MAX_CELL_RADIUS * 0.1;
const double kMinimumHalfExtent = CellData::MIN_CELL_RADIUS * 1.5;

[[nodiscard]] bool hasLivingOrganism(
        const CellData& cells,
        OrganismId organismId) {
    for (int cellIndex = 0; cellIndex < cells.mCellCount; ++cellIndex) {
        if (!cells.mIsDead[static_cast<std::size_t>(cellIndex)]
                && cells.organismId(cellIndex) == organismId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasDetachedDescendant(
        const CellData& cells,
        OrganismId founderOrganismId) {
    for (int cellIndex = 0; cellIndex < cells.mCellCount; ++cellIndex) {
        if (cells.mIsDead[static_cast<std::size_t>(cellIndex)]) {
            continue;
        }
        const OrganismId organismId = cells.organismId(cellIndex);
        if (organismId != kUnknownOrganismId
                && organismId != founderOrganismId
                && cells.parentOrganismId(cellIndex) == founderOrganismId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] OrganismId selectRetainedOrganism(
        const CellData& cells,
        OrganismId founderOrganismId) {
    if (hasLivingOrganism(cells, founderOrganismId)) {
        return founderOrganismId;
    }
    OrganismId firstLiving = kUnknownOrganismId;
    for (int cellIndex = 0; cellIndex < cells.mCellCount; ++cellIndex) {
        if (cells.mIsDead[static_cast<std::size_t>(cellIndex)]) {
            continue;
        }
        const OrganismId candidate = cells.organismId(cellIndex);
        if (isKnownOrganismId(candidate)
                && (firstLiving == kUnknownOrganismId
                        || candidate < firstLiving)) {
            firstLiving = candidate;
        }
    }
    return firstLiving;
}

void retainOneOrganism(SimulationWorld& world, OrganismId organismId) {
    CellData& cells = world.mCellData;
    for (int cellIndex = cells.mCellCount - 1; cellIndex >= 0; --cellIndex) {
        if (cells.mIsDead[static_cast<std::size_t>(cellIndex)]
                || cells.organismId(cellIndex) != organismId) {
            world.removeCellImmediately(cellIndex);
        }
    }
    world.mFoodData.resizeActiveFoodCount(0);
    world.mDeadSplashData.resizeActiveDeadSplashCount(0);
    world.invalidateCellContactCache();
}

[[nodiscard]] int maximumUpdateCount(double maximumSimulationTime) {
    if (!std::isfinite(maximumSimulationTime)
            || maximumSimulationTime < 0.0) {
        throw std::invalid_argument(
                "Genome file preview time limit must be finite and nonnegative");
    }
    const double updates =
            std::floor(maximumSimulationTime / SimulationWorld::DELTA_TIME);
    if (updates >= static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(
                "Genome file preview time limit is too large");
    }
    return static_cast<int>(updates);
}

} // namespace

DevelopedGenomePreview developGenomeForFilePreview(
        const GenomeData& genome,
        int maximumCellCount,
        double maximumSimulationTime) {
    if (maximumCellCount <= 0) {
        throw std::invalid_argument(
                "Genome file preview Cell limit must be positive");
    }

    Environment environment =
            Environment::setupPreviewSimulationEnvironment(maximumCellCount);
    auto world = std::make_unique<SimulationWorld>(true, environment);
    world->mEnvironment.mRandom.setSeed(1337);
    if (!addUserCellToWorld(*world, 0.0, 0.0, genome)) {
        throw std::invalid_argument(
                "Genome file preview could not create its founder Cell");
    }

    const OrganismId founderOrganismId = world->mCellData.organismId(0);
    if (!isKnownOrganismId(founderOrganismId)) {
        throw std::logic_error(
                "Genome file preview founder has no Organism identity");
    }

    DevelopedGenomePreview result;
    result.stopReason = DevelopmentStopReason::TimeLimitReached;
    const int updateLimit = maximumUpdateCount(maximumSimulationTime);
    for (int updateIndex = 0; updateIndex < updateLimit; ++updateIndex) {
        if (world->mCellData.mCellCount >= maximumCellCount) {
            result.stopReason = DevelopmentStopReason::CellLimitReached;
            break;
        }

        world->update(false, 1.0);
        ++result.completedUpdates;

        if (hasDetachedDescendant(world->mCellData, founderOrganismId)) {
            result.stopReason = DevelopmentStopReason::OffspringProduced;
            break;
        }
        if (!hasLivingOrganism(world->mCellData, founderOrganismId)) {
            result.stopReason = DevelopmentStopReason::OrganismExtinct;
            break;
        }
        if (world->mCellData.mCellCount >= maximumCellCount) {
            result.stopReason = DevelopmentStopReason::CellLimitReached;
            break;
        }
    }

    const OrganismId retainedOrganism =
            selectRetainedOrganism(world->mCellData, founderOrganismId);
    retainOneOrganism(*world, retainedOrganism);
    if (world->mCellData.mCellCount > 0) {
        // Development updates deliberately skip render-only work. Refresh the
        // production link attachment offsets without advancing time or
        // allowing another division after the selected stop event.
        world->mCellData.updateAllCellLinkInteractions(
                0.0, nullptr, true);
    }
    result.organismId = retainedOrganism;
    result.world = std::move(world);
    return result;
}

GenomePreviewFraming frameGenomeFilePreview(const SimulationWorld& world) {
    const CellData& cells = world.mCellData;
    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    int visibleCellCount = 0;

    for (int cellIndex = 0; cellIndex < cells.mCellCount; ++cellIndex) {
        const std::size_t index = static_cast<std::size_t>(cellIndex);
        if (cells.mIsDead[index]) {
            continue;
        }
        const double positionX = cells.mPositionX[index];
        const double positionY = cells.mPositionY[index];
        const double radius = cells.mRadius[index];
        if (!std::isfinite(positionX) || !std::isfinite(positionY)
                || !std::isfinite(radius) || radius <= 0.0) {
            throw std::logic_error(
                    "Genome file preview has invalid Cell geometry");
        }
        const CellTypeInfo typeInfo =
                CellTypes::info(cells.getActiveGeneView(cellIndex).cellType());
        const double outlineRadius = typeInfo.outlineShader.empty()
                ? 0.0
                : static_cast<double>(typeInfo.outlineRadius);
        const double visualRadius = radius + outlineRadius;
        minimumX = std::min(minimumX, positionX - visualRadius);
        maximumX = std::max(maximumX, positionX + visualRadius);
        minimumY = std::min(minimumY, positionY - visualRadius);
        maximumY = std::max(maximumY, positionY + visualRadius);
        ++visibleCellCount;
    }

    GenomePreviewFraming result;
    result.visibleCellCount = visibleCellCount;
    if (visibleCellCount == 0) {
        result.halfExtent = static_cast<float>(
                kMinimumHalfExtent * kFramingScale
                + kFramingAbsoluteMargin);
        return result;
    }

    result.centerX = static_cast<float>((minimumX + maximumX) * 0.5);
    result.centerY = static_cast<float>((minimumY + maximumY) * 0.5);
    const double halfWidth = (maximumX - minimumX) * 0.5;
    const double halfHeight = (maximumY - minimumY) * 0.5;
    result.halfExtent = static_cast<float>(
            std::max({halfWidth, halfHeight, kMinimumHalfExtent})
                    * kFramingScale
            + kFramingAbsoluteMargin);
    return result;
}

} // namespace cellularity::genome_preview
