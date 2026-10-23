#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <sys/stat.h>

#include "diagnostics/NativeCrashReportWriter.h"

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("Cannot read native diagnostics report: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

std::filesystem::path makeTemporaryDirectory() {
    std::string pattern =
            (std::filesystem::temp_directory_path() / "cellularity-diagnostics-XXXXXX").string();
    char* directory = mkdtemp(pattern.data());
    if (directory == nullptr) {
        fail("Could not create native diagnostics test directory");
    }
    return directory;
}

cellularity::diagnostics::NativeCrashReportRecord sampleRecord() {
    cellularity::diagnostics::NativeCrashReportRecord record;
    record.epochSeconds = 1'788'868'800;
    record.appVersion = "0.1.0-alpha.1 (1)";
    record.device = "Test Manufacturer Test Device";
    record.supportedAbis = "arm64-v8a, armeabi-v7a";
    record.renderBackend = "vulkan";
    record.renderBackendFallbackReason = "capability_unsupported";
    record.signalNumber = 11;
    record.signalName = "SIGSEGV";
    record.signalCode = 1;
    record.processId = 100;
    record.threadId = 101;
    record.faultAddress = 0x1234;
    record.programCounter = 0x5678;
    return record;
}

void verifyBoundedFormatting() {
    std::string oversizedMetadata(20'000, 'x');
    auto record = sampleRecord();
    record.device = oversizedMetadata.c_str();
    char report[cellularity::diagnostics::NATIVE_REPORT_MAX_BYTES] = {};
    const std::size_t length = cellularity::diagnostics::formatNativeCrashReport(
            report,
            sizeof(report),
            record);

    assertTrue(length > 0 && length < sizeof(report), "Native report must be bounded");
    const std::string text(report, length);
    assertTrue(text.find("timeEpochSeconds=1788868800") != std::string::npos,
            "Native report is missing event time");
    assertTrue(text.find("version=0.1.0-alpha.1 (1)") != std::string::npos,
            "Native report is missing app version");
    assertTrue(text.find("abi=arm64-v8a, armeabi-v7a") != std::string::npos,
            "Native report is missing ABI");
    assertTrue(text.find("renderBackend=vulkan") != std::string::npos,
            "Native report is missing the selected render backend");
    assertTrue(text.find("renderBackendFallbackReason=capability_unsupported")
                    != std::string::npos,
            "Native report is missing the render fallback reason");
    assertTrue(text.find("thread=native tid=101 pid=100") != std::string::npos,
            "Native report is missing thread identity");
    assertTrue(text.find("faultAddress=0x") != std::string::npos && text.find("1234\nprogramCounter=0x") != std::string::npos,
            "Native report is missing fault address");
    assertTrue(text.find("stackTrace=unavailable") != std::string::npos,
            "Native report must disclose its backtrace limitation");
}

void verifyFixedPrivateFileContract() {
    const std::filesystem::path directory = makeTemporaryDirectory();
    try {
        auto first = sampleRecord();
        assertTrue(cellularity::diagnostics::writeNativeCrashReport(
                           directory.c_str(), first),
                "First native report write failed");

        auto second = first;
        second.signalNumber = 6;
        second.signalName = "SIGABRT";
        assertTrue(cellularity::diagnostics::writeNativeCrashReport(
                           directory.c_str(), second),
                "Second native report write failed");

        std::size_t reportCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file())
                reportCount++;
        }
        assertTrue(reportCount == 1, "Native path must retain exactly one fixed report");

        const std::filesystem::path reportPath =
                directory / cellularity::diagnostics::NATIVE_REPORT_FILE_NAME;
        const std::string text = readText(reportPath);
        assertTrue(text.find("SIGABRT") != std::string::npos,
                "Fixed native report was not replaced with the latest event");
        assertTrue(text.size() < cellularity::diagnostics::NATIVE_REPORT_MAX_BYTES,
                "Native report exceeded its byte limit");

        struct stat metadata{};
        assertTrue(stat(reportPath.c_str(), &metadata) == 0,
                "Could not inspect native report permissions");
        assertTrue((metadata.st_mode & 0777) == 0600,
                "Native report permissions must be owner-only");
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    try {
        verifyBoundedFormatting();
        verifyFixedPrivateFileContract();
        assertTrue(!cellularity::diagnostics::writeNativeCrashReport(nullptr, sampleRecord()),
                "Missing diagnostics directory must be rejected");
        std::cout << "native diagnostics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
