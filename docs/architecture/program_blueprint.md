# F1_Sim — Program-Wide Digital Blueprint & Reference Architecture

**Document Path:** `docs/architecture/program_blueprint.md`

**Project Repository:** `https://github.com/sm9026/F1_Sim.git` (local workspace snapshot)

**Target Standard & Toolchain:** ISO C++17 (`-std=c++17 -Wall -Wextra -Wpedantic -Werror`), GNU Make / CMake

**Mathematical Authority:** `docs/f1_simulator_mathematical_framework.md` (Version `1.0.0`)

## 1. Executive Summary and Purpose of `F1_Sim`

`F1_Sim` is a deterministic, data-driven Formula 1 single-car race strategy simulator written in C++17. Its primary engineering goal is to evaluate and compare tyre-stint race strategies across a fixed Grand Prix distance of $L$ laps by integrating static circuit geometry, asphalt surface characteristics, Pirelli's physical dry slick compound catalogue (`C1`–`C5`), event-specific tyre role nominations (`SOFT`, `MEDIUM`, `HARD`), fuel-burn mass trajectories, environmental conditions, and pit-stop time-loss accounting under a single mathematical framework.

### Core Architectural Principles

1. **Strict Separation of Layers:** The interactive CLI (`f1_sim::cli`) and future Web UI / API Adapter are completely decoupled from the C++ numerical engine (`f1_sim::simulation`), which in turn is decoupled from raw filesystem/JSON operations by a read-only local Data Access Layer (`f1_sim::data_access`).

2. **Single Mathematical Source of Truth:** All lap-time decompositions (Equations 6.1–6.4), tyre health transitions (Equations 7.1–7.6), environmental multipliers (Equations 8.1–8.2), and pit-stop loss calculations (Equations 9.1–9.2) live in a single stateless evaluator (`LapStintEvaluator`) shared across Preplanned Race mode, Interactive Race mode, tyre inventory age-to-health derivation, and future Stint-Boundary DAG strategy optimisation.

3. **Epistemological & Calibration Honesty:** Missing or uncalibrated parameters in local JSON datasets are represented as `std::optional<double>` (`std::nullopt`) and never coerced to `0.0`. The system explicitly separates **dataset load validity** (`LoadStatus::Success`) from **simulation capability readiness** (`ExecutionPermission`), blocking uncalibrated runs unless an authorised lower-fidelity/estimated parameter set is explicitly supplied, tagged, and confirmed by the user.

## 2. Current Repository Tree Map and Directory Responsibilities

Every path listed below exists and is verified in the repository as of this implementation. Non-existent future components are excluded from this tree and labelled `PLANNED` in Section 3.

