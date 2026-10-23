#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

#include "core/RenderContactChord.h"

// Sparse capture-owned adjacency storage. Per-Cell metadata is fixed by the
// configured Cell capacity, while RenderContactChord payload exists only for
// constraints collected for the current capture.
class RenderContactChordBuffer final {
public:
    class ConstIterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = RenderContactChord;
        using difference_type = std::ptrdiff_t;
        using pointer = const RenderContactChord*;
        using reference = const RenderContactChord&;

        ConstIterator() noexcept = default;

        [[nodiscard]] reference operator*() const noexcept;
        [[nodiscard]] pointer operator->() const noexcept;
        ConstIterator& operator++() noexcept;
        ConstIterator operator++(int) noexcept;

        friend bool operator==(const ConstIterator& left,
                const ConstIterator& right) noexcept {
            return left.mOwner == right.mOwner
                    && left.mEntryIndex == right.mEntryIndex;
        }

        friend bool operator!=(const ConstIterator& left,
                const ConstIterator& right) noexcept {
            return !(left == right);
        }

    private:
        friend class RenderContactChordBuffer;

        ConstIterator(const RenderContactChordBuffer* owner,
                std::uint32_t entryIndex) noexcept;

        const RenderContactChordBuffer* mOwner = nullptr;
        std::uint32_t mEntryIndex = std::numeric_limits<std::uint32_t>::max();
    };

    class ActiveRange final {
    public:
        [[nodiscard]] int size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] ConstIterator begin() const noexcept;
        [[nodiscard]] ConstIterator end() const noexcept;
        [[nodiscard]] const RenderContactChord& operator[](int index) const noexcept;

    private:
        friend class RenderContactChordBuffer;

        ActiveRange(const RenderContactChordBuffer* owner,
                std::uint32_t firstEntryIndex,
                int size) noexcept;

        const RenderContactChordBuffer* mOwner = nullptr;
        std::uint32_t mFirstEntryIndex =
                std::numeric_limits<std::uint32_t>::max();
        int mSize = 0;
    };

    RenderContactChordBuffer() = default;
    explicit RenderContactChordBuffer(int cellCapacity);

    RenderContactChordBuffer(const RenderContactChordBuffer&) = delete;
    RenderContactChordBuffer& operator=(const RenderContactChordBuffer&) = delete;
    RenderContactChordBuffer(RenderContactChordBuffer&&) noexcept = default;
    RenderContactChordBuffer& operator=(RenderContactChordBuffer&&) noexcept = default;

    void reset(int cellCapacity);
    void beginCollection(int activeCellCount);

    [[nodiscard]] int cellCapacity() const noexcept;
    [[nodiscard]] int activeCellCount() const noexcept;
    [[nodiscard]] int activeChordCount(int cellIndex) const noexcept;
    [[nodiscard]] ActiveRange activeChords(int cellIndex) const noexcept;
    [[nodiscard]] std::size_t activeChordCount() const noexcept;
    [[nodiscard]] std::size_t retainedChordCapacity() const noexcept;

    [[nodiscard]] bool tryAppend(int cellIndex,
            const RenderContactChord& chord) noexcept;

private:
    static constexpr std::uint32_t kNoEntry =
            std::numeric_limits<std::uint32_t>::max();

    struct Entry final {
        RenderContactChord chord{};
        std::uint32_t next = kNoEntry;
    };

    [[nodiscard]] const Entry& entry(std::uint32_t index) const noexcept;

    int mActiveCellCount = 0;
    std::size_t mMaximumChordCount = 0;
    std::vector<std::uint32_t> mFirstEntryByCell;
    std::vector<std::uint32_t> mLastEntryByCell;
    std::vector<std::uint8_t> mChordCountByCell;
    std::vector<Entry> mEntries;
};
