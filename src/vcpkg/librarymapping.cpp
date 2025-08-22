#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/installedpaths.h>
#include <vcpkg/librarymapping.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    // Local constants to avoid modifying contractual-constants.h for easier rebasing
    static constexpr StringLiteral FileLibraryMappings = "library-mappings";
    static constexpr StringLiteral FileExtensionTxt = ".txt";
}

namespace
{
    // Structure to hold package information for mapping generation
    struct PackageInfo
    {
        std::string name;
        std::string version;
        std::string license;
        std::vector<std::string> header_paths;
    };

    // Extract license information from SPDX file for a specific package
    std::string extract_license_from_spdx(const VcpkgPaths& paths, Triplet triplet, const std::string& package_name)
    {
        try
        {
            const auto& fs = paths.get_filesystem();

            // Build SPDX file path: installed/<triplet>/share/<package>/vcpkg.spdx.json
            const auto spdx_path = paths.installed().triplet_dir(triplet) / "share" / package_name / "vcpkg.spdx.json";

            if (!fs.exists(spdx_path, IgnoreErrors{}))
            {
                return "Unknown";
            }

            const auto spdx_contents = fs.read_contents(spdx_path, VCPKG_LINE_INFO);
            const auto spdx_json_opt = Json::parse(spdx_contents, spdx_path);

            if (auto parsed_json = spdx_json_opt.get())
            {
                if (auto spdx_json = parsed_json->value.maybe_object())
                {
                    // Look for packages array and find the port entry
                    if (auto packages_array = spdx_json->get("packages"))
                    {
                        if (auto packages = packages_array->maybe_array())
                        {
                            for (const auto& package : *packages)
                            {
                                if (auto package_obj = package.maybe_object())
                                {
                                    // Look for the port package (not binary)
                                    if (auto spdx_id = package_obj->get("SPDXID"))
                                    {
                                        if (auto id_str = spdx_id->maybe_string())
                                        {
                                            if (*id_str == "SPDXRef-port")
                                            {
                                                if (auto license_concluded = package_obj->get("licenseConcluded"))
                                                {
                                                    if (auto license_str = license_concluded->maybe_string())
                                                    {
                                                        return *license_str;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
            // Return unknown if SPDX parsing fails
        }

        return "Unknown";
    }

    // Extract header file paths for a specific package from its .list file
    std::vector<std::string> extract_header_paths(const VcpkgPaths& paths,
                                                  Triplet triplet,
                                                  const std::string& package_name,
                                                  const std::string& package_version)
    {
        std::vector<std::string> header_paths;

        try
        {
            const auto& fs = paths.get_filesystem();

            // Build the .list file path: installed/vcpkg/info/<package>_<version>_<triplet>.list
            const auto list_filename = fmt::format("{}_{}_{}", package_name, package_version, triplet.canonical_name());
            const auto list_path = paths.installed().vcpkg_dir() / "info" / (list_filename + ".list");

            if (!fs.exists(list_path, IgnoreErrors{}))
            {
                return header_paths; // Return empty vector if .list file doesn't exist
            }

            const auto list_contents = fs.read_contents(list_path, VCPKG_LINE_INFO);
            const auto lines = Strings::split(list_contents, '\n');

            const std::string include_prefix = triplet.canonical_name() + "/include/";

            for (const auto& line : lines)
            {
                const auto trimmed_line = Strings::trim(line);
                if (trimmed_line.empty()) continue;

                // Check if this line is a header file in the include directory
                if (Strings::starts_with(trimmed_line, include_prefix))
                {
                    // Extract the path relative to the triplet's include directory
                    const auto relative_path = trimmed_line.substr(include_prefix.length());

                    // Only include actual files (not directories)
                    if (!relative_path.empty() && !Strings::ends_with(relative_path, "/"))
                    {
                        // Add absolute path for mapping file (including the include directory)
                        const auto absolute_path = paths.installed().triplet_dir(triplet) / "include" / relative_path;
                        header_paths.push_back(absolute_path.generic_u8string());
                    }
                    else if (Strings::ends_with(relative_path, "/"))
                    {
                        // For directories, add with trailing slash (including the include directory)
                        const auto absolute_path = paths.installed().triplet_dir(triplet) / "include" / relative_path;
                        header_paths.push_back(absolute_path.generic_u8string());
                    }
                }
            }
        }
        catch (...)
        {
            // Return empty vector if file reading fails
        }

        return header_paths;
    }

    // Extract installed packages for the given triplet from status database
    std::vector<PackageInfo> extract_installed_packages(const VcpkgPaths& paths, Triplet triplet)
    {
        std::vector<PackageInfo> packages;

        try
        {
            const auto& fs = paths.get_filesystem();
            const StatusParagraphs status_db = database_load(fs, paths.installed());

            for (const auto& status_paragraph_ptr : status_db)
            {
                const auto& status_paragraph = *status_paragraph_ptr;

                // Only process installed packages for this triplet that are not features
                if (status_paragraph.is_installed() && status_paragraph.package.spec.triplet() == triplet &&
                    !status_paragraph.package.is_feature())
                {
                    PackageInfo package;
                    package.name = status_paragraph.package.spec.name();
                    package.version = status_paragraph.package.version.to_string();

                    // Extract license information from SPDX file
                    package.license = extract_license_from_spdx(paths, triplet, package.name);

                    // Extract header paths from package files
                    package.header_paths = extract_header_paths(paths, triplet, package.name, package.version);

                    // Only include packages that have header files
                    if (!package.header_paths.empty())
                    {
                        packages.push_back(std::move(package));
                    }
                }
            }
        }
        catch (...)
        {
            // Return empty vector if status database reading fails
        }

        return packages;
    }

    // Generate the mapping file content in CycloneDX MLIR format
    std::string generate_mapping_content(const std::vector<PackageInfo>& packages, Triplet triplet)
    {
        std::string content;
        content.append("# Generated by vcpkg\n");
        content.append("# Library mapping file for triplet: ");
        content.append(triplet.canonical_name());
        content.append("\n\n");

        for (const auto& package : packages)
        {
            content.append("Name: ");
            content.append(package.name);
            content.append("\n");

            content.append("Version: ");
            content.append(package.version);
            content.append("\n");

            content.append("License: ");
            content.append(package.license);
            content.append("\n");

            for (const auto& header_path : package.header_paths)
            {
                content.append(header_path);
                content.append("\n");
            }

            content.append("\n"); // Empty line between packages
        }

        return content;
    }
}

namespace vcpkg
{
    void regenerate_library_mappings_file(const VcpkgPaths& paths, Triplet triplet)
    {
        const auto& fs = paths.get_filesystem();
        const auto mapping_filename =
            fmt::format("{}-{}{}", FileLibraryMappings, triplet.canonical_name(), FileExtensionTxt);
        const auto mapping_path = paths.installed().vcpkg_dir() / mapping_filename;

        try
        {
            // Extract installed packages for this triplet
            const auto packages = extract_installed_packages(paths, triplet);

            // Generate mapping file content
            const auto content = generate_mapping_content(packages, triplet);

            // Write to file
            fs.write_contents(mapping_path, content, VCPKG_LINE_INFO);
        }
        catch (...)
        {
            // Don't fail the main operation if mapping generation fails
            // Silently ignore errors for now to avoid modifying message files for easier rebasing
        }
    }
}