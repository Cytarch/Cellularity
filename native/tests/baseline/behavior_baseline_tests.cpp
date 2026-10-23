#include <cstdlib>
#include <iostream>
#include <string>

#include "tests/baseline/BehaviorBaseline.h"
#include "tests/support/StableDigest.h"

namespace {

using cellularity::behavior_baseline::ExpectedState;
using cellularity::behavior_baseline::ScenarioDefinition;
using cellularity::behavior_baseline::ScenarioResult;

[[noreturn]] void fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void requireEqual(int expected, int actual, const char* field, const char* scenario) {
    if (expected != actual) {
        fail(std::string(scenario) + ": " + field + " expected="
                + std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

void requireEqual(std::size_t expected, std::size_t actual, const char* field, const char* scenario) {
    if (expected != actual) {
        fail(std::string(scenario) + ": " + field + " expected="
                + std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

void printResult(const ScenarioDefinition& scenario, const ScenarioResult& result) {
    std::cout << scenario.name
              << "\tseed=" << scenario.seed
              << "\tupdates=" << scenario.updateCount
              << "\tdigest=" << result.digest
              << "\tcells=" << result.state.cells
              << "\tliving=" << result.state.livingCells
              << "\tfoods=" << result.state.foods
              << "\tlinks=" << result.state.links
              << "\tmutations=" << result.state.mutations
              << "\tactiveSignals=" << result.state.activeSignalValues
              << "\tsnapshotBytes=" << result.state.snapshotBytes
              << '\n';
}

void verifyExpected(const ScenarioDefinition& scenario, const ScenarioResult& result) {
    if (!result.finiteState) {
        fail(std::string(scenario.name) + ": non-finite semantic state");
    }
    if (result.digest != scenario.expectedDigest) {
        fail(std::string(scenario.name) + ": digest expected=" + scenario.expectedDigest
                + " actual=" + result.digest);
    }
    const ExpectedState& expected = scenario.expectedState;
    requireEqual(expected.cells, result.state.cells, "cells", scenario.name);
    requireEqual(expected.livingCells, result.state.livingCells, "livingCells", scenario.name);
    requireEqual(expected.foods, result.state.foods, "foods", scenario.name);
    requireEqual(expected.links, result.state.links, "links", scenario.name);
    requireEqual(expected.mutations, result.state.mutations, "mutations", scenario.name);
    requireEqual(expected.activeSignalValues, result.state.activeSignalValues, "activeSignalValues", scenario.name);
    requireEqual(expected.snapshotBytes, result.state.snapshotBytes, "snapshotBytes", scenario.name);
}

} // namespace

int main(int argc, char** argv) {
    const bool printOnly = argc == 2 && std::string(argv[1]) == "--print";
    if (cellularity::test_support::stableDigest({})
            != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
        fail("stable digest implementation failed its known empty-input check");
    }
    for (const ScenarioDefinition& scenario : cellularity::behavior_baseline::scenarios()) {
        const ScenarioResult first = cellularity::behavior_baseline::runScenario(scenario);
        const ScenarioResult second = cellularity::behavior_baseline::runScenario(scenario);
        if (first.digest != second.digest) {
            fail(std::string(scenario.name) + ": repeated runs produced different digests");
        }
        printResult(scenario, first);
        if (!printOnly) {
            verifyExpected(scenario, first);
        }
    }
    std::cout << "behavior baseline scenarios passed\n";
    return 0;
}
