#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace cellularity::render::abi::validation {

template <typename T>
[[nodiscard]] bool validSpan(const T* data, std::uint64_t count,
        std::uint32_t elementSize,
        std::uint32_t elementAlignment) noexcept {
    if (elementSize != sizeof(T) || elementAlignment != alignof(T)
            || count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        return false;
    }
    if (count == 0)
        return true;
    return data != nullptr
            && reinterpret_cast<std::uintptr_t>(data) % alignof(T) == 0;
}

[[nodiscard]] inline bool validInt32Span(
        const CellularityVulkanInt32SpanV1& span,
        std::uint64_t expected) noexcept {
    return span.element_count == expected
            && validSpan(span.data, span.element_count, span.element_size,
                    span.element_alignment);
}

[[nodiscard]] inline bool validInt32Matrix(
        const CellularityVulkanInt32MatrixViewV1& matrix) noexcept {
    if (matrix.rows != CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1
            || matrix.columns
                    != CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1
            || matrix.row_stride_elements < matrix.columns
            || matrix.element_size != sizeof(std::int32_t)
            || matrix.element_alignment != alignof(std::int32_t)
            || matrix.reserved_zero != 0) {
        return false;
    }
    const std::uint64_t elements = static_cast<std::uint64_t>(matrix.rows - 1)
                    * matrix.row_stride_elements
            + matrix.columns;
    return validSpan(matrix.data, elements, matrix.element_size,
            matrix.element_alignment);
}

} // namespace cellularity::render::abi::validation