```
F1_Sim/
├── .gitignore                                              [IMPLEMENTED] Ignores build/ and compiled binaries
├── CMakeLists.txt                                          [IMPLEMENTED] Canonical CMake build (f1_sim_dal, f1_sim_engine, f1_sim, tests)
├── Makefile                                                [IMPLEMENTED] GNU Make build & test runner (build/libf1_sim_dal.a, build/libf1_sim_engine.a, build/f1_sim)
├── README.md                                               [IMPLEMENTED] Build instructions, CLI flags, and Interactive CLI User Guide
├── data/                                                   [IMPLEMENTED] Read-only runtime JSON datasets (CONFINED DATA ROOT)
│   ├── compounds/
│   │   └── 2026_pirelli_slick_catalogue_v1.json            [IMPLEMENTED] Schema 4: 2026 C1..C5 catalogue (structural shell; C3 pace=0.0, wear/deg null)
│   ├── events/
│   │   ├── 2026_british_gp_config.json                     [IMPLEMENTED] Schema 2: 2026 British GP (L=52, C1/C2/C3 nominated, Sprint format)
│   │   └── 2026_british_gp_role_mapping.json               [IMPLEMENTED] Schema 5: Bijective role map (HARD->C1, MEDIUM->C2, SOFT->C3)
│   └── tracks/
│       ├── silverstone_gp_layout_v2011.json                [IMPLEMENTED] Schema 1A: Silverstone 5891m layout, 18 corners (8L/10R), pit lane 457m @ 80km/h
│       └── silverstone_gp_surface_v2019.json               [IMPLEMENTED] Schema 1B: Silverstone 2019 resurfacing profile (abrasiveness=2.0, grip=4.0)
├── docs/                                                   [IMPLEMENTED] Technical specifications and subsystem documentation
│   ├── DAL_Usage_and_Architecture.md                       [IMPLEMENTED] Data Access Layer architecture, security rules, and usage guide
│   ├── Simulation_Engine_Implementation_and_Decisions.md   [IMPLEMENTED] Simulation engine design decisions and framework reconciliation notes
│   ├── f1_simulator_mathematical_framework.md              [IMPLEMENTED] Authoritative mathematical specification (Stages M0-M5, Equations 6.1-12.3)
│   └── architecture/
│       └── program_blueprint.md                            [IMPLEMENTED] This program-wide digital blueprint and web-app integration reference
├── include/                                                [IMPLEMENTED] Public C++17 headers
│   └── f1_sim/
│       ├── data_access/
│       │   ├── data_access_layer.hpp                       [IMPLEMENTED] Public DAL facade (DataAccessLayer, including listAvailableEventIds)
│       │   ├── dataset_resolver.hpp                        [IMPLEMENTED] Path confinement and logical ID resolution (DatasetResolver)
│       │   ├── diagnostic.hpp                              [IMPLEMENTED] Severity, DiagnosticCode, LoadStatus, Diagnostic, Result<T>, DiagnosticSink
│       │   └── domain_models.hpp                           [IMPLEMENTED] Typed domain structs (EventBundle, TrackLayoutVersion, PhysicalCompoundSpec, etc.)
│       └── simulation/
│           ├── engine_types.hpp                            [IMPLEMENTED] Simulation domain/state/decision/trace/result types and enums
│           ├── lap_evaluator.hpp                           [IMPLEMENTED] Stateless mathematical evaluator (LapStintEvaluator)
│           ├── race_session.hpp                            [IMPLEMENTED] Session state machine & controller (RaceSession)
│           ├── strategy_validator.hpp                      [IMPLEMENTED] Readiness gating & strategy/decision validator (StrategyAndReadinessValidator)
│           └── tyre_inventory.hpp                          [IMPLEMENTED] Fixed 7-set (3S/2M/2H) inventory manager (TyreInventoryManager)
├── research/                                               [IMPLEMENTED] Offline provenance, data-mining reports, and manifests (NEVER read at runtime)
│   ├── manifests/
│   │   └── 2026_silverstone_v1.json                        [IMPLEMENTED] Dataset batch manifest
│   ├── reports/
│   │   └── 2026_silverstone_research_and_validation_report.md [IMPLEMENTED] Empirical research & validation report for 2026 British GP dataset
│   └── sources/
│       └── sources.md                                      [IMPLEMENTED] Regulatory and technical source citations
├── src/                                                    [IMPLEMENTED] C++17 implementation files
│   ├── main.cpp                                            [IMPLEMENTED] Interactive menu-driven CLI application & deterministic --smoke-test entry point
│   ├── data_access/
│   │   ├── data_access_layer.cpp                           [IMPLEMENTED] 7-stage schema, semantic, and cross-reference validation implementation
│   │   ├── dataset_resolver.cpp                            [IMPLEMENTED] Canonical path confinement under data/{tracks,events,compounds}
│   │   ├── json_parser.cpp                                 [IMPLEMENTED] Strict RFC 8259 recursive-descent JSON parser implementation
│   │   └── json_parser.hpp                                 [IMPLEMENTED] Private internal JSON AST header (hidden from public include/)
│   └── simulation/
│       ├── lap_evaluator.cpp                               [IMPLEMENTED] Single-source mathematical equations implementation
│       ├── race_session.cpp                                [IMPLEMENTED] Preplanned & Interactive race session controller implementation
│       ├── strategy_validator.cpp                          [IMPLEMENTED] Capability readiness & strategy/decision validation implementation
│       └── tyre_inventory.cpp                              [IMPLEMENTED] 7-set tyre inventory lifecycle and deterministic set allocation
└── tests/                                                  [IMPLEMENTED] Automated verification test suites
    ├── data_access/
    │   └── dal_tests.cpp                                   [IMPLEMENTED] 10 DAL verification test suites (including listAvailableEventIds)
    └── simulation/
        └── simulation_tests.cpp                            [IMPLEMENTED] 13 verification test suites (8 Simulation Engine + 5 Interactive CLI suites)
```

