#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/SimulationMemoryPolicy.h"
#include "io/DecodeError.h"

namespace {

std::uint64_t readStatusBytes(std::string_view requestedKey) {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, requestedKey.size(), requestedKey) != 0) {
            continue;
        }
        const char* begin = line.data() + requestedKey.size();
        const char* end = line.data() + line.size();
        while (begin != end
                && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
            ++begin;
        }
        std::uint64_t kibibytes = 0;
        const std::from_chars_result parsed = std::from_chars(begin, end, kibibytes);
        if (parsed.ec != std::errc()
                || kibibytes > std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
            return 0;
        }
        return kibibytes * 1024ULL;
    }
    return 0;
}

std::uint64_t positiveDelta(std::uint64_t after, std::uint64_t before) {
    return after >= before ? after - before : 0;
}

int parsePositiveInt(const char* text, const char* name) {
    int value = 0;
    const std::string_view input(text);
    const std::from_chars_result parsed = std::from_chars(
            input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != input.data() + input.size()
            || value <= 0) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return value;
}

int parseNonNegativeInt(const char* text, const char* name) {
    int value = 0;
    const std::string_view input(text);
    const std::from_chars_result parsed = std::from_chars(
            input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != input.data() + input.size()
            || value < 0) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return value;
}

double parsePositiveDouble(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0' || !(value > 0.0)) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return value;
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <maximum-cells> <maximum-nutrients> <world-radius>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        printUsage(argv[0]);
        return 2;
    }

    try {
        const int maximumCells = parsePositiveInt(argv[1], "maximum Cell count");
        const int maximumNutrients = parseNonNegativeInt(
                argv[2], "maximum nutrient count");
        const double radius = parsePositiveDouble(argv[3], "world radius");
        const Environment environment = Environment::constructDefaultEnvironment(
                maximumCells, maximumNutrients, radius);
        const cellularity::memory::RuntimeLimits limits =
                cellularity::memory::currentRuntimeLimits();
        const cellularity::memory::WorldAllocationEstimate estimate =
                cellularity::memory::estimateCurrentWorldAllocation(environment);

        const std::uint64_t rssBefore = readStatusBytes("VmRSS:");
        const std::uint64_t peakBefore = readStatusBytes("VmHWM:");
        auto world = std::make_unique<SimulationWorld>(false, environment);
        const std::uint64_t rssAfter = readStatusBytes("VmRSS:");
        const std::uint64_t peakAfter = readStatusBytes("VmHWM:");

        // Keep the complete world observable until after both memory samples.
        const std::int64_t observedCapacity =
                static_cast<std::int64_t>(world->mCellData.MAX_CELL_COUNT)
                + world->mFoodData.MAX_FOOD_COUNT;

        std::cout << std::setprecision(17)
                  << "{\"address_bits\":" << limits.addressBits
                  << ",\"maximum_cells\":" << maximumCells
                  << ",\"maximum_nutrients\":" << maximumNutrients
                  << ",\"world_radius\":" << radius
                  << ",\"estimated_bytes\":" << estimate.bytes
                  << ",\"renderer_budgeted_bytes\":" << estimate.rendererBudgetedBytes
                  << ",\"budget_bytes\":" << estimate.budgetBytes
                  << ",\"rss_before_bytes\":" << rssBefore
                  << ",\"rss_after_bytes\":" << rssAfter
                  << ",\"rss_delta_bytes\":" << positiveDelta(rssAfter, rssBefore)
                  << ",\"peak_before_bytes\":" << peakBefore
                  << ",\"peak_after_bytes\":" << peakAfter
                  << ",\"peak_delta_bytes\":" << positiveDelta(peakAfter, peakBefore)
                  << ",\"observed_capacity\":" << observedCapacity
                  << "}\n";
        return 0;
    } catch (const DecodeError& error) {
        std::cerr << "{\"error_code\":\"" << decodeErrorCodeName(error.code())
                  << "\",\"message\":\"" << error.what() << "\"}\n";
        return 3;
    } catch (const std::exception& error) {
        std::cerr << "memory probe failed: " << error.what() << '\n';
        return 1;
    }
}
