#include "f1_sim/data_access/dataset_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <system_error>
#include <vector>

namespace f1_sim {
namespace data_access {

namespace {

bool containsPathTraversalOrSeparator(const std::string& s) noexcept {
    if (s.find("..") != std::string::npos) {
        return true;
    }
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':') {
            return true;
        }
    }
    return false;
}

bool isCanonicalSubpathOf(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) noexcept {
    auto parent_it = parent.begin();
    auto child_it = child.begin();
    for (; parent_it != parent.end(); ++parent_it, ++child_it) {
        if (child_it == child.end() || *child_it != *parent_it) {
            return false;
        }
    }
    return true;
}

bool isAllowedRuntimeSubdirectory(const std::string& top_dir) noexcept {
    return top_dir == "tracks" || top_dir == "events" || top_dir == "compounds";
}

} // namespace

DatasetResolver::DatasetResolver(std::filesystem::path data_root)
    : configured_root_(std::move(data_root)) {
    if (configured_root_.empty()) {
        root_error_ = Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRequestIdentifier,
            "Configured data_root path is empty; an explicit local data directory is required.",
            "data_root",
            "",
            "",
            "Non-empty existing directory path",
            "<empty>"
        };
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(configured_root_, ec) || ec) {
        root_error_ = Diagnostic{
            Severity::Error,
            DiagnosticCode::DatasetFileNotFound,
            "Configured data_root directory does not exist.",
            "data_root",
            "",
            "",
            "Existing local data directory",
            configured_root_.filename().generic_string()
        };
        return;
    }

    if (!std::filesystem::is_directory(configured_root_, ec) || ec) {
        root_error_ = Diagnostic{
            Severity::Error,
            DiagnosticCode::FileReadPermissionError,
            "Configured data_root is not a directory.",
            "data_root",
            "",
            "",
            "Directory",
            configured_root_.filename().generic_string()
        };
        return;
    }

    canonical_root_ = std::filesystem::canonical(configured_root_, ec);
    if (ec || canonical_root_.empty()) {
        root_error_ = Diagnostic{
            Severity::Error,
            DiagnosticCode::FileReadPermissionError,
            "Failed to canonicalize configured data_root directory.",
            "data_root",
            "",
            "",
            "Canonicalizable directory path",
            configured_root_.filename().generic_string()
        };
        return;
    }

    root_valid_ = true;
}

Result<void> DatasetResolver::validateIdentifier(
    const std::string& identifier,
    const std::string& param_name,
    const std::string& dataset_context) {
    if (identifier.empty()) {
        return Result<void>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Requested identifier '" + param_name + "' must not be empty.",
                dataset_context,
                "",
                param_name,
                "Non-empty identifier matching [a-z0-9_]+",
                "<empty>"
            }});
    }

    if (containsPathTraversalOrSeparator(identifier)) {
        return Result<void>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Identifier '" + param_name + "' contains path separator or traversal sequence.",
                dataset_context,
                "",
                param_name,
                "Simple identifier [a-z0-9_]+ without '/', '\\', or '..'",
                identifier
            }});
    }

    if (identifier.size() > 64) {
        return Result<void>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Identifier '" + param_name + "' exceeds maximum length of 64 characters.",
                dataset_context,
                "",
                param_name,
                "Length in [1, 64]",
                std::to_string(identifier.size())
            }});
    }

    for (char c : identifier) {
        bool valid_char = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
        if (!valid_char) {
            return Result<void>::failure(
                LoadStatus::InvalidRequest,
                {Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InvalidRequestIdentifier,
                    "Identifier '" + param_name + "' contains invalid character(s); only [a-z0-9_] are permitted.",
                    dataset_context,
                    "",
                    param_name,
                    "[a-z0-9_]+",
                    identifier
                }});
        }
    }

    return Result<void>::success();
}

