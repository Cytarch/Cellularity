#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "core/SimulationRandom.h"

namespace cellularity {

struct SubstrateBounds final {
    double minX;
    double maxX;
    double minY;
    double maxY;
};

struct SubstrateBoundaryContact final {
    double centerDistance;
    double chordLength;
    double wallRadius;
    double normalDirection;
};

struct SubstrateBoundaryLocation final {
    double centerDistance;
    double clearance;
    double normalX;
    double normalY;
    double normalDirection;
};

struct SubstrateBoundaryChord final {
    double x1;
    double y1;
    double x2;
    double y2;
};

// Circular or annular dish centered at the world origin. A zero inner radius
// describes the original circle; nonzero inner radii describe an obstacle.
// Geometry queries do not own simulation state or choose boundary behavior.
// Keep the circle's arithmetic and random draw order unchanged.
class SubstrateGeometry final {
public:
    explicit constexpr SubstrateGeometry(double radius, double innerRadius = 0.0) noexcept
            : mRadius(radius), mInnerRadius(innerRadius) {}

    [[nodiscard]] constexpr double radius() const noexcept {
        return mRadius;
    }

    [[nodiscard]] constexpr double innerRadius() const noexcept {
        return mInnerRadius;
    }

    [[nodiscard]] constexpr bool isRing() const noexcept {
        return mInnerRadius > 0.0;
    }

    // One boundary contact/link per cell is sufficient only if a maximum-size
    // cell cannot touch both walls. The hole must also exceed a cell's radius,
    // so the inner contact always has a well-defined intersection chord.
    [[nodiscard]] bool isValid(double maximumCellRadius) const noexcept {
        return std::isfinite(mRadius) && mRadius > 0.0
                && std::isfinite(mInnerRadius) && mInnerRadius >= 0.0
                && (!isRing() || (mInnerRadius > maximumCellRadius && mRadius - mInnerRadius > 2.0 * maximumCellRadius * (1.0 + 1e-6)));
    }

    [[nodiscard]] constexpr SubstrateBounds bounds() const noexcept {
        return {-mRadius, mRadius, -mRadius, mRadius};
    }

    [[nodiscard]] constexpr double area() const noexcept {
        return 3.14159265358979323846 * mRadius * mRadius
                - 3.14159265358979323846 * mInnerRadius * mInnerRadius;
    }

    // Preserve the caller's scalar precision, including float Food positions.
    template <typename Scalar>
    [[nodiscard]] bool isOutside(Scalar x, Scalar y) const noexcept {
        const auto distanceSq = x * x + y * y;
        return distanceSq > mRadius * mRadius
                || (isRing() && distanceSq < mInnerRadius * mInnerRadius);
    }

    template <typename Scalar>
    [[nodiscard]] bool isOutsideOrOnBoundary(Scalar x, Scalar y) const noexcept {
        const auto distanceSq = x * x + y * y;
        return distanceSq >= mRadius * mRadius
                || (isRing() && distanceSq <= mInnerRadius * mInnerRadius);
    }

    [[nodiscard]] bool cellClearsBoundary(double x, double y, double cellRadius) const noexcept {
        const double contactDistance = mRadius - cellRadius;
        const double innerContactDistance = mInnerRadius + cellRadius;
        const double distanceSq = x * x + y * y;
        return distanceSq < contactDistance * contactDistance
                && (!isRing() || distanceSq > innerContactDistance * innerContactDistance);
    }

    [[nodiscard]] SubstrateBoundaryLocation boundaryLocation(double x, double y) const noexcept {
        const double distance = std::sqrt(x * x + y * y);
        if (isInnerBoundary(distance)) {
            return {distance, distance - mInnerRadius, distance != 0.0 ? -x / distance : -1.0, distance != 0.0 ? -y / distance : 0.0, -1.0};
        }
        return {distance, mRadius - distance, distance != 0.0 ? x / distance : 0.0, distance != 0.0 ? y / distance : 0.0, 1.0};
    }

    [[nodiscard]] bool cellBoundaryContact(double x, double y, double cellRadius, SubstrateBoundaryContact& contact) const noexcept {
        if (cellClearsBoundary(x, y, cellRadius)) {
            return false;
        }
        contact.centerDistance = std::sqrt(x * x + y * y);
        const double distance = contact.centerDistance;
        const bool inner = isInnerBoundary(distance);
        contact.wallRadius = inner ? mInnerRadius : mRadius;
        contact.normalDirection = inner ? -1.0 : 1.0;
        const double wallRadius = contact.wallRadius;
        // Invalid/fully contained positions can occur during frozen editing.
        // They have no intersection chord and must never introduce NaNs.
        if (isRing() && (distance == 0.0 || distance < std::abs(wallRadius - cellRadius) || distance > wallRadius + cellRadius)) {
            return false;
        }
        const double chordProduct = (-distance + cellRadius - wallRadius)
                * (-distance - cellRadius + wallRadius)
                * (-distance + cellRadius + wallRadius)
                * (distance + cellRadius + wallRadius);
        contact.chordLength = std::sqrt(isRing() ? std::max(0.0, chordProduct) : chordProduct) / distance;
        return true;
    }

