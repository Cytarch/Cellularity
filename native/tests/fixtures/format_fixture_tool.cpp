#include "tests/fixtures/FormatFixtureCorpus.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: cellularity_format_fixture_tool --generate|--verify <corpus-directory>\n";
        return 2;
    }
    try {
        const std::string action = argv[1];
        if (action == "--generate") {
            cellularity::test_fixtures::writeFormatFixtureCorpus(argv[2]);
        } else if (action == "--verify") {
            cellularity::test_fixtures::verifyFormatFixtureCorpus(argv[2]);
        } else {
            std::cerr << "Unknown action: " << action << '\n';
            return 2;
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
