# F1_Sim

A local-first Formula 1 race strategy simulator in C++, built around an explicit mathematical framework, versioned race data, and a dedicated Data Access Layer (DAL).

> **Project status:** Early development. The repository contains an initial 2026 Silverstone data batch and a newly generated DAL implementation. The DAL must be built and its tests verified in the target development environment before it is considered integrated and production-ready. Empirical lap-time prediction and tyre-degradation modelling remain limited by uncalibrated or missing parameters.

## Goals

F1_Sim aims to provide a transparent, extensible foundation for exploring Formula 1 race strategy. The intended system will:

- Represent track geometry, track-surface characteristics, event configuration, tyre compounds, and model parameters as separate datasets.
- Keep source data and assumptions traceable through provenance, confidence, and validation metadata.
- Use a local-only Data Access Layer to load and validate internal data files.
- Simulate stint and pit-stop strategies using an explicit mathematical model.
- Eventually compare candidate strategies using a DAG or other optimisation approach.
- Distinguish missing, estimated, assumed, and calibrated values instead of silently treating unknowns as zero.

## Current scope

The initial data batch targets the **2026 British Grand Prix at Silverstone**. Current data includes:

- A physical slick-compound catalogue for Pirelli C1–C5.
- British Grand Prix event configuration and event-specific Hard/Medium/Soft role mapping.
- Separate Silverstone layout and surface datasets.
- Research sources, a validation report, and a batch manifest.
- A mathematical framework document.
- A Data Access Layer implementation and tests generated for the project.

This does **not** mean the simulator can yet produce calibrated, empirically reliable race predictions. Important tyre pace/wear coefficients and some track parameters are currently unknown, estimated, or assumed. Historical telemetry integration and model calibration are separate future tasks.

## Architecture

The intended data flow is:

```text
Simulation Engine
       |
       | Requests an event bundle / dataset
       v
Data Access Layer (DAL)
       |
       | Resolves paths, reads local files,
       | parses, validates, assembles typed models
       v
Local JSON datasets under data/
```

The engine should consume typed C++ domain models rather than open JSON files directly. The DAL returns structured results and diagnostics, including when a file is malformed, a reference is invalid, or a required parameter is unknown. The engine can also report discrepancies back to the DAL for diagnostics.

The DAL is **not** an internet agent, a calibration system, or a data editor. It must not automatically repair, rewrite, or invent source data.

### Repository layout

```text
F1_Sim/
├── data/
│   ├── compounds/       # Physical tyre-compound catalogue
│   ├── events/          # Event configuration and tyre-role mapping
│   └── tracks/          # Track layout and surface datasets
├── docs/
│   ├── f1_simulator_mathematical_framework.md
│   ├── Data_Access_Layer.md
│   └── DAL_Usage_and_Architecture.md
├── include/
│   └── f1_sim/
│       └── data_access/ # Public DAL interfaces and domain models
├── research/
│   ├── manifests/       # Data-batch manifests
│   ├── reports/         # Research and validation reports
│   └── sources/         # Source register
├── src/
│   ├── main.cpp
│   └── data_access/     # DAL implementation and internal JSON parser
├── tests/
│   └── data_access/     # DAL unit and integration tests
├── CMakeLists.txt
└── Makefile
```

Some entries above may be local or newly generated rather than already committed to Git. Check the actual checkout before assuming every listed file is present.

## Data and modelling principles

1. **Separate concerns.** Track geometry, track surface, event rules, physical compounds, event tyre-role mapping, vehicle specifications, and calibrated model parameters are distinct concepts.
2. **Keep unknowns explicit.** Nullable values remain unknown; they must not be silently replaced with `0.0` or another convenient default.
3. **Distinguish evidence levels.** Published/observed, derived, estimated, calibrated, and assumed values should remain distinguishable.
4. **Validate relationships.** A role mapping must refer to compounds that exist; dataset identifiers and cross-file references must be checked.
5. **Preserve provenance.** Where available, retain sources, retrieval dates, validity/version information, derivation methods, uncertainty, and confidence.
6. **Keep runtime local.** The DAL reads files from the local data directory. It does not fetch data from the internet.
7. **Keep data read-only.** The DAL may report data issues, but it must not rewrite, repair, rename, delete, or regenerate source JSON files.
8. **Fail meaningfully.** Distinguish malformed/corrupt data from legitimate unknowns and from insufficient inputs for a particular simulation mode.
9. **Validate only what is required.** A dataset bundle may load successfully even if a particular calculation cannot run without additional calibration parameters.

## Build and test

The DAL currently has both a CMake configuration and a GNU Makefile. Prefer the CMake workflow as the canonical build path if the two configurations are kept consistent.

From the repository root, with a compatible C++17 compiler and CMake installed:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The repository also provides a Makefile; inspect its targets before using it. Build and test commands should be verified on the actual environment. In particular, tests should use portable temporary-directory handling rather than assuming `/tmp` exists on every platform.

The DAL uses C++17 according to its generated build configuration. No successful build or test result is implied by the presence of these files.

## Data integrity

Before committing DAL changes, verify that existing research and data files remain untouched:

```bash
git status --short
git diff -- data/ research/
```

Review any output before committing. The current Silverstone batch manifest contains paths that do not match the repository's tracked locations for the source register and research report. This mismatch is documented as a known issue and should be corrected in a separate, deliberate data-maintenance change—not automatically by the DAL.

## Current limitations and next milestones

- Tyre pace and degradation coefficients are not fully calibrated.
- Some sector measurements and track-surface parameters are unavailable.
- The Silverstone pit-lane length is estimated with low confidence.
- Some track-evolution values are assumptions, not empirical calibration.
- Scenario-environment series, vehicle specifications, and calibrated model-parameter datasets are future extension points.
- The DAL must be built, tested, and reviewed before integration into the simulation engine is considered complete.
- Historical telemetry integration and calibration are future work.

Suggested development order:

1. Verify the DAL build and tests.
2. Review validation, path confinement, and diagnostic behaviour.
3. Integrate the DAL with the simulation engine.
4. Add or validate vehicle and scenario inputs as needed.
5. Integrate suitable historical timing/telemetry data.
6. Calibrate the tyre and track model before interpreting predictions as empirical results.
7. Implement and evaluate strategy search/optimisation.

## Contributing and changing data

Keep code, runtime datasets, research evidence, and model documentation separate. Data changes should be traceable, validated, and reviewed. Do not treat assumed or illustrative framework values as official Pirelli measurements or calibrated simulator coefficients.

## License


