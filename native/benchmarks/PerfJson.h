#pragma once

#include <string>

#include <nlohmann/json.hpp>

using PerfJson = nlohmann::json;

void writePerfJsonReport(const PerfJson& report, const std::string& outputPath);
