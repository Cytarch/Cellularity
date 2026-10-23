#include "formats/CellSnapshotWireCodec.h"

#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "formats/InternalWireFormatConstants.h"
#include "io/NativeByteStream.h"

std::vector<std::uint8_t> writeCellSnapshotWire(
        const SimulationWorld& world,
        int cellId) {
    const CellData& cells = world.mCellData;
    ByteWriter writer;
    writer.writeInt(cellularity::internal_wire::kCellSnapshotMagic);
    writer.writeInt(cellularity::internal_wire::kCellSnapshotVersion);
    writer.writeInt(cellId);
    writer.writeDouble(cells.mPositionX[cellId]);
    writer.writeDouble(cells.mPositionY[cellId]);
    writer.writeDouble(cells.mPositionZ[cellId]);
    writer.writeDouble(cells.mRadius[cellId]);
    writer.writeDouble(cells.mAge[cellId]);
    writer.writeDouble(cells.mMass[cellId]);
    writer.writeDouble(cells.mNitrates[cellId]);
    writer.writeDouble(cells.mVelocityX[cellId]);
    writer.writeDouble(cells.mVelocityY[cellId]);
    writer.writeDouble(cells.mAngle[cellId]);
    writer.writeDouble(cells.mAngularVelocity[cellId]);
    writer.writeFloat(cells.mToxin[cellId]);
    writer.writeFloat(cells.mToxication[cellId]);
    writer.writeFloat(cells.mLipids[cellId]);
    writer.writeInt(cells.activeGeneIndex(cellId).value());
    writer.writeInt(cells.mOriginTag[cellId]);
    writer.writeInt(cells.mSelectTag[cellId]);
    writer.writeInt(cells.mMutationCount[cellId]);
    writer.writeInt(cells.mPendingMutationCount[cellId]);
    writer.writeInt(cells.mTelomeresCount[cellId]);
    writer.writeBool(cells.mIsMirrored[cellId]);
    writer.writeInt(CellTypes::ordinal(cells.mActiveCellType[cellId]));
    for (int index = 0; index < 4; ++index) {
        writer.writeFloat(cells.mColor[cellId][index]);
    }
    for (int index = 0; index < CellData::SIGNAL_SUBSTANCE_COUNT; ++index) {
        writer.writeFloat(cells.mSignalSubstance[cellId][index]);
    }
    for (int index = 0; index < CellData::SIGNAL_SUBSTANCE_COUNT; ++index) {
        writer.writeFloat(cells.mNewSignalSubstance[cellId][index]);
    }
    writer.writeLong(static_cast<std::int64_t>(cells.organismId(cellId)));
    writer.writeLong(
            static_cast<std::int64_t>(cells.parentOrganismId(cellId)));
    return writer.takeBytes();
}
