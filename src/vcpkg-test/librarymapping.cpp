#include <vcpkg-test/util.h>

#include <vcpkg/librarymapping.h>
#include <vcpkg/triplet.h>

using namespace vcpkg;

TEST_CASE ("librarymapping optimization - simple exclusive directory", "[librarymapping]")
{
    // Test case: fmt package with all files in /include/fmt/
    std::vector<std::string> fmt_paths = {"/usr/local/include/fmt/",
                                          "/usr/local/include/fmt/core.h",
                                          "/usr/local/include/fmt/format.h",
                                          "/usr/local/include/fmt/args.h"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"fmt", fmt_paths}};

    auto result = optimize_header_paths(fmt_paths, all_packages, "fmt");

    // Should optimize to just the directory since it's exclusive to fmt
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/fmt/");
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - shared directory conflict", "[librarymapping]")
{
    // Test case: Two packages sharing the same directory
    std::vector<std::string> package1_paths = {"/usr/local/include/boost/detail/file1.hpp"};

    std::vector<std::string> package2_paths = {"/usr/local/include/boost/detail/file2.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"package1", package1_paths},
                                                                    {"package2", package2_paths}};

    auto result1 = optimize_header_paths(package1_paths, all_packages, "package1");
    auto result2 = optimize_header_paths(package2_paths, all_packages, "package2");

    // Should fall back to individual files since directory is shared
    REQUIRE(result1.directories.empty());
    REQUIRE(result1.individual_files.size() == 1);
    CHECK(result1.individual_files[0] == "/usr/local/include/boost/detail/file1.hpp");

    REQUIRE(result2.directories.empty());
    REQUIRE(result2.individual_files.size() == 1);
    CHECK(result2.individual_files[0] == "/usr/local/include/boost/detail/file2.hpp");
}

TEST_CASE ("librarymapping optimization - nested directory removal", "[librarymapping]")
{
    // Test case: boost-unordered with nested directories and individual files
    std::vector<std::string> unordered_paths = {
        "/usr/local/include/boost/unordered/",
        "/usr/local/include/boost/unordered/detail/",
        "/usr/local/include/boost/unordered/detail/foa/",
        "/usr/local/include/boost/unordered/file1.hpp", // File inside unordered/
        "/usr/local/include/boost/unordered_map.hpp",
        "/usr/local/include/boost/unordered_set.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-unordered", unordered_paths}};

    auto result = optimize_header_paths(unordered_paths, all_packages, "boost-unordered");

    // The algorithm identifies that all files are within the boost/ directory
    // and optimizes to the common parent directory level
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/boost/");

    // All files are covered by the directory, so no individual files needed
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - mixed case with root level files", "[librarymapping]")
{
    // Test case: Package with both exclusive subdirectory and root-level files
    std::vector<std::string> mixed_paths = {"/usr/local/include/boost/algorithm/",
                                            "/usr/local/include/boost/algorithm/searching/",
                                            "/usr/local/include/boost/algorithm/string/",
                                            "/usr/local/include/boost/algorithm/file1.hpp", // File inside algorithm/
                                            "/usr/local/include/boost/algorithm.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-algorithm", mixed_paths}};

    auto result = optimize_header_paths(mixed_paths, all_packages, "boost-algorithm");

    // The algorithm identifies that all files are within the boost/ directory
    // and optimizes to that level
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/boost/");

    // All files are covered by the directory
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - complex nested hierarchy", "[librarymapping]")
{
    // Test case: Complex nested directory structure
    std::vector<std::string> complex_paths = {"/usr/local/include/boost/mpl/",
                                              "/usr/local/include/boost/mpl/aux_/",
                                              "/usr/local/include/boost/mpl/aux_/config/",
                                              "/usr/local/include/boost/mpl/limits/",
                                              "/usr/local/include/boost/mpl/vector/",
                                              "/usr/local/include/boost/mpl/vector/aux_/"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-mpl", complex_paths}};

    auto result = optimize_header_paths(complex_paths, all_packages, "boost-mpl");

    // Should optimize to just the top-level directory
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/boost/mpl/");
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - empty input", "[librarymapping]")
{
    // Test case: Empty input
    std::vector<std::string> empty_paths;
    std::map<std::string, std::vector<std::string>> all_packages;

    auto result = optimize_header_paths(empty_paths, all_packages, "empty");

    CHECK(result.directories.empty());
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - root level files only", "[librarymapping]")
{
    // Test case: Only root-level files, no directories - but they share the same directory
    std::vector<std::string> root_paths = {"/usr/local/include/boost/static_assert.hpp",
                                           "/usr/local/include/boost/throw_exception.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-static-assert", root_paths}};

    auto result = optimize_header_paths(root_paths, all_packages, "boost-static-assert");

    // Since both files are in the same directory (/usr/local/include/boost/),
    // and this package owns that directory exclusively, it should optimize to the directory
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/boost/");
    CHECK(result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - partial directory overlap", "[librarymapping]")
{
    // Test case: Two packages with partial overlap in directory structure
    std::vector<std::string> package1_paths = {"/usr/local/include/boost/detail/file1.hpp",
                                               "/usr/local/include/boost/utility/file1.hpp"};

    std::vector<std::string> package2_paths = {"/usr/local/include/boost/detail/file2.hpp",
                                               "/usr/local/include/boost/core/file2.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"package1", package1_paths},
                                                                    {"package2", package2_paths}};

    auto result1 = optimize_header_paths(package1_paths, all_packages, "package1");
    auto result2 = optimize_header_paths(package2_paths, all_packages, "package2");

    // package1 should get utility/ directory but detail/ file individually
    REQUIRE(result1.directories.size() == 1);
    CHECK(result1.directories[0] == "/usr/local/include/boost/utility/");
    REQUIRE(result1.individual_files.size() == 1);
    CHECK(result1.individual_files[0] == "/usr/local/include/boost/detail/file1.hpp");

    // package2 should get core/ directory but detail/ file individually
    REQUIRE(result2.directories.size() == 1);
    CHECK(result2.directories[0] == "/usr/local/include/boost/core/");
    REQUIRE(result2.individual_files.size() == 1);
    CHECK(result2.individual_files[0] == "/usr/local/include/boost/detail/file2.hpp");
}

TEST_CASE ("librarymapping optimization - directory sorting and nesting", "[librarymapping]")
{
    // Test case: Ensure proper sorting and nesting detection
    std::vector<std::string> nested_paths = {
        "/usr/local/include/boost/fusion/view/zip_view/detail/",
        "/usr/local/include/boost/fusion/view/",
        "/usr/local/include/boost/fusion/",
        "/usr/local/include/boost/fusion/view/zip_view/",
        "/usr/local/include/boost/fusion/file1.hpp" // File inside fusion/
    };

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-fusion", nested_paths}};

    auto result = optimize_header_paths(nested_paths, all_packages, "boost-fusion");

    // Should optimize to just the top-level directory
    REQUIRE(result.directories.size() == 1);
    CHECK(result.directories[0] == "/usr/local/include/boost/fusion/");
    CHECK(result.individual_files.empty());
}