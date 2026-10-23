#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cellularity::test_fixtures {

struct FixtureSpec {
    std::string id;
    std::string family;
    int outerVersion = -1;
    int environmentVersion = -1;
    int cellVersion = -1;
    int geneVersion = -1;
    int snapshotVersion = -1;
    int productVersion = -1;
    std::string compression = "none";
    int cellCount = 0;
    int geneCount = 0;
    int foodCount = 0;
    int linkCount = 0;
    std::string expected;
    std::string semanticValues;
    std::string covers;
};

const std::vector<FixtureSpec>& formatFixtureSpecs();
void writeFormatFixtureCorpus(const std::filesystem::path& corpusDirectory);
void verifyFormatFixtureCorpus(const std::filesystem::path& corpusDirectory);

} // namespace cellularity::test_fixtures