    // The chord is relative to the Cell center, shared by all render backends.
    [[nodiscard]] SubstrateBoundaryChord cellBoundaryChord(double x, double y, double cellRadius, const SubstrateBoundaryContact& contact) const noexcept {
        const double distance = contact.centerDistance;
        const double midpointDistance = (distance * distance - contact.wallRadius * contact.wallRadius + cellRadius * cellRadius) / (2.0 * distance);
        const double midpointX = midpointDistance * -x / distance;
        const double midpointY = midpointDistance * -y / distance;
        SubstrateBoundaryChord chord{
                midpointX + contact.chordLength * y / (2.0 * distance),
                midpointY - contact.chordLength * x / (2.0 * distance),
                midpointX - contact.chordLength * y / (2.0 * distance),
                midpointY + contact.chordLength * x / (2.0 * distance)
        };
        // The blocked half-plane is reversed at the inner wall.
        if (contact.normalDirection < 0.0) {
            return {chord.x2, chord.y2, chord.x1, chord.y1};
        }
        return chord;
    }

    [[nodiscard]] std::array<double, 2> boundaryAnchor(double x, double y, double centerDistance, double offset) const noexcept {
        const double anchorRadius = isInnerBoundary(centerDistance) ? mInnerRadius - offset : mRadius + offset;
        return {x / centerDistance * anchorRadius, y / centerDistance * anchorRadius};
    }

    template <typename Scalar>
    [[nodiscard]] std::array<Scalar, 2> projectToBoundary(Scalar x, Scalar y, Scalar centerDistance) const noexcept {
        const double wallRadius = isInnerBoundary(centerDistance) ? mInnerRadius : mRadius;
        if (isRing() && centerDistance == 0) {
            return {static_cast<Scalar>(wallRadius), Scalar{0}};
        }
        return {static_cast<Scalar>(wallRadius) * x / centerDistance, static_cast<Scalar>(wallRadius) * y / centerDistance};
    }

    // Endpoint classification alone misses fast particles that cross the hole
    // and emerge on the other side within one update.
    [[nodiscard]] bool clipMotionAtInnerBoundary(double oldX, double oldY, double newX, double newY, std::array<double, 2>& hit) const noexcept {
        if (!isRing()) {
            return false;
        }
        const double dx = newX - oldX;
        const double dy = newY - oldY;
        const double motionSq = dx * dx + dy * dy;
        const double projection = oldX * dx + oldY * dy;
        if (motionSq == 0.0 || projection >= 0.0) {
            return false;
        }
        const double closestFraction = std::min(1.0, -projection / motionSq);
        const double closestX = oldX + closestFraction * dx;
        const double closestY = oldY + closestFraction * dy;
        if (closestX * closestX + closestY * closestY >= mInnerRadius * mInnerRadius) {
            return false;
        }
        const double clearanceSq = oldX * oldX + oldY * oldY - mInnerRadius * mInnerRadius;
        if (clearanceSq < 0.0) {
            return false;
        }
        const double discriminant = projection * projection - motionSq * clearanceSq;
        // A stable first-hit root avoids subtracting nearly equal values.
        const double fraction = clearanceSq / (-projection + std::sqrt(std::max(0.0, discriminant)));
        const double x = oldX + fraction * dx;
        const double y = oldY + fraction * dy;
        const double scale = mInnerRadius / std::hypot(x, y);
        hit = {x * scale, y * scale};
        return true;
    }

    [[nodiscard]] std::array<double, 2> samplePoint(SimulationRandom& random) const {
        if (isRing()) {
            // Uniform area sampling without rejection in narrow annuli.
            const double fraction = (random.nextUniformSymmetric() + 1.0) * 0.5;
            const double distance = std::sqrt(mInnerRadius * mInnerRadius
                    + fraction * (mRadius * mRadius - mInnerRadius * mInnerRadius));
            const double angle = random.nextUniformSymmetric() * 3.14159265358979323846;
            return {distance * std::cos(angle), distance * std::sin(angle)};
        }
        std::array<double, 2> point;
        do {
            point[0] = random.nextUniformSymmetric() * mRadius;
            point[1] = random.nextUniformSymmetric() * mRadius;
        } while (isOutside(point[0], point[1]));
        return point;
    }

private:
    [[nodiscard]] bool isInnerBoundary(double centerDistance) const noexcept {
        return isRing() && centerDistance - mInnerRadius < mRadius - centerDistance;
    }

    double mRadius;
    double mInnerRadius;
};

} // namespace cellularity