## 3. Whole-Program Architecture Diagram

The diagram below illustrates the complete system architecture, distinguishing **IMPLEMENTED** components (`f1_sim::cli`, `f1_sim::simulation`, `f1_sim::data_access`, local JSON datasets, and verification suites) from **PLANNED** future components (Web UI, Application/HTTP API Adapter, Strategy Optimiser, and Offline Telemetry Calibration Pipeline).

```
flowchart TB
    subgraph IMPLEMENTED_CLI["Interactive CLI Layer — f1_sim::cli (IMPLEMENTED)"]
        CLI["CliApp (src/main.cpp)<br/>7-Option Interactive Menu, Strategy Builder,<br/>7-Set Inventory Age Editor, Step/Pause/Resume,<br/>Interactive Decision Prompt & --smoke-test"]
    end

    subgraph PLANNED_WEB["Future Presentation & API Layer (PLANNED)"]
        UI["Web UI Client (PLANNED)<br/>Strategy Builder, Inventory Age Editor,<br/>Live Lap Telemetry & Interactive Pit Prompt"]
        API["Application / HTTP / WebSocket Adapter (PLANNED)<br/>Session Registry, JSON DTO Serialiser,<br/>Command Dispatcher"]
        OPT["Strategy Optimiser Module f1_sim::optimiser (PLANNED)<br/>Stint-Boundary DAG O(|R|*L^2) & Top-M DP Search"]
    end

    subgraph IMPLEMENTED_ENGINE["C++17 Simulation Engine Subsystem — f1_sim::simulation (IMPLEMENTED)"]
        SESS["RaceSession Controller (IMPLEMENTED)<br/>Lifecycle State Machine, Pause/Resume,<br/>Snapshot/Restore, Preplanned & Interactive Modes"]
        VAL["StrategyAndReadinessValidator (IMPLEMENTED)<br/>Capability Gating, Stint/Compound Validation,<br/>Interactive Decision & Extension Validator"]
        INV["TyreInventoryManager (IMPLEMENTED)<br/>Fixed 7-Set Inventory (S1..S3, M1..M2, H1..H2),<br/>Age->Health Derivation, Deterministic Set Allocation"]
        EVAL["LapStintEvaluator (IMPLEMENTED)<br/>Single-Source Equations (6.1-9.2), Mandatory Stop<br/>Monitor (<30% Mean, <12% Corner), Stint Rollout"]
    end

    subgraph IMPLEMENTED_DAL["C++17 Data Access Layer — f1_sim::data_access (IMPLEMENTED)"]
        DAL["DataAccessLayer Facade (IMPLEMENTED)<br/>listAvailableEventIds, loadEventBundle,<br/>checkBaselineSimulationSufficiency, reportEngineDiscrepancy"]
        RES["DatasetResolver (IMPLEMENTED)<br/>Regex ID Validation, Symlink Escape Guard,<br/>Confinement to data/{tracks,events,compounds}"]
        JPAR["detail::parseJsonDocument (IMPLEMENTED)<br/>Private RFC 8259 Recursive-Descent Parser"]
        SINK["InMemoryDiagnosticCollector (IMPLEMENTED)<br/>Thread-Safe Two-Way Diagnostic Sink"]
    end

    subgraph IMPLEMENTED_DATA["Local Read-Only Runtime Datasets — data/ (IMPLEMENTED)"]
        TRK["data/tracks/*.json (IMPLEMENTED)<br/>TrackLayoutVersion & TrackSurfaceProfile"]
        EVT["data/events/*.json (IMPLEMENTED)<br/>EventConfiguration & EventRoleMapping"]
        CMP["data/compounds/*.json (IMPLEMENTED)<br/>PhysicalCompoundCatalogue (C1..C5)"]
    end

    CLI -->|"Calls Public C++ Engine & DAL APIs"| SESS
    CLI -->|"Read-Only Event Discovery & Sufficiency"| DAL
    UI <-->|"REST / WebSocket DTOs (PLANNED)"| API
    API -->|"Calls Public C++ Engine API"| SESS
    OPT -->|"Reuses evaluateStintRollout & evaluatePitStopLoss"| EVAL
    API -.->|"Invokes Strategy Search (PLANNED)"| OPT

    SESS --> VAL
    SESS --> INV
    SESS --> EVAL
    VAL --> INV
    VAL --> EVAL
    INV --> EVAL

    SESS -->|"loadEventBundle (Read-Only)"| DAL
    VAL -->|"reportEngineDiscrepancy (In-Memory)"| DAL
    DAL --> RES
    DAL --> JPAR
    DAL --> SINK
    RES -->|"Confined File Reads"| TRK
    RES -->|"Confined File Reads"| EVT
    RES -->|"Confined File Reads"| CMP
```

