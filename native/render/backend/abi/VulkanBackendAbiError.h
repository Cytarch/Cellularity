#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace cellularity::render::abi::error {

[[nodiscard]] inline std::size_t validUtf8Prefix(const char* data,
        std::size_t size, std::size_t limit) noexcept {
    std::size_t cursor = 0;
    std::size_t valid = 0;
    while (cursor < size && cursor < limit) {
        const auto lead = static_cast<unsigned char>(data[cursor]);
        std::size_t sequence = 0;
        if (lead <= 0x7F) {
            sequence = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            sequence = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            sequence = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            sequence = 4;
        } else {
            break;
        }
        if (sequence > size - cursor || sequence > limit - cursor)
            break;
        bool continuationValid = true;
        for (std::size_t index = 1; index < sequence; ++index) {
            const auto byte = static_cast<unsigned char>(data[cursor + index]);
            if ((byte & 0xC0U) != 0x80U)
                continuationValid = false;
        }
        if (!continuationValid)
            break;
        if (sequence == 3) {
            const auto second = static_cast<unsigned char>(data[cursor + 1]);
            if ((lead == 0xE0 && second < 0xA0)
                    || (lead == 0xED && second >= 0xA0)) {
                break;
            }
        } else if (sequence == 4) {
            const auto second = static_cast<unsigned char>(data[cursor + 1]);
            if ((lead == 0xF0 && second < 0x90)
                    || (lead == 0xF4 && second >= 0x90)) {
                break;
            }
        }
        cursor += sequence;
        valid = cursor;
    }
    return valid;
}

inline void initializeError(CellularityVulkanErrorV1* output) noexcept {
    if (output == nullptr)
        return;
    std::memset(output, 0, sizeof(*output));
    output->abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    output->struct_size = sizeof(*output);
}

inline void setError(CellularityVulkanErrorV1* output,
        CellularityVulkanErrorCodeV1 code, const char* message,
        std::int32_t backendCode = 0) noexcept {
    if (output == nullptr)
        return;
    initializeError(output);
    output->code = code;
    output->backend_code = backendCode;
    if (message == nullptr)
        return;
    const std::size_t sourceSize = std::strlen(message);
    const std::size_t stored = validUtf8Prefix(
            message, sourceSize, sizeof(output->message) - 1);
    if (stored != 0)
        std::memcpy(output->message, message, stored);
    output->message[stored] = '\0';
    output->message_length = static_cast<std::uint32_t>(stored);
    if (stored != sourceSize) {
        output->flags |= CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1;
    }
}

} // namespace cellularity::render::abi::error
