# F1 Simulator — Data Access Layer (DAL) Architecture and Usage Guide

## 1. Architectural Overview and Boundary Rules

The F1 Simulator Data Access Layer (`f1_sim::data_access`) enforces a strict, local, read-only boundary between the Simulation Engine and the repository's JSON datasets under `data/`:

- **No Direct JSON in the Simulation Engine:** The Simulation Engine interacts exclusively with strongly typed C++17 domain structs (`EventBundle`, `TrackLayoutVersion`, `TrackSurfaceProfile`, `EventConfiguration`, `EventRoleMapping`, `PhysicalCompoundCatalogue`, `PhysicalCompoundSpec`, and `ProvenanceMetadata`). The internal RFC 8259 JSON parser (`f1_sim::data_access::detail`) is never exposed in public headers.
- **Explicit Local Data Root & Path Confinement:** `DataAccessLayer` requires an explicit `data_root` directory path at construction. `DatasetResolver` validates all logical identifiers against `[a-z0-9_]+`, blocks `..` traversal and absolute paths, canonicalizes targets via `std::filesystem::weakly_canonical`, and enforces that every resolved file resides inside `data_root/{tracks,events,compounds}`. Research notes (`research/`), documentation (`docs/`), and version-control metadata (`.git/`) are never read as runtime datasets.
- **Honest Representation of Unknowns (`std::optional<T>`):** Nullable fields—such as telemetry-dependent sector lengths (`sector1_length_m`..`sector3_length_m`), speed-band fractions (`frac_lap_*`), `bumpiness_index_score`, and uncalibrated compound model parameters (`pace_offset_ref_s`, `base_wear_rate_per_lap`, `linear_deg_coeff_s`, `opt_temp_window_*`)—are stored as `std::optional<double>` (`std::nullopt` when `null` in JSON). They are **never** silently coerced to `0.0`. Verified physical or definitional zeros (such as `track_evolution_amplitude_s = 0.0` or reference compound `C3`'s `pace_offset_ref_s = 0.0`) remain `std::optional<double>{0.0}` or `0.0`.
- **Two-Way Diagnostics Without Autonomous Repair:** Every DAL call returns a `Result<T>` containing a `LoadStatus`, an optional `T`, and a vector of `Diagnostic` records. Additionally, the Engine can report runtime discrepancies via `DataAccessLayer::reportEngineDiscrepancy(const EngineDiscrepancyReport&)`, which records structured diagnostics into the shared `DiagnosticSink` (`InMemoryDiagnosticCollector`) without modifying files, inventing defaults, or retrying failed operations.

## 2. Dataset Resolution Conventions and Extensibility

`DatasetResolver` maps domain identifiers to repository-relative paths beneath `data_root` without hard-coding Silverstone or the 2026 British Grand Prix:

| Domain Entity | Query Key(s) | Resolved Path Relative to `data_root` |
| :--- | :--- | :--- |
| `TrackLayoutVersion` | `track_id`, `layout_version` | `tracks/{track_id}_layout_v{layout_version}.json` |
| `TrackSurfaceProfile` | `track_id`, `surface_version` | `tracks/{track_id}_surface_v{surface_version}.json` |
| `EventConfiguration` | `event_id` | `events/{event_id}_config.json` |
| `EventRoleMapping` | `event_id` | `events/{event_id}_role_mapping.json` |
| `PhysicalCompoundCatalogue` | `season_year`, `spec_version` | `compounds/{season_year}_pirelli_slick_catalogue_v{spec_version}.json` (or single matching `compounds/{season_year}_*_catalogue_v{spec_version}.json`) |

Adding a new circuit or Grand Prix weekend requires only placing the corresponding JSON files under `data/tracks/` and `data/events/` following this naming convention.

## 3. Simulation Engine Usage Example

```cpp
#include "f1_sim/data_access/data_access_layer.hpp"
#include <iostream>

int runSimulationSetupExample(const std::filesystem::path& repo_data_root) {
    using namespace f1_sim::data_access;

    // 1. Initialize DAL with an explicit local data root and in-memory diagnostic sink
    auto diag_collector = std::make_shared<InMemoryDiagnosticCollector>();
    DataAccessLayer dal(repo_data_root, diag_collector);

    // 2. Load the complete cross-validated EventBundle once during simulation setup
    Result<EventBundle> bundle_res = dal.loadEventBundle("2026_british_gp");
    if (!bundle_res.ok()) {
        std::cerr << "Failed to load event bundle (" << toString(bundle_res.status) << "):\n";
        for (const Diagnostic& d : bundle_res.diagnostics) {
            std::cerr << "  [" << toString(d.code) << "] " << d.relative_path
                      << " (" << d.field_path << "): " << d.message << "\n";
        }
        return 1;
    }

    // 3. Hold the immutable EventBundle snapshot for repeated strategy evaluations
    const EventBundle& snapshot = *bundle_res.value;

    std::cout << "Loaded Event: " << snapshot.event_config.official_event_name
              << " (" << snapshot.event_config.race_distance_laps << " laps, "
              << snapshot.track_layout.lap_length_m << " m/lap)\n";

    // 4. Resolve event tyre roles (HARD, MEDIUM, SOFT) to physical compounds (C1..C5)
    const PhysicalCompoundSpec* soft_spec = snapshot.compoundForRole(TyreRole::Soft);
    if (soft_spec != nullptr) {
        std::cout << "Soft role maps to physical compound "
                  << toString(soft_spec->compound_id)
                  << " (hardness rank " << soft_spec->hardness_rank << ")\n";
    }

    // 5. Check whether the loaded bundle has calibrated coefficients for Stage M0-M2 simulation
    Result<void> suff = dal.checkBaselineSimulationSufficiency(snapshot);
    if (!suff.ok()) {
        std::cout << "Bundle is structurally valid, but Stage M0 calibration is incomplete:\n";
        for (const Diagnostic& d : suff.diagnostics) {
            std::cout << "  [" << toString(d.code) << "] " << d.field_path
                      << ": " << d.message << "\n";
        }
    }

    // 6. Report an Engine-detected runtime observation without mutating source files
    dal.reportEngineDiscrepancy(EngineDiscrepancyReport{
        Severity::Warning,
        DiagnosticCode::EngineReportedDiscrepancy,
        snapshot.event_config.event_id,
        "events/2026_british_gp_config.json",
        "/available_sets_soft",
        "Pre-race Sunday allocation constrained to 3 remaining used Soft sets.",
        ">= 4 new Soft sets",
        "3 used Soft sets"
    });

    return 0;
}
```
