#include "diagnostics/NativeCrashReportWriter.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace cellularity::diagnostics {
namespace {

constexpr std::size_t REPORT_PATH_MAX_BYTES = 640;

class FixedTextBuilder {
public:
    FixedTextBuilder(char* destination, std::size_t destinationSize) noexcept
            : mDestination(destination), mDestinationSize(destinationSize) {
        if (mDestination != nullptr && mDestinationSize > 0) {
            mDestination[0] = '\0';
        }
    }

    void append(const char* value) noexcept {
        if (value == nullptr) {
            append("unknown");
            return;
        }
        for (std::size_t index = 0; value[index] != '\0'; index++) {
            append(value[index]);
        }
    }

    void appendLimited(const char* value, std::size_t maximumCharacters) noexcept {
        if (value == nullptr) {
            append("unknown");
            return;
        }
        for (std::size_t index = 0;
                index < maximumCharacters && value[index] != '\0';
                index++) {
            append(value[index]);
        }
    }

    void append(char value) noexcept {
        if (mDestination == nullptr || mDestinationSize == 0 || mLength + 1 >= mDestinationSize) {
            mTruncated = true;
            return;
        }
        mDestination[mLength++] = value;
        mDestination[mLength] = '\0';
    }

    void appendSigned(std::int64_t value) noexcept {
        if (value < 0) {
            append('-');
            appendUnsigned(static_cast<std::uint64_t>(-(value + 1)) + 1);
            return;
        }
        appendUnsigned(static_cast<std::uint64_t>(value));
    }

    void appendUnsigned(std::uint64_t value) noexcept {
        char reversedDigits[32] = {};
        std::size_t count = 0;
        do {
            reversedDigits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < sizeof(reversedDigits));
        while (count > 0) {
            append(reversedDigits[--count]);
        }
    }

    void appendHex(std::uintptr_t value) noexcept {
        static constexpr char HEX_DIGITS[] = "0123456789abcdef";
        append("0x");
        for (std::size_t index = sizeof(value) * 2; index > 0; index--) {
            const unsigned shift = static_cast<unsigned>((index - 1) * 4);
            append(HEX_DIGITS[(value >> shift) & 0x0f]);
        }
    }

    std::size_t length() const noexcept {
        return mLength;
    }

    bool truncated() const noexcept {
        return mTruncated;
    }

private:
    char* mDestination = nullptr;
    std::size_t mDestinationSize = 0;
    std::size_t mLength = 0;
    bool mTruncated = false;
};

bool writeAll(int fileDescriptor, const char* bytes, std::size_t byteCount) noexcept {
    std::size_t written = 0;
    while (written < byteCount) {
        const ssize_t result = write(fileDescriptor, bytes + written, byteCount - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

std::size_t formatNativeCrashReport(
        char* destination,
        std::size_t destinationSize,
        const NativeCrashReportRecord& record) noexcept {
    if (destination == nullptr || destinationSize == 0) {
        return 0;
    }

    FixedTextBuilder report(destination, destinationSize);
    report.append("Cellularity native diagnostics report\n");
    report.append("timeEpochSeconds=");
    report.appendSigned(record.epochSeconds);
    report.append("\nversion=");
    report.appendLimited(record.appVersion, 127);
    report.append("\ndevice=");
    report.appendLimited(record.device, 255);
    report.append("\nabi=");
    report.appendLimited(record.supportedAbis, 255);
    report.append("\nrenderBackend=");
    report.appendLimited(record.renderBackend, 31);
    report.append("\nrenderBackendFallbackReason=");
    report.appendLimited(record.renderBackendFallbackReason, 63);
    report.append("\nthread=native tid=");
    report.appendSigned(record.threadId);
    report.append(" pid=");
    report.appendSigned(record.processId);
    report.append("\nsignal=");
    report.appendSigned(record.signalNumber);
    report.append(" (");
    report.appendLimited(record.signalName, 31);
    report.append(")\nsignalCode=");
    report.appendSigned(record.signalCode);
    report.append("\nfaultAddress=");
    report.appendHex(record.faultAddress);
    report.append("\nprogramCounter=");
    report.appendHex(record.programCounter);
    report.append(
            "\nstackTrace=unavailable in the in-process native handler; "
            "Android system diagnostics contain the full native backtrace\n");
    return report.length();
}

bool writeNativeCrashReport(
        const char* directory,
        const NativeCrashReportRecord& record) noexcept {
    if (directory == nullptr || directory[0] == '\0') {
        return false;
    }

    char path[REPORT_PATH_MAX_BYTES] = {};
    FixedTextBuilder reportPath(path, sizeof(path));
    reportPath.appendLimited(directory, REPORT_PATH_MAX_BYTES);
    reportPath.append('/');
    reportPath.append(NATIVE_REPORT_FILE_NAME);
    if (reportPath.truncated()) {
        return false;
    }

    char report[NATIVE_REPORT_MAX_BYTES] = {};
    const std::size_t reportLength = formatNativeCrashReport(report, sizeof(report), record);
    if (reportLength == 0) {
        return false;
    }

    const int fileDescriptor = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fileDescriptor < 0) {
        return false;
    }
    const bool complete = writeAll(fileDescriptor, report, reportLength);
    close(fileDescriptor);
    return complete;
}

} // namespace cellularity::diagnostics