Result<ResolvedDatasetPath> DatasetResolver::verifyRelativePathUnderRoot(
    const std::filesystem::path& relative_candidate,
    const std::string& dataset_id) const {
    if (!root_valid_) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::FileNotFound,
            {root_error_.value_or(Diagnostic{
                Severity::Error,
                DiagnosticCode::DatasetFileNotFound,
                "Configured data_root is invalid.",
                dataset_id,
                "",
                "",
                "",
                ""
            })});
    }

    if (relative_candidate.empty()) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Relative dataset path cannot be empty.",
                dataset_id,
                "",
                "",
                "Non-empty relative path under data_root",
                "<empty>"
            }});
    }

    if (relative_candidate.is_absolute()) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Absolute paths are prohibited; dataset path must be relative to data_root.",
                dataset_id,
                "",
                "",
                "Relative path under data_root/{tracks,events,compounds}",
                relative_candidate.filename().generic_string()
            }});
    }

    for (const auto& part : relative_candidate) {
        const std::string token = part.generic_string();
        if (token == "..") {
            return Result<ResolvedDatasetPath>::failure(
                LoadStatus::PathOutsideRoot,
                {Diagnostic{
                    Severity::Error,
                    DiagnosticCode::PathOutsideDataRoot,
                    "Parent-directory traversal ('..') is forbidden in dataset paths.",
                    dataset_id,
                    relative_candidate.generic_string(),
                    "",
                    "Path strictly within data_root",
                    relative_candidate.generic_string()
                }});
        }
    }

    std::filesystem::path normalized_rel = relative_candidate.lexically_normal();
    if (normalized_rel.empty() || *normalized_rel.begin() == "..") {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Normalized dataset path escapes configured data_root.",
                dataset_id,
                relative_candidate.generic_string(),
                "",
                "Path strictly within data_root",
                normalized_rel.generic_string()
            }});
    }

    const std::string top_dir = normalized_rel.begin()->generic_string();
    if (!isAllowedRuntimeSubdirectory(top_dir)) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Dataset path resolves outside allowed runtime subdirectories (tracks, events, compounds).",
                dataset_id,
                normalized_rel.generic_string(),
                "",
                "Subdirectory in {tracks, events, compounds}",
                top_dir
            }});
    }

    std::filesystem::path combined = canonical_root_ / normalized_rel;
    std::error_code ec;
    std::filesystem::path weak_canon = std::filesystem::weakly_canonical(combined, ec);
    if (ec || !isCanonicalSubpathOf(weak_canon, canonical_root_) || weak_canon == canonical_root_) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Resolved target path escapes configured data_root (e.g., via symlink or relative segment).",
                dataset_id,
                normalized_rel.generic_string(),
                "",
                "Target inside configured data_root",
                normalized_rel.generic_string()
            }});
    }

    std::filesystem::path rel_from_canon = std::filesystem::relative(weak_canon, canonical_root_, ec);
    if (ec || rel_from_canon.empty() || !isAllowedRuntimeSubdirectory(rel_from_canon.begin()->generic_string())) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::PathOutsideRoot,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::PathOutsideDataRoot,
                "Canonical target path escapes allowed runtime subdirectories under data_root.",
                dataset_id,
                normalized_rel.generic_string(),
                "",
                "Canonical target under data_root/{tracks,events,compounds}",
                normalized_rel.generic_string()
            }});
    }

    return Result<ResolvedDatasetPath>::success(ResolvedDatasetPath{
        weak_canon,
        rel_from_canon.generic_string()
    });
}

Result<ResolvedDatasetPath> DatasetResolver::resolveTrackLayout(
    const std::string& track_id,
    int layout_version) const {
    const std::string dataset_id = track_id + "_layout_v" + std::to_string(layout_version);
    auto id_ok = validateIdentifier(track_id, "track_id", dataset_id);
    if (!id_ok.ok()) {
        return Result<ResolvedDatasetPath>::failure(id_ok.status, std::move(id_ok.diagnostics));
    }
    if (layout_version < 1) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "layout_version must be a positive integer (>= 1).",
                dataset_id,
                "",
                "layout_version",
                ">= 1",
                std::to_string(layout_version)
            }});
    }

    std::filesystem::path rel = std::filesystem::path("tracks") /
        (track_id + "_layout_v" + std::to_string(layout_version) + ".json");
    return verifyRelativePathUnderRoot(rel, dataset_id);
}

