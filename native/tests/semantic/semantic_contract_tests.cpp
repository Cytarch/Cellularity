#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tests/semantic/SemanticContractFixtures.h"
#include "verification/SemanticTrace.h"

#ifndef CELLULARITY_SEMANTIC_BASELINE_PATH
#error "CELLULARITY_SEMANTIC_BASELINE_PATH must name the Level-1 golden file"
#endif

namespace {

using cellularity::semantic_tests::FixtureResult;
using cellularity::verification::ScopedSemanticTrace;
using cellularity::verification::SemanticEventKind;
using cellularity::verification::SemanticTraceSink;
using cellularity::verification::TraceChannel;
using cellularity::verification::TraceChannelResult;

struct ExpectedTrace {
    std::uint64_t eventCount = 0;
    std::string digest;
    std::string stateDigest;
    std::size_t stateBytes = 0;
    std::vector<std::string> eventDigests;
};

using ExpectedKey = std::pair<std::string, std::string>;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(delimiter, begin);
        parts.push_back(text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            return parts;
        }
        begin = end + 1;
    }
}

std::vector<std::string> parseEventDigests(const std::string& value) {
    return value == "-" ? std::vector<std::string>{} : split(value, ',');
}

std::map<ExpectedKey, ExpectedTrace> loadExpected() {
    std::ifstream input(CELLULARITY_SEMANTIC_BASELINE_PATH);
    if (!input) {
        fail("cannot open semantic baseline: " CELLULARITY_SEMANTIC_BASELINE_PATH);
    }
    std::map<ExpectedKey, ExpectedTrace> expected;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        lineNumber++;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields = split(line, '\t');
        if (fields.size() != 7) {
            fail("semantic baseline line " + std::to_string(lineNumber)
                    + " must have seven tab-separated fields");
        }
        ExpectedTrace trace;
        trace.eventCount = std::stoull(fields[2]);
        trace.digest = fields[3];
        trace.stateDigest = fields[4];
        trace.stateBytes = static_cast<std::size_t>(std::stoull(fields[5]));
        trace.eventDigests = parseEventDigests(fields[6]);
        const ExpectedKey key{fields[0], fields[1]};
        if (!expected.emplace(key, std::move(trace)).second) {
            fail("duplicate semantic baseline row for " + fields[0] + '/' + fields[1]);
        }
    }
    return expected;
}

std::string joinEventDigests(const std::vector<std::string>& digests) {
    if (digests.empty()) {
        return "-";
    }
    std::ostringstream out;
    for (std::size_t index = 0; index < digests.size(); index++) {
        if (index != 0) {
            out << ',';
        }
        out << digests[index];
    }
    return out.str();
}

void emitChannel(const FixtureResult& fixture,
        const char* channelName,
        const TraceChannelResult& channel,
        std::ostream& output) {
    output << fixture.name << '\t'
           << channelName << '\t'
           << channel.eventCount << '\t'
           << channel.digest << '\t'
           << fixture.normalizedStateDigest << '\t'
           << fixture.normalizedStateBytes << '\t'
           << joinEventDigests(channel.capturedEventDigests) << '\n';
}

void emitBaseline(const std::vector<FixtureResult>& fixtures,
        std::ostream& output) {
    output << "# Cellularity simulation semantic contract v1\n"
           << "# fixture\tchannel\tevent_count\tstream_sha256\tstate_sha256\tstate_bytes\tevent_hash64_window\n";
    for (const FixtureResult& fixture : fixtures) {
        emitChannel(fixture, "sweep", fixture.trace.sweep, output);
        emitChannel(fixture, "endpoint", fixture.trace.endpoint, output);
        emitChannel(fixture, "apply", fixture.trace.apply, output);
    }
}

void requireRepeatEqual(const FixtureResult& first, const FixtureResult& second) {
    auto channelEqual = [](const TraceChannelResult& left,
                                const TraceChannelResult& right) {
        return left.eventCount == right.eventCount
                && left.digest == right.digest
                && left.capturedEventDigests == right.capturedEventDigests
                && left.diagnosticWindowTruncated == right.diagnosticWindowTruncated;
    };
    if (!channelEqual(first.trace.sweep, second.trace.sweep)
            || !channelEqual(first.trace.endpoint, second.trace.endpoint)
            || !channelEqual(first.trace.apply, second.trace.apply)
            || first.normalizedStateDigest != second.normalizedStateDigest
            || first.normalizedStateBytes != second.normalizedStateBytes) {
        fail(first.name + ": repeated fixture execution is not exact");
    }
}

std::string mismatchWindow(const ExpectedTrace& expected,
        const TraceChannelResult& actual) {
    const std::size_t shared = std::min(expected.eventDigests.size(),
            actual.capturedEventDigests.size());
    std::size_t firstDifference = shared;
    for (std::size_t index = 0; index < shared; index++) {
        if (expected.eventDigests[index] != actual.capturedEventDigests[index]) {
            firstDifference = index;
            break;
        }
    }
    if (firstDifference == shared
            && expected.eventDigests.size() == actual.capturedEventDigests.size()) {
        return "event window is identical; divergence is after the bounded window";
    }

    const std::size_t begin = firstDifference > 2 ? firstDifference - 2 : 0;
    const std::size_t end = std::min(
            std::max(expected.eventDigests.size(), actual.capturedEventDigests.size()),
            firstDifference + 3);
    std::ostringstream out;
    out << "first event divergence=" << firstDifference << " window=[" << begin
        << ',' << end << ')';
    for (std::size_t index = begin; index < end; index++) {
        out << "\n  [" << index << "] expected=";
        if (index < expected.eventDigests.size()) {
            out << expected.eventDigests[index];
        } else {
            out << "<missing>";
        }
        out << " actual=";
        if (index < actual.capturedEventDigests.size()) {
            out << actual.capturedEventDigests[index];
            if (index < actual.capturedEventDescriptions.size()) {
                out << ' ' << actual.capturedEventDescriptions[index];
            }
        } else {
            out << "<missing>";
        }
    }
    return out.str();
}

