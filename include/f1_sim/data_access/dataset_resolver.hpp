#ifndef F1_SIM_DATA_ACCESS_DATASET_RESOLVER_HPP
#define F1_SIM_DATA_ACCESS_DATASET_RESOLVER_HPP

#include "f1_sim/data_access/diagnostic.hpp"

#include <filesystem>
#include <string>

namespace f1_sim {
namespace data_access {

/**
 * @brief Validated filesystem location for a runtime dataset beneath the configured data root.
 */
struct ResolvedDatasetPath {
    std::filesystem::path absolute_path;
    std::string relative_path; // Normalized generic-format path relative to data_root
};

/**
 * @brief Resolves logical dataset identifiers (event_id, track_id + layout_version,
 *        track_id + surface_version, season_year + spec_version) to local JSON files
 *        strictly confined beneath a configured read-only data root.
 *
 * Security & Architectural Guarantees:
 *  - Rejects empty identifiers, path separators ('/', '\\'), traversal tokens (".."),
 *    or non-identifier characters before touching the filesystem.
 *  - Canonicalizes both the configured data_root and target files so that symlinks
 *    or relative segments escaping data_root are rejected with PathOutsideDataRoot.
 *  - Restricts resolution to the runtime data subdirectories ("tracks", "events",
 *    "compounds"), never scanning "research/", "docs/", ".git/", or build directories.
 *  - Track- and event-agnostic: uses the repository's naming conventions
 *    (e.g., tracks/{track_id}_layout_v{layout_version}.json) so new circuits and
 *    events work without code changes.
 */
class DatasetResolver {
public:
    explicit DatasetResolver(std::filesystem::path data_root);

    bool isRootValid() const noexcept { return root_valid_; }
    const std::filesystem::path& configuredRoot() const noexcept { return configured_root_; }
    const std::filesystem::path& canonicalRoot() const noexcept { return canonical_root_; }
    const std::optional<Diagnostic>& rootError() const noexcept { return root_error_; }

    /**
     * @brief Validate that a logical identifier (such as track_id or event_id)
     *        contains only safe identifier characters [a-z0-9_] and no path traversal.
     */
    static Result<void> validateIdentifier(
        const std::string& identifier,
        const std::string& param_name,
        const std::string& dataset_context);

    Result<ResolvedDatasetPath> resolveTrackLayout(
        const std::string& track_id,
        int layout_version) const;

    Result<ResolvedDatasetPath> resolveTrackSurface(
        const std::string& track_id,
        int surface_version) const;

    Result<ResolvedDatasetPath> resolveEventConfiguration(
        const std::string& event_id) const;

    Result<ResolvedDatasetPath> resolveEventRoleMapping(
        const std::string& event_id) const;

    Result<ResolvedDatasetPath> resolveCompoundCatalogue(
        int season_year,
        int spec_version,
        const std::string& catalogue_slug = "pirelli_slick") const;

    /**
     * @brief Confine an arbitrary relative path under data_root, verifying that it does
     *        not escape data_root via '..' or symlinks and resides in an allowed runtime
     *        subdirectory ("tracks", "events", "compounds").
     */
    Result<ResolvedDatasetPath> verifyRelativePathUnderRoot(
        const std::filesystem::path& relative_candidate,
        const std::string& dataset_id) const;

private:
    std::filesystem::path configured_root_;
    std::filesystem::path canonical_root_;
    bool root_valid_ = false;
    std::optional<Diagnostic> root_error_;
};

} // namespace data_access
} // namespace f1_sim

#endif // F1_SIM_DATA_ACCESS_DATASET_RESOLVER_HPP