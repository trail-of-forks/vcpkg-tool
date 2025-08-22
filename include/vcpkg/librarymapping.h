#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    /// Generate library mapping file for all installed packages on the specified triplet
    /// The mapping file contains header-to-library relationships in CycloneDX MLIR format
    /// and is written to installed/vcpkg/library-mappings-<triplet>.txt
    /// @param paths VcpkgPaths instance providing filesystem and directory information
    /// @param triplet Target triplet to generate mappings for
    void regenerate_library_mappings_file(const VcpkgPaths& paths, Triplet triplet);
}