## 4. Component Dependency Map and Layer-Isolation Rules

### 4.1 Direction of Compile-Time and Runtime Dependencies

```
[src/main.cpp (f1_sim::cli)]   [tests/simulation/simulation_tests.cpp]
              \                               /
               ▼                             ▼
[libf1_sim_engine.a] (f1_sim::simulation)
  ├── race_session.cpp ────────► strategy_validator.cpp ──► tyre_inventory.cpp ──► lap_evaluator.cpp
  │                                      │                                                │
  └──────────────────────────────────────┴────────────────────────────────────────────────┘
                                         │
                                         ▼ (Typed Domain Structs & Public Facade ONLY)
[libf1_sim_dal.a] (f1_sim::data_access)
  ├── data_access_layer.cpp ───► dataset_resolver.cpp
  └── json_parser.cpp (PRIVATE to src/data_access/; NOT in include/)
```

### 4.2 Hard Architectural Rules Preventing DAL Bypass

1. **Private JSON Parser Header:** `src/data_access/json_parser.hpp` resides inside `src/data_access/` rather than `include/f1_sim/`. The CMake target `f1_sim_dal` marks `src/data_access` as a `PRIVATE` include directory, making `detail::JsonValue` and `detail::parseJsonDocument` invisible to `f1_sim_engine`, `f1_sim`, and downstream consumers.

2. **Zero File I/O in `src/simulation/` and `src/main.cpp`:** Neither `src/simulation/` nor `src/main.cpp` parses JSON files directly. All event discovery (`DataAccessLayer::listAvailableEventIds()`) and dataset loading (`DataAccessLayer::loadEventBundle()`) pass exclusively through `f1_sim::data_access`.

3. **Read-Only Local Data Root:** `DataAccessLayer` never opens files with `std::ios::out` or `std::ios::trunc`. Diagnostic feedback from the simulation engine (`DataAccessLayer::reportEngineDiscrepancy`) writes strictly to the in-memory `DiagnosticSink` and never modifies JSON files on disk.

## 5. Build System, Test Structure, and Local Commands

### 5.1 Build Targets (`Makefile` & `CMakeLists.txt`)

* **Zero External Third-Party Dependencies:** The DAL, Simulation Engine, and Interactive CLI use standard ISO C++17 (`<filesystem>`, `<optional>`, `<variant>`, `<array>`, `<vector>`, `<map>`, `<set>`, `<cmath>`, `<mutex>`).

* **Static Libraries & Executable:**
  * `build/libf1_sim_dal.a` (`f1_sim_dal` in CMake)
  * `build/libf1_sim_engine.a` (`f1_sim_engine` in CMake)
  * `build/f1_sim` (`f1_sim` in CMake)

* **Verification Test Binaries:**
  * `build/dal_tests` (`f1_sim_dal_tests` in CMake) — 10 test suites covering all mandatory DAL checks and read-only event ID discovery (`listAvailableEventIds`).
  * `build/simulation_tests` (`f1_sim_simulation_tests` in CMake) — 13 test suites covering 8 Simulation Engine suites and 5 Interactive CLI end-to-end verification suites (`Test09`–`Test13`).

### 5.2 Local Build & Verification Commands

```bash
# Via GNU Make:
make clean && make all
make test
./build/f1_sim --help
./build/f1_sim --data-root data --check-only
./build/f1_sim --data-root data --smoke-test
./build/f1_sim --data-root data

# Via CMake (Windows PowerShell with MSYS2 UCRT64 GCC):
cmake -S . -B build-ucrt64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe"
cmake --build build-ucrt64
ctest --test-dir build-ucrt64 --output-on-failure
.\build-ucrt64\f1_sim.exe
```