Result<ResolvedDatasetPath> DatasetResolver::resolveTrackSurface(
    const std::string& track_id,
    int surface_version) const {
    const std::string dataset_id = track_id + "_surface_v" + std::to_string(surface_version);
    auto id_ok = validateIdentifier(track_id, "track_id", dataset_id);
    if (!id_ok.ok()) {
        return Result<ResolvedDatasetPath>::failure(id_ok.status, std::move(id_ok.diagnostics));
    }
    if (surface_version < 1) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "surface_version must be a positive integer (>= 1).",
                dataset_id,
                "",
                "surface_version",
                ">= 1",
                std::to_string(surface_version)
            }});
    }

    std::filesystem::path rel = std::filesystem::path("tracks") /
        (track_id + "_surface_v" + std::to_string(surface_version) + ".json");
    return verifyRelativePathUnderRoot(rel, dataset_id);
}

Result<ResolvedDatasetPath> DatasetResolver::resolveEventConfiguration(
    const std::string& event_id) const {
    auto id_ok = validateIdentifier(event_id, "event_id", event_id);
    if (!id_ok.ok()) {
        return Result<ResolvedDatasetPath>::failure(id_ok.status, std::move(id_ok.diagnostics));
    }

    std::filesystem::path rel = std::filesystem::path("events") / (event_id + "_config.json");
    return verifyRelativePathUnderRoot(rel, event_id);
}

Result<ResolvedDatasetPath> DatasetResolver::resolveEventRoleMapping(
    const std::string& event_id) const {
    const std::string dataset_id = event_id + "_role_mapping";
    auto id_ok = validateIdentifier(event_id, "event_id", dataset_id);
    if (!id_ok.ok()) {
        return Result<ResolvedDatasetPath>::failure(id_ok.status, std::move(id_ok.diagnostics));
    }

    std::filesystem::path rel = std::filesystem::path("events") / (event_id + "_role_mapping.json");
    return verifyRelativePathUnderRoot(rel, dataset_id);
}

Result<ResolvedDatasetPath> DatasetResolver::resolveCompoundCatalogue(
    int season_year,
    int spec_version,
    const std::string& catalogue_slug) const {
    const std::string dataset_id =
        std::to_string(season_year) + "_compound_catalogue_v" + std::to_string(spec_version);
    if (season_year < 2018 || season_year > 2035) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Requested season_year is outside supported range [2018, 2035].",
                dataset_id,
                "",
                "season_year",
                "[2018, 2035]",
                std::to_string(season_year)
            }});
    }
    if (spec_version < 1) {
        return Result<ResolvedDatasetPath>::failure(
            LoadStatus::InvalidRequest,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Requested spec_version must be >= 1.",
                dataset_id,
                "",
                "spec_version",
                ">= 1",
                std::to_string(spec_version)
            }});
    }

    auto slug_ok = validateIdentifier(catalogue_slug, "catalogue_slug", dataset_id);
    if (!slug_ok.ok()) {
        return Result<ResolvedDatasetPath>::failure(slug_ok.status, std::move(slug_ok.diagnostics));
    }

    std::filesystem::path primary_rel = std::filesystem::path("compounds") /
        (std::to_string(season_year) + "_" + catalogue_slug + "_catalogue_v" +
         std::to_string(spec_version) + ".json");
    auto primary_res = verifyRelativePathUnderRoot(primary_rel, dataset_id);
    if (!primary_res.ok()) {
        return primary_res;
    }

    std::error_code ec;
    if (std::filesystem::exists(primary_res.value->absolute_path, ec) && !ec) {
        return primary_res;
    }

    std::filesystem::path compounds_dir = canonical_root_ / "compounds";
    if (std::filesystem::exists(compounds_dir, ec) && std::filesystem::is_directory(compounds_dir, ec)) {
        const std::string prefix = std::to_string(season_year) + "_";
        const std::string suffix = "_catalogue_v" + std::to_string(spec_version) + ".json";
        std::vector<std::string> matching_filenames;

        for (const auto& entry : std::filesystem::directory_iterator(compounds_dir, ec)) {
            if (ec) {
                break;
            }
            const std::string fname = entry.path().filename().generic_string();
            if (fname.size() > prefix.size() + suffix.size() &&
                fname.compare(0, prefix.size(), prefix) == 0 &&
                fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
                matching_filenames.push_back(fname);
            }
        }
        std::sort(matching_filenames.begin(), matching_filenames.end());
        if (matching_filenames.size() == 1) {
            return verifyRelativePathUnderRoot(
                std::filesystem::path("compounds") / matching_filenames.front(),
                dataset_id);
        }
    }

    return primary_res;
}

} // namespace data_access
} // namespace f1_sim