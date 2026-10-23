#include "core/CellType.h"

namespace {

constexpr CellTypeInfo makeInfo(std::string_view baseShader) {
    return {baseShader, "", CellDataConstants::MAX_CELL_RADIUS};
}

constexpr CellTypeInfo makeInfo(std::string_view baseShader, std::string_view outlineShader, float outlineRadius) {
    return {baseShader, outlineShader, outlineRadius * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS)};
}

constexpr std::array<CellTypeInfo, CellTypes::length> CELL_TYPE_INFO = {
        makeInfo("phagocyte"),
        makeInfo("phagocyte", "flagellocyte_outline", 4.0f),
        makeInfo("photocyte"),
        makeInfo("phagocyte", "devorocyte_outline", 0.3f),
        makeInfo("lipocyte"),
        makeInfo("keratinocyte"),
        makeInfo("buoyocyte"),
        makeInfo("phagocyte", "glueocyte_outline", 0.4f),
        makeInfo("phagocyte", "virocyte_outline", 0.3f),
        makeInfo("phagocyte", "nitrocyte_outline", 0.45f),
        makeInfo("neurocyte", "stereocyte_outline", 0.4f),
        makeInfo("neurocyte", "senseocyte_outline", 0.2f),
        makeInfo("myocyte"),
        makeInfo("neurocyte"),
        makeInfo("secrocyte"),
        makeInfo("stemocyte"),
        makeInfo("gamete"),
        makeInfo("phagocyte", "ciliocyte_outline", 0.15f)
};

} // namespace

CellTypeInfo CellTypes::info(CellType type) {
    return CELL_TYPE_INFO[ordinal(type)];
}