void verifyChannel(const FixtureResult& fixture,
        const char* channelName,
        const TraceChannelResult& actual,
        const std::map<ExpectedKey, ExpectedTrace>& expectedRows) {
    if (actual.diagnosticWindowTruncated) {
        fail(fixture.name + '/' + channelName
                + ": fixture exceeds the bounded 512-event diagnostic window");
    }
    const auto found = expectedRows.find({fixture.name, channelName});
    if (found == expectedRows.end()) {
        fail("missing semantic baseline row for " + fixture.name + '/' + channelName);
    }
    const ExpectedTrace& expected = found->second;
    if (expected.stateDigest != fixture.normalizedStateDigest
            || expected.stateBytes != fixture.normalizedStateBytes) {
        fail(fixture.name + ": normalized state mismatch: expected="
                + expected.stateDigest + '/' + std::to_string(expected.stateBytes)
                + " actual=" + fixture.normalizedStateDigest + '/'
                + std::to_string(fixture.normalizedStateBytes));
    }
    if (expected.eventCount != actual.eventCount
            || expected.digest != actual.digest
            || expected.eventDigests != actual.capturedEventDigests) {
        fail(fixture.name + '/' + channelName + ": trace mismatch: expected count="
                + std::to_string(expected.eventCount) + " digest=" + expected.digest
                + ", actual count=" + std::to_string(actual.eventCount)
                + " digest=" + actual.digest + "\n"
                + mismatchWindow(expected, actual));
    }
}

std::vector<FixtureResult> runAllFixtures() {
    std::vector<FixtureResult> fixtures;
    for (const std::string& name :
            cellularity::semantic_tests::semanticFixtureNames()) {
        try {
            FixtureResult first = cellularity::semantic_tests::runSemanticFixture(name);
            FixtureResult second = cellularity::semantic_tests::runSemanticFixture(name);
            requireRepeatEqual(first, second);
            fixtures.push_back(std::move(first));
        } catch (const std::exception& error) {
            fail(name + ": " + error.what());
        }
    }
    cellularity::semantic_tests::verifySaveLoadTieOrderContract();
    return fixtures;
}

void verifyBoundedDiagnosticWindow() {
    SemanticTraceSink sink;
    {
        ScopedSemanticTrace trace(sink);
        for (std::size_t index = 0;
                index < SemanticTraceSink::kMaximumCapturedEventsPerChannel + 1;
                index++) {
            cellularity::verification::emitSemanticTrace(
                    TraceChannel::Sweep,
                    SemanticEventKind::SweepPair,
                    {cellularity::verification::traceU64(index)});
        }
    }
    const TraceChannelResult result = sink.finish().sweep;
    if (result.eventCount
                    != SemanticTraceSink::kMaximumCapturedEventsPerChannel + 1
            || result.capturedEventDigests.size()
                    != SemanticTraceSink::kMaximumCapturedEventsPerChannel
            || result.capturedEventDescriptions.size()
                    != SemanticTraceSink::kMaximumCapturedEventsPerChannel
            || !result.diagnosticWindowTruncated) {
        fail("semantic trace diagnostic storage is not bounded as specified");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool emit = argc == 2 && std::string(argv[1]) == "--emit-baseline";
        const bool write = argc == 3
                && std::string(argv[1]) == "--write-baseline";
        if (argc > 1 && !emit && !write) {
            fail("usage: cellularity_semantic_contract_tests "
                 "[--emit-baseline | --write-baseline <path>]");
        }
        verifyBoundedDiagnosticWindow();
        const std::vector<FixtureResult> fixtures = runAllFixtures();
        if (emit) {
            emitBaseline(fixtures, std::cout);
            return 0;
        }
        if (write) {
            std::ofstream output(argv[2], std::ios::trunc);
            if (!output) {
                fail("cannot create semantic baseline: "
                        + std::string(argv[2]));
            }
            emitBaseline(fixtures, output);
            output.close();
            if (!output) {
                fail("cannot write semantic baseline: "
                        + std::string(argv[2]));
            }
            return 0;
        }

        const std::map<ExpectedKey, ExpectedTrace> expected = loadExpected();
        for (const FixtureResult& fixture : fixtures) {
            verifyChannel(fixture, "sweep", fixture.trace.sweep, expected);
            verifyChannel(fixture, "endpoint", fixture.trace.endpoint, expected);
            verifyChannel(fixture, "apply", fixture.trace.apply, expected);
        }
        if (expected.size() != fixtures.size() * 3U) {
            fail("semantic baseline contains stale or duplicate fixture rows");
        }
        std::cout << "simulation semantic contract: " << fixtures.size()
                  << " fixtures, three exact traces each, save/load identity check passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulation semantic contract failed: " << error.what() << '\n';
        return 1;
    }
}
