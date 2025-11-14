#include <vcpkg-test/util.h>

#include <vcpkg/base/json.h>
#include <vcpkg/librarymapping.h>
#include <vcpkg/triplet.h>

using namespace vcpkg;

// Note on JSON format testing:
// The generate_mapping_content() function that produces JSON output is in an anonymous namespace
// and is tested indirectly through integration testing and manual verification.
// The JSON generation uses vcpkg's battle-tested Json::Object and Json::Array APIs,
// and the logic is straightforward (wrapping package data into JSON structure).
// All complex logic (path optimization) is tested directly in the test cases below.

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

TEST_CASE ("librarymapping optimization - incremental package addition", "[librarymapping]")
{
    // Test case: Verify optimization is recalculated when packages are added incrementally

    // Start with fmt package alone
    std::vector<std::string> fmt_paths = {"/usr/local/include/fmt/core.h", "/usr/local/include/fmt/format.h"};
    std::map<std::string, std::vector<std::string>> packages_stage1 = {{"fmt", fmt_paths}};
    auto fmt_result = optimize_header_paths(fmt_paths, packages_stage1, "fmt");

    // fmt should get the directory since it's exclusive
    REQUIRE(fmt_result.directories.size() == 1);
    CHECK(fmt_result.directories[0] == "/usr/local/include/fmt/");
    CHECK(fmt_result.individual_files.empty());

    // Add boost package that shares a root directory
    std::vector<std::string> boost_paths = {"/usr/local/include/boost/algorithm.hpp",
                                            "/usr/local/include/boost/format.hpp"};
    std::map<std::string, std::vector<std::string>> packages_stage2 = {{"fmt", fmt_paths}, {"boost", boost_paths}};

    // Re-optimize both packages with new conflict information
    auto fmt_result2 = optimize_header_paths(fmt_paths, packages_stage2, "fmt");
    auto boost_result = optimize_header_paths(boost_paths, packages_stage2, "boost");

    // Both should still get their respective directories since they don't conflict
    REQUIRE(fmt_result2.directories.size() == 1);
    CHECK(fmt_result2.directories[0] == "/usr/local/include/fmt/");
    CHECK(fmt_result2.individual_files.empty());

    REQUIRE(boost_result.directories.size() == 1);
    CHECK(boost_result.directories[0] == "/usr/local/include/boost/");
    CHECK(boost_result.individual_files.empty());
}

TEST_CASE ("librarymapping optimization - incremental with directory conflicts", "[librarymapping]")
{
    // Test case: Verify that adding a package that creates conflicts triggers re-optimization

    // Start with package1 that owns a directory
    std::vector<std::string> package1_paths = {"/usr/local/include/shared/file1.hpp"};
    std::map<std::string, std::vector<std::string>> packages_stage1 = {{"package1", package1_paths}};
    auto result1_alone = optimize_header_paths(package1_paths, packages_stage1, "package1");

    // package1 should get the directory since it's exclusive
    REQUIRE(result1_alone.directories.size() == 1);
    CHECK(result1_alone.directories[0] == "/usr/local/include/shared/");
    CHECK(result1_alone.individual_files.empty());

    // Add package2 that conflicts with the same directory
    std::vector<std::string> package2_paths = {"/usr/local/include/shared/file2.hpp"};
    std::map<std::string, std::vector<std::string>> packages_stage2 = {{"package1", package1_paths},
                                                                       {"package2", package2_paths}};

    // Re-optimize both packages - now they should conflict and fall back to individual files
    auto result1_conflict = optimize_header_paths(package1_paths, packages_stage2, "package1");
    auto result2_conflict = optimize_header_paths(package2_paths, packages_stage2, "package2");

    // Both should now use individual files since directory is shared
    CHECK(result1_conflict.directories.empty());
    REQUIRE(result1_conflict.individual_files.size() == 1);
    CHECK(result1_conflict.individual_files[0] == "/usr/local/include/shared/file1.hpp");

    CHECK(result2_conflict.directories.empty());
    REQUIRE(result2_conflict.individual_files.size() == 1);
    CHECK(result2_conflict.individual_files[0] == "/usr/local/include/shared/file2.hpp");
}

