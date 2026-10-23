#include "PerfSignal.h"

#include <stdexcept>

CellSignalUpdateAlgorithm parseSignalUpdateAlgorithm(const std::string& name) {
    if (name == "global-edge") {
        return CellSignalUpdateAlgorithm::GLOBAL_EDGE;
    }
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    if (name == "cell-owned-csr") {
        return CellSignalUpdateAlgorithm::CELL_OWNED_CSR;
    }
#endif
    throw std::invalid_argument("Unknown signal update algorithm: " + name);
}

const char* signalUpdateAlgorithmName(
        CellSignalUpdateAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CellSignalUpdateAlgorithm::GLOBAL_EDGE:
            return "global-edge";
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
        case CellSignalUpdateAlgorithm::CELL_OWNED_CSR:
            return "cell-owned-csr";
#endif
    }
    return "unknown";
}
