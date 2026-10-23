#include "PerfJson.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

void writePerfJsonReport(const PerfJson& report, const std::string& outputPath) {
    if (outputPath.empty() || outputPath == "-") {
        std::cout << report.dump(2) << '\n';
        return;
    }
    std::ofstream output(outputPath);
    if (!output) {
        throw std::runtime_error("Could not open JSON output " + outputPath);
    }
    output << report.dump(2) << '\n';
}
