#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>

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
    static constexpr StringLiteral FileExtensionJson = ".json";
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
            Debug::println("Failed to parse SPDX for package ", package_name, " on triplet ", triplet.canonical_name());
        }

        return "Unknown";
    }

    // Extract all header file paths (raw) for a specific package from its .list file
    std::vector<std::string> extract_raw_header_paths(const VcpkgPaths& paths, const BinaryParagraph& package)
    {
        std::vector<std::string> header_paths;

        try
        {
            const auto& fs = paths.get_filesystem();
            const auto triplet = package.spec.triplet();

            // Use the proper API to get the .list file path
            const auto list_path = paths.installed().listfile_path(package);

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
                    package.version = status_paragraph.package.version.text;

                    // Extract license information from SPDX file
                    package.license = extract_license_from_spdx(paths, triplet, package.name);

                    // Extract header paths from package files
                    package.header_paths = extract_raw_header_paths(paths, status_paragraph.package);

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

    // Generate the mapping file content in JSON format
    std::string generate_mapping_content(const std::vector<PackageInfo>& packages)
    {
        Json::Array package_array;

        for (const auto& package : packages)
        {
            Json::Object package_obj;
            package_obj.insert("name", Json::Value::string(package.name));
            package_obj.insert("version", Json::Value::string(package.version));
            package_obj.insert("license", Json::Value::string(package.license));

            Json::Array paths_array;
            for (const auto& header_path : package.header_paths)
            {
                paths_array.push_back(Json::Value::string(header_path));
            }
            package_obj.insert("paths", Json::Value::array(std::move(paths_array)));

            package_array.push_back(Json::Value::object(std::move(package_obj)));
        }

        return Json::stringify(Json::Value::array(std::move(package_array)));
    }
}

namespace vcpkg
{
    // Optimize header paths to find minimal non-overlapping sets
    OptimizedPathSet optimize_header_paths(const std::vector<std::string>& raw_paths,
                                           const std::map<std::string, std::vector<std::string>>& all_packages_paths,
                                           const std::string& package_name)
    {
        OptimizedPathSet result;

        if (raw_paths.empty())
        {
            return result;
        }

        // Separate individual files from directories
        std::vector<std::string> files;
        std::vector<std::string> existing_dirs;

        for (const auto& path : raw_paths)
        {
            if (path.back() == '/' || path.back() == '\\')
            {
                existing_dirs.push_back(path);
            }
            else
            {
                files.push_back(path);
            }
        }

        // Build a map of directories to files they contain
        std::map<std::string, std::vector<std::string>> dir_to_files;

        for (const auto& file : files)
        {
            // Find the directory containing this file
            size_t last_separator = file.find_last_of("/\\");
            if (last_separator != std::string::npos)
            {
                std::string dir_path = file.substr(0, last_separator + 1);
                dir_to_files[dir_path].push_back(file);
            }
            else
            {
                // File in root include directory
                result.individual_files.push_back(file);
            }
        }

        // For each directory, check if we own all files in it (no conflicts with other packages)
        for (const auto& [dir_path, files_in_dir] : dir_to_files)
        {
            bool directory_is_exclusive = true;

            // Check if any other package has files in this directory
            for (const auto& [other_package, other_paths] : all_packages_paths)
            {
                if (other_package == package_name) continue;

                for (const auto& other_path : other_paths)
                {
                    // Skip directory entries for this check
                    if (other_path.back() == '/' || other_path.back() == '\\') continue;

                    // Check if this other file is in our directory
                    if (other_path.find(dir_path) == 0)
                    {
                        directory_is_exclusive = false;
                        break;
                    }
                }

                if (!directory_is_exclusive) break;
            }

            if (directory_is_exclusive)
            {
                // We can represent this directory as a single entry
                result.directories.push_back(dir_path);
            }
            else
            {
                // Add individual files since directory is shared
                for (const auto& file : files_in_dir)
                {
                    result.individual_files.push_back(file);
                }
            }
        }

        // Add any existing directory entries that were explicitly listed
        for (const auto& dir : existing_dirs)
        {
            // Check if this directory is not already covered by our optimized directories
            bool already_covered = false;
            for (const auto& opt_dir : result.directories)
            {
                if (dir.find(opt_dir) == 0 || opt_dir.find(dir) == 0)
                {
                    already_covered = true;
                    break;
                }
            }

            if (!already_covered)
            {
                result.directories.push_back(dir);
            }
        }

        // Final optimization: Remove redundant nested directories within this package
        // Sort directories by length (shorter first) so we can identify parent-child relationships
        std::sort(result.directories.begin(), result.directories.end(), [](const std::string& a, const std::string& b) {
            return a.length() < b.length();
        });

        std::vector<std::string> optimized_directories;
        for (const auto& dir : result.directories)
        {
            bool is_redundant = false;
            for (const auto& parent_dir : optimized_directories)
            {
                // If this directory is a subdirectory of an already included parent directory
                if (dir.find(parent_dir) == 0 && dir.length() > parent_dir.length())
                {
                    is_redundant = true;
                    break;
                }
            }

            if (!is_redundant)
            {
                optimized_directories.push_back(dir);
            }
        }

        result.directories = std::move(optimized_directories);

        return result;
    }

    void regenerate_library_mappings_file(const VcpkgPaths& paths, Triplet triplet)
    {
        const auto& fs = paths.get_filesystem();
        const auto mapping_filename =
            fmt::format("{}-{}{}", FileLibraryMappings, triplet.canonical_name(), FileExtensionJson);
        const auto mapping_path = paths.installed().vcpkg_dir() / mapping_filename;

        try
        {
            // Extract installed packages for this triplet (without header paths optimization yet)
            auto packages = extract_installed_packages(paths, triplet);

            // First pass: collect all raw header paths for all packages
            std::map<std::string, std::vector<std::string>> all_packages_paths;
            for (auto& package : packages)
            {
                if (!package.header_paths.empty()) // Only process packages that have headers
                {
                    all_packages_paths[package.name] = package.header_paths;
                }
            }

            // Second pass: optimize header paths for each package
            std::map<std::string, OptimizedPathSet> optimized_results;
            for (auto& package : packages)
            {
                if (!package.header_paths.empty())
                {
                    optimized_results[package.name] =
                        optimize_header_paths(package.header_paths, all_packages_paths, package.name);
                }
            }

            // Third pass: resolve directory conflicts - if multiple packages claim the same directory,
            // force them all to use individual files instead
            std::map<std::string, std::vector<std::string>> directory_to_packages;
            for (const auto& [pkg_name, opt_result] : optimized_results)
            {
                for (const auto& dir : opt_result.directories)
                {
                    directory_to_packages[dir].push_back(pkg_name);
                }
            }

            // Find conflicted directories and fix them
            for (const auto& [dir_path, claiming_packages] : directory_to_packages)
            {
                if (claiming_packages.size() > 1)
                {
                    // Multiple packages claim this directory - force all to use individual files
                    for (const auto& pkg_name : claiming_packages)
                    {
                        auto& opt_result = optimized_results[pkg_name];

                        // Remove the conflicted directory
                        opt_result.directories.erase(
                            std::remove(opt_result.directories.begin(), opt_result.directories.end(), dir_path),
                            opt_result.directories.end());

                        // Add back individual files for this package that were in that directory
                        const auto& raw_paths = all_packages_paths[pkg_name];
                        for (const auto& path : raw_paths)
                        {
                            // Skip directories in raw paths
                            if (path.back() == '/' || path.back() == '\\') continue;

                            // If this file was in the conflicted directory, add it as individual file
                            if (path.find(dir_path) == 0)
                            {
                                // Make sure it's not already in individual_files
                                if (std::find(opt_result.individual_files.begin(),
                                              opt_result.individual_files.end(),
                                              path) == opt_result.individual_files.end())
                                {
                                    opt_result.individual_files.push_back(path);
                                }
                            }
                        }
                    }
                }
            }

            // Fourth pass: apply the resolved optimizations to packages
            for (auto& package : packages)
            {
                if (!package.header_paths.empty())
                {
                    const auto& optimized = optimized_results[package.name];

                    // Convert OptimizedPathSet back to vector<string> for compatibility
                    package.header_paths.clear();
                    package.header_paths.insert(
                        package.header_paths.end(), optimized.directories.begin(), optimized.directories.end());
                    package.header_paths.insert(package.header_paths.end(),
                                                optimized.individual_files.begin(),
                                                optimized.individual_files.end());
                }
            }

            // Generate mapping file content
            const auto content = generate_mapping_content(packages);

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
