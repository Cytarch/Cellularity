#pragma once

#include <array>
#include <string_view>

#include "core/CellDataConstants.h"

enum class CellType {
    PHAGOCYTE = 0,
    FLAGELLOCYTE = 1,
    PHOTOCYTE = 2,
    DEVOROCYTE = 3,
    LIPOCYTE = 4,
    KERATINOCYTE = 5,
    BUOYOCYTE = 6,
    GLUEOCYTE = 7,
    VIROCYTE = 8,
    NITROCYTE = 9,
    STEREOCYTE = 10,
    SENSEOCYTE = 11,
    MYOCYTE = 12,
    NEUROCYTE = 13,
    SECROCYTE = 14,
    STEMOCYTE = 15,
    GAMETE = 16,
    CILIOCYTE = 17
};

struct CellTypeInfo {
    std::string_view baseShader;
    std::string_view outlineShader;
    float outlineRadius;
};

class CellTypes {
public:
    static constexpr int length = 18;
    static constexpr std::array<CellType, length> values = {
            CellType::PHAGOCYTE,
            CellType::FLAGELLOCYTE,
            CellType::PHOTOCYTE,
            CellType::DEVOROCYTE,
            CellType::LIPOCYTE,
            CellType::KERATINOCYTE,
            CellType::BUOYOCYTE,
            CellType::GLUEOCYTE,
            CellType::VIROCYTE,
            CellType::NITROCYTE,
            CellType::STEREOCYTE,
            CellType::SENSEOCYTE,
            CellType::MYOCYTE,
            CellType::NEUROCYTE,
            CellType::SECROCYTE,
            CellType::STEMOCYTE,
            CellType::GAMETE,
            CellType::CILIOCYTE
    };

    static constexpr int ordinal(CellType type) {
        return static_cast<int>(type);
    }

    static constexpr CellType fromOrdinal(int ordinal) {
        return values[ordinal];
    }

    static CellTypeInfo info(CellType type);
};