TEST_CASE ("librarymapping optimization - incremental separate directory optimization", "[librarymapping]")
{
    // Test case: Verify that adding packages with separate directories maintains optimization

    // Start with boost-algorithm
    std::vector<std::string> algorithm_paths = {"/usr/local/include/boost/algorithm/string.hpp",
                                                "/usr/local/include/boost/algorithm/searching.hpp"};
    std::map<std::string, std::vector<std::string>> packages_stage1 = {{"boost-algorithm", algorithm_paths}};
    auto algorithm_result1 = optimize_header_paths(algorithm_paths, packages_stage1, "boost-algorithm");

    // Should get algorithm directory
    REQUIRE(algorithm_result1.directories.size() == 1);
    CHECK(algorithm_result1.directories[0] == "/usr/local/include/boost/algorithm/");

    // Add boost-format with separate directory structure
    std::vector<std::string> format_paths = {"/usr/local/include/boost/format.hpp",
                                             "/usr/local/include/boost/format/format.hpp"};
    std::map<std::string, std::vector<std::string>> packages_stage2 = {{"boost-algorithm", algorithm_paths},
                                                                       {"boost-format", format_paths}};

    // Re-optimize - both should maintain their separate optimizations
    auto algorithm_result2 = optimize_header_paths(algorithm_paths, packages_stage2, "boost-algorithm");
    auto format_result = optimize_header_paths(format_paths, packages_stage2, "boost-format");

    // boost-algorithm should still own its directory
    REQUIRE(algorithm_result2.directories.size() == 1);
    CHECK(algorithm_result2.directories[0] == "/usr/local/include/boost/algorithm/");
    CHECK(algorithm_result2.individual_files.empty());

    // boost-format should get its directory plus the root file
    REQUIRE(format_result.directories.size() == 1);
    CHECK(format_result.directories[0] == "/usr/local/include/boost/format/");
    REQUIRE(format_result.individual_files.size() == 1);
    CHECK(format_result.individual_files[0] == "/usr/local/include/boost/format.hpp");
}

TEST_CASE ("librarymapping optimization - global directory conflict resolution", "[librarymapping]")
{
    // Test case: Verify that multiple packages claiming same directory are resolved to individual files

    // Three packages all wanting to claim /usr/local/include/boost/ directory
    std::vector<std::string> boost_array_paths = {"/usr/local/include/boost/array.hpp"};
    std::vector<std::string> boost_assert_paths = {"/usr/local/include/boost/assert.hpp",
                                                   "/usr/local/include/boost/current_function.hpp"};
    std::vector<std::string> boost_static_paths = {"/usr/local/include/boost/static_assert.hpp"};

    std::map<std::string, std::vector<std::string>> all_packages = {{"boost-array", boost_array_paths},
                                                                    {"boost-assert", boost_assert_paths},
                                                                    {"boost-static-assert", boost_static_paths}};

    // Each package individually would claim /boost/ directory, but conflict resolution should prevent this
    auto array_result = optimize_header_paths(boost_array_paths, all_packages, "boost-array");
    auto assert_result = optimize_header_paths(boost_assert_paths, all_packages, "boost-assert");
    auto static_result = optimize_header_paths(boost_static_paths, all_packages, "boost-static-assert");

    // All packages should fall back to individual files since they conflict at /boost/ level
    CHECK(array_result.directories.empty());
    REQUIRE(array_result.individual_files.size() == 1);
    CHECK(array_result.individual_files[0] == "/usr/local/include/boost/array.hpp");

    CHECK(assert_result.directories.empty());
    REQUIRE(assert_result.individual_files.size() == 2);
    CHECK(assert_result.individual_files[0] == "/usr/local/include/boost/assert.hpp");
    CHECK(assert_result.individual_files[1] == "/usr/local/include/boost/current_function.hpp");

    CHECK(static_result.directories.empty());
    REQUIRE(static_result.individual_files.size() == 1);
    CHECK(static_result.individual_files[0] == "/usr/local/include/boost/static_assert.hpp");
}
