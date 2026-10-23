#include "render/frame/RenderContactChordBuffer.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include "core/RendererMemoryModel.h"
#include "render/frame/RenderContactLimits.h"

namespace {

std::size_t checkedCellCapacity(int cellCapacity) {
    if (cellCapacity < 0) {
        throw std::length_error(
                "Render contact-chord Cell capacity cannot be negative");
    }
    return static_cast<std::size_t>(cellCapacity);
}

std::size_t checkedMaximumChordCount(std::size_t cellCapacity) {
    constexpr std::size_t perCell = static_cast<std::size_t>(
            cellularity::render::kMaximumRenderContactChordsPerCell);
    if (cellCapacity > std::numeric_limits<std::uint32_t>::max() / perCell) {
        throw std::length_error(
                "Render contact-chord sparse capacity exceeds its index representation");
    }
    return cellCapacity * perCell;
}

} // namespace

RenderContactChordBuffer::ConstIterator::ConstIterator(
        const RenderContactChordBuffer* owner,
        std::uint32_t entryIndex) noexcept
        : mOwner(owner),
          mEntryIndex(entryIndex) {
}

RenderContactChordBuffer::ConstIterator::reference
RenderContactChordBuffer::ConstIterator::operator*() const noexcept {
    assert(mOwner != nullptr);
    assert(mEntryIndex != kNoEntry);
    return mOwner->entry(mEntryIndex).chord;
}

RenderContactChordBuffer::ConstIterator::pointer
RenderContactChordBuffer::ConstIterator::operator->() const noexcept {
    return &operator*();
}

RenderContactChordBuffer::ConstIterator&
RenderContactChordBuffer::ConstIterator::operator++() noexcept {
    assert(mOwner != nullptr);
    assert(mEntryIndex != kNoEntry);
    mEntryIndex = mOwner->entry(mEntryIndex).next;
    return *this;
}

RenderContactChordBuffer::ConstIterator
RenderContactChordBuffer::ConstIterator::operator++(int) noexcept {
    ConstIterator previous = *this;
    ++(*this);
    return previous;
}

RenderContactChordBuffer::ActiveRange::ActiveRange(
        const RenderContactChordBuffer* owner,
        std::uint32_t firstEntryIndex,
        int size) noexcept
        : mOwner(owner),
          mFirstEntryIndex(firstEntryIndex),
          mSize(size) {
}

int RenderContactChordBuffer::ActiveRange::size() const noexcept {
    return mSize;
}

bool RenderContactChordBuffer::ActiveRange::empty() const noexcept {
    return mSize == 0;
}

RenderContactChordBuffer::ConstIterator
RenderContactChordBuffer::ActiveRange::begin() const noexcept {
    return ConstIterator(mOwner, mFirstEntryIndex);
}

RenderContactChordBuffer::ConstIterator
RenderContactChordBuffer::ActiveRange::end() const noexcept {
    return ConstIterator(mOwner, kNoEntry);
}

const RenderContactChord& RenderContactChordBuffer::ActiveRange::operator[](
        int index) const noexcept {
    assert(index >= 0 && index < mSize);
    std::uint32_t entryIndex = mFirstEntryIndex;
    for (int current = 0; current < index; ++current) {
        entryIndex = mOwner->entry(entryIndex).next;
    }
    return mOwner->entry(entryIndex).chord;
}

RenderContactChordBuffer::RenderContactChordBuffer(int cellCapacity) {
    reset(cellCapacity);
}

void RenderContactChordBuffer::reset(int cellCapacity) {
    static_assert(sizeof(Entry) <= cellularity::memory::kSparseRenderContactChordEntryBudgetBytes,
            "RendererMemoryModel must cover sparse contact entries");
    const std::size_t capacity = checkedCellCapacity(cellCapacity);
    mMaximumChordCount = checkedMaximumChordCount(capacity);
    mFirstEntryByCell.assign(capacity, kNoEntry);
    mLastEntryByCell.assign(capacity, kNoEntry);
    mChordCountByCell.assign(capacity, 0);
    std::vector<Entry>().swap(mEntries);
    mActiveCellCount = cellCapacity;
}

void RenderContactChordBuffer::beginCollection(int activeCellCount) {
    if (activeCellCount < 0 || activeCellCount > cellCapacity()) {
        throw std::length_error(
                "Render contact-chord active Cell count is out of range");
    }
    const auto activeEnd = static_cast<std::size_t>(activeCellCount);
    std::fill(mFirstEntryByCell.begin(),
            mFirstEntryByCell.begin() + activeEnd,
            kNoEntry);
    std::fill(mLastEntryByCell.begin(),
            mLastEntryByCell.begin() + activeEnd,
            kNoEntry);
    std::fill(mChordCountByCell.begin(),
            mChordCountByCell.begin() + activeEnd,
            std::uint8_t{0});
    mEntries.clear();
    mActiveCellCount = activeCellCount;
}

int RenderContactChordBuffer::cellCapacity() const noexcept {
    return static_cast<int>(mFirstEntryByCell.size());
}

int RenderContactChordBuffer::activeCellCount() const noexcept {
    return mActiveCellCount;
}

int RenderContactChordBuffer::activeChordCount(int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mActiveCellCount);
    return static_cast<int>(
            mChordCountByCell[static_cast<std::size_t>(cellIndex)]);
}

RenderContactChordBuffer::ActiveRange RenderContactChordBuffer::activeChords(
        int cellIndex) const noexcept {
    assert(cellIndex >= 0 && cellIndex < mActiveCellCount);
    const std::size_t index = static_cast<std::size_t>(cellIndex);
    return ActiveRange(this,
            mFirstEntryByCell[index],
            static_cast<int>(mChordCountByCell[index]));
}

std::size_t RenderContactChordBuffer::activeChordCount() const noexcept {
    return mEntries.size();
}

std::size_t RenderContactChordBuffer::retainedChordCapacity() const noexcept {
    return mEntries.capacity();
}

bool RenderContactChordBuffer::tryAppend(
        int cellIndex,
        const RenderContactChord& chord) noexcept {
    assert(cellIndex >= 0 && cellIndex < mActiveCellCount);
    const std::size_t cell = static_cast<std::size_t>(cellIndex);
    std::uint8_t& chordCount = mChordCountByCell[cell];
    assert(chordCount <= cellularity::render::kMaximumRenderContactChordsPerCell);
    if (chordCount == cellularity::render::kMaximumRenderContactChordsPerCell || mEntries.size() == mMaximumChordCount) {
        return false;
    }

    const std::uint32_t newEntryIndex =
            static_cast<std::uint32_t>(mEntries.size());
    mEntries.push_back({chord, kNoEntry});
    const std::uint32_t previousEntryIndex = mLastEntryByCell[cell];
    if (previousEntryIndex == kNoEntry) {
        mFirstEntryByCell[cell] = newEntryIndex;
    } else {
        mEntries[previousEntryIndex].next = newEntryIndex;
    }
    mLastEntryByCell[cell] = newEntryIndex;
    ++chordCount;
    return true;
}

const RenderContactChordBuffer::Entry& RenderContactChordBuffer::entry(
        std::uint32_t index) const noexcept {
    assert(index < mEntries.size());
    return mEntries[index];
}
