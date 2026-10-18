#ifndef F1_SIM_DATA_ACCESS_DATA_ACCESS_LAYER_HPP
#define F1_SIM_DATA_ACCESS_DATA_ACCESS_LAYER_HPP

#include "f1_sim/data_access/dataset_resolver.hpp"
#include "f1_sim/data_access/diagnostic.hpp"
#include "f1_sim/data_access/domain_models.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace f1_sim {
namespace data_access {

/**
 * @brief Primary Data Access Layer (DAL) facade between the F1 Simulation Engine
 *        and local repository JSON data files.
 *
 * Core Responsibilities and Guarantees:
 *  1. Read-Only Local Boundary: Reads only from the explicitly configured local
 *     data root (`data/tracks`, `data/events`, `data/compounds`). Never modifies,
 *     creates, formats, or deletes any file on disk, and never makes network calls.
 *  2. Typed Domain API: Parses and validates JSON into strongly typed C++ domain
 *     models (`EventBundle`, `TrackLayoutVersion`, `TrackSurfaceProfile`,
 *     `EventConfiguration`, `EventRoleMapping`, `PhysicalCompoundCatalogue`).
 *     Parser-native JSON structures remain hidden inside the implementation.
 *  3. Honest Unknown Handling: Preserves `null` / `UNKNOWN_UNAVAILABLE` fields as
 *     `std::optional<T>` (`std::nullopt`) with `LegitimateUnknownValue` warnings.
 *     Never silently substitutes `0.0` or invented defaults.
 *  4. Staged Validation: Distinguishes invalid identifiers, out-of-root paths,
 *     missing files, I/O errors, malformed JSON, unsupported schema versions,
 *     structural schema failures, semantic range/invariant errors, and broken
 *     cross-dataset references.
 *  5. Two-Way Diagnostics Without Auto-Repair: Accepts an optional `DiagnosticSink`
 *     (defaulting to an `InMemoryDiagnosticCollector`) so the Simulation Engine can
 *     report runtime discrepancies via `reportEngineDiscrepancy(...)` without
 *     triggering autonomous file edits, recalibration, or retry loops.
 */
class DataAccessLayer {
public:
    explicit DataAccessLayer(
        std::filesystem::path data_root,
        std::shared_ptr<DiagnosticSink> diagnostic_sink = std::make_shared<InMemoryDiagnosticCollector>());

    /**
     * @brief Load and cross-validate a complete, immutable EventBundle for `event_id`.
     *
     * Resolves and loads:
     *  - EventConfiguration (`events/{event_id}_config.json`)
     *  - EventRoleMapping (`events/{event_id}_role_mapping.json`)
     *  - TrackLayoutVersion (`tracks/{track_id}_layout_v{layout_version}.json`)
     *  - TrackSurfaceProfile (`tracks/{track_id}_surface_v{surface_version}.json`)
     *  - PhysicalCompoundCatalogue (`compounds/{season_year}_*_catalogue_v{spec_version}.json`)
     *
     * Validates all foreign keys, lap-distance geometry consistency, and the four
     * mathematical invariants of Section 7.2 on the role mapping m_e : {S,M,H} -> N_e.
     *
     * Note: Uncalibrated (`null`) compound parameters do NOT cause `loadEventBundle`
     * to fail; they are surfaced as `LegitimateUnknownValue` warnings on the returned
     * `Result<EventBundle>` and can be queried via `checkBaselineSimulationSufficiency`.
     */
    Result<EventBundle> loadEventBundle(const std::string& event_id) const;

    /**
     * @brief Load an individual TrackLayoutVersion by track_id and layout_version.
     */
    Result<TrackLayoutVersion> loadTrackLayout(
        const std::string& track_id,
        int layout_version) const;

    /**
     * @brief Load an individual TrackSurfaceProfile by track_id and surface_version.
     *        If `expected_layout_version` is supplied, verifies foreign-key match.
     */
    Result<TrackSurfaceProfile> loadTrackSurface(
        const std::string& track_id,
        int surface_version,
        std::optional<int> expected_layout_version = std::nullopt) const;

    /**
     * @brief Load an individual EventConfiguration by event_id.
     */
    Result<EventConfiguration> loadEventConfiguration(
        const std::string& event_id) const;

    /**
     * @brief Load an individual EventRoleMapping by event_id.
     */
    Result<EventRoleMapping> loadEventRoleMapping(
        const std::string& event_id) const;

    /**
     * @brief Load a PhysicalCompoundCatalogue by season_year and spec_version.
     */
    Result<PhysicalCompoundCatalogue> loadCompoundCatalogue(
        int season_year,
        int spec_version = 1) const;

    /**
     * @brief Load a single PhysicalCompoundSpec (C1..C5) from the specified catalogue.
     */
    Result<PhysicalCompoundSpec> loadCompoundSpec(
        int season_year,
        int spec_version,
        PhysicalCompoundId compound_id) const;

    /**
     * @brief Evaluate whether a loaded EventBundle contains all calibrated parameters
     *        required to run a Stage M0-M2 baseline race-time simulation.
     *
     * Returns `LoadStatus::Success` if all three nominated compounds have non-null
     * `pace_offset_ref_s`, `base_wear_rate_per_lap`, and `linear_deg_coeff_s`.
     * Otherwise returns `LoadStatus::InsufficientCapabilityData` with structured
     * `InsufficientDataForCapability` diagnostics naming every uncalibrated field.
     */
    Result<void> checkBaselineSimulationSufficiency(const EventBundle& bundle) const;

    /**
     * @brief Record a runtime discrepancy or invariant violation observed by the
     *        Simulation Engine.
     *
     * Appends a structured `Diagnostic` to the configured `DiagnosticSink` and
     * returns it to the caller. Performs NO file writes, NO value substitution,
     * and NO automatic retries.
     */
    Diagnostic reportEngineDiscrepancy(const EngineDiscrepancyReport& report) const;

    /**
     * @brief Access the configured diagnostic sink (e.g. InMemoryDiagnosticCollector).
     */
    std::shared_ptr<DiagnosticSink> diagnosticSink() const noexcept {
        return diagnostic_sink_;
    }

    /**
     * @brief Access the underlying dataset path resolver.
     */
    const DatasetResolver& resolver() const noexcept {
        return resolver_;
    }

private:
    void emitDiagnostics(const std::vector<Diagnostic>& diagnostics) const;

    DatasetResolver resolver_;
    std::shared_ptr<DiagnosticSink> diagnostic_sink_;
};

} // namespace data_access
} // namespace f1_sim

#endif // F1_SIM_DATA_ACCESS_DATA_ACCESS_LAYER_HPP