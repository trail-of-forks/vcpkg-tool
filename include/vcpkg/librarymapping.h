#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <map>
#include <string>
#include <vector>

namespace vcpkg
{
    /// Structure to hold optimized path information
    struct OptimizedPathSet
    {
        std::vector<std::string> directories;      // Complete directories owned by this package
        std::vector<std::string> individual_files; // Individual files not covered by directories
    };

    /// Generate library mapping file for all installed packages on the specified triplet
    /// The mapping file contains header-to-library relationships in JSON format
    /// and is written to installed/vcpkg/library-mappings-<triplet>.json
    ///
    /// JSON Schema:
    /// [
    ///   {
    ///     "name": "package_name",
    ///     "version": "version_string",
    ///     "license": "license_identifier",
    ///     "paths": ["header_path1", "header_path2", ...]
    ///   },
    ///   ...
    /// ]
    ///
    /// @param paths VcpkgPaths instance providing filesystem and directory information
    /// @param triplet Target triplet to generate mappings for
    void regenerate_library_mappings_file(const VcpkgPaths& paths, Triplet triplet);

    /// Optimize header paths to find minimal non-overlapping sets
    /// This function analyzes the raw paths and produces an optimized representation
    /// that avoids redundant entries while handling inter-package conflicts
    /// @param raw_paths The original list of header paths for this package
    /// @param all_packages_paths Map of all packages to their header paths for conflict detection
    /// @param package_name Name of the package being optimized
    /// @return OptimizedPathSet containing minimal directories and individual files
    OptimizedPathSet optimize_header_paths(const std::vector<std::string>& raw_paths,
                                           const std::map<std::string, std::vector<std::string>>& all_packages_paths,
                                           const std::string& package_name);
}
