#pragma once

#include <string>

#include "core/CellData.h"

CellSignalUpdateAlgorithm parseSignalUpdateAlgorithm(const std::string& name);
const char* signalUpdateAlgorithmName(
        CellSignalUpdateAlgorithm algorithm) noexcept;
