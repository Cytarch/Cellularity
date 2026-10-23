#pragma once

namespace cellularity::internal_wire {

inline constexpr int kCellSnapshotMagic = 0x434C4353; // CLCS
inline constexpr int kCellSnapshotVersion = 8;
inline constexpr int kSubstrateInspectionMagic = 0x434C494E; // CLIN
inline constexpr int kSubstrateInspectionVersion = 7;

} // namespace cellularity::internal_wire
