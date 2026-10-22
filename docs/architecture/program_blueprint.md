# F1_Sim — Program-Wide Digital Blueprint & Reference Architecture

**Document Path:** `docs/architecture/program_blueprint.md`

**Project Repository:** `https://github.com/sm9026/F1_Sim.git` (local workspace snapshot)

**Target Standard & Toolchain:** ISO C++17 (`-std=c++17 -Wall -Wextra -Wpedantic -Werror`), GNU Make / CMake

**Mathematical Authority:** `docs/f1_simulator_mathematical_framework.md` (Version `1.0.0`)

## 1. Executive Summary and Purpose of `F1_Sim`

`F1_Sim` is a deterministic, data-driven Formula 1 single-car race strategy simulator written in C++17. Its primary engineering goal is to evaluate and compare tyre-stint race strategies across a fixed Grand Prix distance of $L$ laps by integrating static circuit geometry, asphalt surface characteristics, Pirelli's physical dry slick compound catalogue (`C1`–`C5`), event-specific tyre role nominations (`SOFT`, `MEDIUM`, `HARD`), fuel-burn mass trajectories, environmental conditions, and pit-stop time-loss accounting under a single mathematical framework.

### Core Architectural Principles

1. **Strict Separation of Layers:** The future Web UI and Application/API Adapter are completely decoupled from the C++ numerical engine (`f1_sim::simulation`), which in turn is decoupled from raw filesystem/JSON operations by a read-only local Data Access Layer (`f1_sim::data_access`).

2. **Single Mathematical Source of Truth:** All lap-time decompositions (Equations 6.1–6.4), tyre health transitions (Equations 7.1–7.6), environmental multipliers (Equations 8.1–8.2), and pit-stop loss calculations (Equations 9.1–9.2) live in a single stateless evaluator (`LapStintEvaluator`) shared across Preplanned Race mode, Interactive Race mode, tyre inventory age-to-health derivation, and future Stint-Boundary DAG strategy optimisation.

3. **Epistemological & Calibration Honesty:** Missing or uncalibrated parameters in local JSON datasets are represented as `std::optional<double>` (`std::nullopt`) and never coerced to `0.0`. The system explicitly separates **dataset load validity** (`LoadStatus::Success`) from **simulation capability readiness** (`ExecutionPermission`), blocking uncalibrated runs unless an authorised lower-fidelity/estimated parameter set is explicitly supplied and tagged.

## 2. Current Repository Tree Map and Directory Responsibilities

Every path listed below exists and is verified in the repository as of this implementation. Non-existent future components are excluded from this tree and labelled `PLANNED` in Section 3.

```
F1_Sim/
├── .gitignore                                              [IMPLEMENTED] Ignores build/ and compiled binaries
├── CMakeLists.txt                                          [IMPLEMENTED] Canonical CMake build (f1_sim_dal, f1_sim_engine, tests)
├── Makefile                                                [IMPLEMENTED] GNU Make build & test runner (build/libf1_sim_dal.a, build/libf1_sim_engine.a)
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
│       │   ├── data_access_layer.hpp                       [IMPLEMENTED] Public DAL facade (DataAccessLayer)
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
│   ├── main.cpp                                            [IMPLEMENTED] Reserved production entry point placeholder (currently empty)
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
    │   └── dal_tests.cpp                                   [IMPLEMENTED] 9 DAL verification test suites (all 11 mandatory DAL checks)
    └── simulation/
        └── simulation_tests.cpp                            [IMPLEMENTED] 8 comprehensive engine, DAL-integration, and math conformance test suites

```

## 3. Whole-Program Architecture Diagram

The diagram below illustrates the complete system architecture, distinguishing **IMPLEMENTED** components (`f1_sim::simulation`, `f1_sim::data_access`, local JSON datasets, and verification suites) from **PLANNED** future components (Web UI, Application/HTTP API Adapter, Strategy Optimiser, and Offline Telemetry Calibration Pipeline).

```
flowchart TB
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
        DAL["DataAccessLayer Facade (IMPLEMENTED)<br/>loadEventBundle, checkBaselineSimulationSufficiency,<br/>reportEngineDiscrepancy"]
        RES["DatasetResolver (IMPLEMENTED)<br/>Regex ID Validation, Symlink Escape Guard,<br/>Confinement to data/{tracks,events,compounds}"]
        JPAR["detail::parseJsonDocument (IMPLEMENTED)<br/>Private RFC 8259 Recursive-Descent Parser"]
        SINK["InMemoryDiagnosticCollector (IMPLEMENTED)<br/>Thread-Safe Two-Way Diagnostic Sink"]
    end

    subgraph IMPLEMENTED_DATA["Local Read-Only Runtime Datasets — data/ (IMPLEMENTED)"]
        TRK["data/tracks/*.json (IMPLEMENTED)<br/>TrackLayoutVersion & TrackSurfaceProfile"]
        EVT["data/events/*.json (IMPLEMENTED)<br/>EventConfiguration & EventRoleMapping"]
        CMP["data/compounds/*.json (IMPLEMENTED)<br/>PhysicalCompoundCatalogue (C1..C5)"]
    end

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
[tests/simulation/simulation_tests.cpp]
          │
          ▼
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

1. **Private JSON Parser Header:** `src/data_access/json_parser.hpp` resides inside `src/data_access/` rather than `include/f1_sim/`. The CMake target `f1_sim_dal` marks `src/data_access` as a `PRIVATE` include directory, making `detail::JsonValue` and `detail::parseJsonDocument` invisible to `f1_sim_engine` and downstream consumers.

2. **Zero File I/O in `src/simulation/`:** None of the files under `include/f1_sim/simulation/` or `src/simulation/` include `<fstream>` or `<filesystem>`. All event, circuit, surface, role-mapping, and compound catalogue data enter `f1_sim::simulation` exclusively as immutable `f1_sim::data_access::EventBundle` structs.

3. **Read-Only Local Data Root:** `DataAccessLayer` never opens files with `std::ios::out` or `std::ios::trunc`. Diagnostic feedback from the simulation engine (`DataAccessLayer::reportEngineDiscrepancy`) writes strictly to the in-memory `DiagnosticSink` and never modifies JSON files on disk.

## 5. Data Access Layer (`f1_sim::data_access`) Reference

### 5.1 Responsibilities & Bundle-Loading Flow

When `DataAccessLayer::loadEventBundle(event_id)` is called:

1. `DatasetResolver` validates `event_id` against `^[a-z0-9_]{1,64}$`, blocks `..` and path separators, canonicalizes `data_root`, and verifies that target paths reside inside `data_root/{tracks,events,compounds}`.

2. `loadEventConfiguration(event_id)` loads and validates `events/{event_id}_config.json` (Schema 2).

3. `loadEventRoleMapping(event_id)` loads and validates `events/{event_id}_role_mapping.json` (Schema 5), enforcing all four mathematical invariants of Framework Section 7.2:

   * Domain completeness over `{HARD, MEDIUM, SOFT}`;

   * Injectivity (3 distinct physical compounds);

   * Bijection onto `nominated_compounds_set` $\mathcal{N}_e$;

   * Strict hardness rank ordering $\rho(m_e(H)) < \rho(m_e(M)) < \rho(m_e(S))$ on official events.

4. `loadTrackLayout(track_id, layout_version)` loads `tracks/{track_id}_layout_v{layout_version}.json` (Schema 1A).

5. `loadTrackSurface(track_id, surface_version, layout_version)` loads `tracks/{track_id}_surface_v{surface_version}.json` (Schema 1B) and verifies the `layout_version` foreign key.

6. `loadCompoundCatalogue(season_year, spec_version)` loads `compounds/{season_year}_*_catalogue_v{spec_version}.json` (Schema 4) and verifies pace/wear monotonicity across `C1`–`C5`.

7. Cross-reference validation checks that `scheduled_race_distance_km` agrees with `race_distance_laps * lap_length_m / 1000.0` within $\pm 0.5\,\mathrm{km}$ and that all nominated compounds exist in the catalogue.

### 5.2 Supported vs. Missing Repository Schemas

| Framework Schema | Status in DAL & `data/` | How Consumed by Simulation Engine | 
| ----- | ----- | ----- | 
| **Schema 1A: `TrackLayoutVersion`** | **IMPLEMENTED** (`data/tracks/silverstone_gp_layout_v2011.json`) | Loaded into `EventBundle::track_layout`. Sector lengths and speed-band fractions are `std::nullopt` (deferred to telemetry). | 
| **Schema 1B: `TrackSurfaceProfile`** | **IMPLEMENTED** (`data/tracks/silverstone_gp_surface_v2019.json`) | Loaded into `EventBundle::track_surface`. `bumpiness_index_score` is `std::nullopt`. | 
| **Schema 2: `EventConfiguration`** | **IMPLEMENTED** (`data/events/2026_british_gp_config.json`) | Loaded into `EventBundle::event_config` ($L = 52$, $\mathcal{N}_e = \{C1, C2, C3\}$, $K_{\min} = 2$). | 
| **Schema 3: `ScenarioEnvironmentSeries`** | **PARTIAL** (In-memory `EnvironmentScenarioConfig` implemented; no JSON file under `data/`) | Passed to `RaceSession::setEnvironmentConfig(...)` as constant baseline or lap-indexed vector. | 
| **Schema 4: `PhysicalCompoundCatalogue`** | **IMPLEMENTED** (`data/compounds/2026_pirelli_slick_catalogue_v1.json`) | Loaded into `EventBundle::compound_catalogue`. In the repository's 2026 catalogue, `C3.pace_offset_ref_s = 0.0`, while other `pace_offset_ref_s`, `base_wear_rate_per_lap`, and `linear_deg_coeff_s` fields are `null` (`std::nullopt`). | 
| **Schema 5: `EventRoleMapping`** | **IMPLEMENTED** (`data/events/2026_british_gp_role_mapping.json`) | Loaded into `EventBundle::role_mapping` (`HARD -> C1`, `MEDIUM -> C2`, `SOFT -> C3`). | 
| **Schema 6: `VehicleSpecification`** | **PARTIAL** (In-memory `VehicleConfig` implemented with framework defaults; no JSON file under `data/`) | Passed to `RaceSession::setVehicleConfig(...)` / `setDriverMassKg(...)`. | 
| **Schema 7: `CalibratedModelParameters`** | **PARTIAL** (In-memory `ModelParameters` & `CompoundParameterOverride` implemented; no JSON file under `data/`) | Passed to `RaceSession::setModelParameters(...)`. Missing `ref_lap_time_s` or `pit_loss_total_s` blocks execution via `assessReadiness()`. | 
| **Schema 8: `HistoricalLapDataset`** | **PLANNED** (Offline calibration pipeline input only) | Not used at runtime; will feed future offline parameter fitting. | 

## 6. Simulation Engine (`f1_sim::simulation`) Components & Domain Model

### 6.1 Conceptual Subsystems & Interactions

1. **`RaceSession` (`race_session.hpp` / `race_session.cpp`):** Stateful orchestrator owning session configuration, the immutable `EventBundle`, the `TyreInventoryManager`, runtime lap/stint/fuel/timing counters, interactive decision prompts, checkpoint snapshots, and the completed `RaceResult`.

2. **`TyreInventoryManager` (`tyre_inventory.hpp` / `tyre_inventory.cpp`):** Owns the 7 physical tyre sets (`S1, S2, S3, M1, M2, H1, H2`), enforces age-only editing locks while a simulation is active, computes derived starting health via `LapStintEvaluator`, allocates sets deterministically to stints, and resets runtime wear upon run termination.

3. **`StrategyAndReadinessValidator` (`strategy_validator.hpp` / `strategy_validator.cpp`):** Evaluates 5-axis capability readiness (`CapabilityReadinessReport`), validates initial strategies (`validateInitialStrategy`), and validates mid-race interactive decisions (`validateInteractiveDecision`).

4. **`LapStintEvaluator` (`lap_evaluator.hpp` / `lap_evaluator.cpp`):** Pure, stateless mathematical engine computing flying-lap times (`evaluateFlyingLap`), tyre health updates (`stepTyreHealth`), mandatory threshold checks (`evaluateMandatoryStopThresholds`), pit-stop time losses (`evaluatePitStopLoss`), and contiguous stint rollouts (`evaluateStintRollout`).

## 7. Race-Mode Workflows: Preplanned vs. Interactive

### 7.1 Mode A — Preplanned Race (`RaceMode::Preplanned`)

* **Autonomous Execution:** Once `start()` succeeds, `advanceLap()` or `runUntilStopOrCompletion()` runs autonomously to lap $L$. Manual decisions (`applyDecision`) are rejected.

* **Planned Stint Boundaries:** When `actual_laps_completed == effective_target_laps` at lap $k < L$ (with no mandatory health breach), the engine executes a pit stop (`PitStopReason::PlannedStrategyBoundary`), adds net pit loss $P_j$, removes the outgoing set, and fits the next stint's assigned set.

* **Mandatory Health-Triggered Stops & Shortfall Cascade:**

  * After every completed lap $k < L$, the engine checks $\text{mean}(\mathbf{h}_{k+1}) < 0.30$ and $\min_i h_{k+1,i} < 0.12$.

  * If either strict inequality holds, the current stint ends immediately before lap $k+1$.

  * If the mandatory trigger coincides with the planned stint boundary (`actual_laps_completed == effective_target_laps`), it is executed as **one** stop (`PitStopReason::MandatoryAndPlannedBoundary`), never two.

  * If the stint ended early (`actual_laps_completed < effective_target_laps`), the stint shortfall `shortfall = effective_target_laps - actual_laps_completed` is added to `accumulated_shortfall_laps`.

  * The next stint's uncapped cascaded target becomes `original_target_laps + accumulated_shortfall_laps`, and its `effective_target_laps` is `std::min(uncapped_cascaded_target_laps, L - completed_laps)`.

  * Thus on the final stint, the target is capped by the remaining race distance $L - k$, guaranteeing the race finishes at lap $L$.

### 7.2 Mode B — Interactive Race (`RaceMode::Interactive`)

* **Decision Points:** At any planned stint boundary or mandatory health-trigger lap $k < L$, the engine completes and counts lap $k$, records a `PendingDecisionPrompt`, and transitions to `SessionState::DecisionRequired` **before** executing the pit stop or starting lap $k+1$.

* **User Actions (`InteractiveDecision`):**

  1. `DecisionActionType::KeepOriginalPlan`: Pits immediately at lap $k$ onto the next planned stint, provided the remaining stints sum to `L - k`.

  2. `DecisionActionType::ExtendCurrentStint`: Adds `extension_laps > 0` to the current stint (`new_target = actual_laps + extension_laps`). The engine **never** automatically subtracts `extension_laps` from the next stint; the user must provide `revised_remaining_stints` such that `extension_laps + sum(revised_remaining_stints) == L - k`.

  3. `DecisionActionType::PitNowWithRevisedPlan`: Pits immediately at lap $k$ while replacing the future stints' target lengths and/or tyre roles (including the final stint's role), subject to remaining distance `L - k`, compound diversity ($K_{\min}$), stint bounds, and unused physical set availability.

* **Mandatory-Stop Precedence & No Silent Cascade:**

  * When `PendingDecisionPrompt::mandatory_stop_required == true`, `ExtendCurrentStint` is strictly rejected (`EngineStatus::MandatoryStopCannotBeOverridden`).

  * Unlike Preplanned mode, Interactive mode **does not** automatically cascade an early mandatory stop's shortfall onto the next stint. Instead, `PendingDecisionPrompt` exposes the actual un-cascaded remaining stints and `remaining_race_laps = L - k` so the user explicitly reallocates the remaining race distance via `PitNowWithRevisedPlan`.

* **Explicit Resume:** `applyDecision(...)` commits a valid decision and transitions from `DecisionRequired` to `Paused`. The caller must explicitly invoke `resume()` to transition to `Running`.

## 8. Tyre Inventory Model and Complete Lifecycle

### 8.1 Fixed 7-Set Inventory Structure

| Set ID | Role (`TyreRole`) | Default Configured Age | Default Derived Health | Editable While Running/Paused? | 
| ----- | ----- | ----- | ----- | ----- | 
| `"S1"`, `"S2"`, `"S3"` | `TyreRole::Soft` (`S`) | `0` laps | `100.0%` (`1.0`) | **No** (Age-only when idle) | 
| `"M1"`, `"M2"` | `TyreRole::Medium` (`M`) | `0` laps | `100.0%` (`1.0`) | **No** (Age-only when idle) | 
| `"H1"`, `"H2"` | `TyreRole::Hard` (`H`) | `0` laps | `100.0%` (`1.0`) | **No** (Age-only when idle) | 

### 8.2 Deterministic Allocation Policy & Lifecycle Rules

1. **Age-to-Health Derivation:** Users can only edit `configured_age_laps` (via `RaceSession::setConfiguredTyreAge`). Derived starting health is computed via Equation (7.3): $h_{\mathrm{init},i}(a_0) = \max(0.0, 1.0 - a_0 \cdot D_{\mathrm{base}}(c, r, i))$.

2. **Deterministic Set Selection:** When `StintSpec::requested_set_id` is omitted, `TyreInventoryManager::allocateSetsForStints` selects an unused set of the requested role ordered by:

   * Highest `derived_starting_health->meanHealth()`;

   * Lowest `configured_age_laps`;

   * Lexicographically smallest `set_id` (`"S1" < "S2" < "S3"`).

3. **Physical Set Uniqueness:** No physical set ID can ever be assigned to two stints in the same race.

4. **Post-Run Reset vs. Track-Change Reset:**

   * When a run ends (`Completed`, `Failed`, or `Cancelled`), `resetRuntimeStateToConfigured()` restores every set's `runtime_age_laps`, `runtime_health`, `currently_fitted`, and `consumed_in_current_run` back to its pre-race configured state while **preserving** `configured_age_laps` for repeatable A/B comparisons.

   * When `selectEvent` or `loadFromEventBundle` changes the active event/track, `resetForNewEvent()` resets all 7 sets' `configured_age_laps` back to `0` (`100%` health).

## 9. Session State Machine Diagram

```
stateDiagram-v2
    [*] --> Unconfigured
    Unconfigured --> Configured : selectEvent() / loadFromEventBundle()
    Configured --> Configured : set*Config() / setConfiguredTyreAge()
    Configured --> Validated : validate() [canExecute() == true]
    Validated --> Configured : set*Config() / setConfiguredTyreAge()
    Configured --> Running : start() [auto-validates & fits Stint 0 set]
    Validated --> Running : start() [fits Stint 0 set]

    Running --> Running : advanceLap() [k < L, no stop/decision trigger]
    Running --> Paused : pause()
    Paused --> Running : resume()

    Running --> DecisionRequired : advanceLap() [Interactive mode stop/mandatory boundary k < L]
    DecisionRequired --> DecisionRequired : applyDecision() [invalid plan -> rejected]
    DecisionRequired --> Paused : applyDecision() [valid decision committed]

    Running --> Completed : advanceLap() [k == L -> resets runtime tyre state]
    Running --> Failed : advanceLap() [unrecoverable runtime constraint -> resets runtime tyre state]
    Running --> Cancelled : cancel() [resets runtime tyre state]
    Paused --> Cancelled : cancel() [resets runtime tyre state]
    DecisionRequired --> Cancelled : cancel() [resets runtime tyre state]

    Completed --> Configured : resetForNewRun() / set*Config() / setConfiguredTyreAge()
    Completed --> Running : start() [re-runs from preserved configuration]
    Failed --> Configured : resetForNewRun() / set*Config() / setConfiguredTyreAge()
    Cancelled --> Configured : resetForNewRun() / set*Config() / setConfiguredTyreAge()

```

## 10. Lap-by-Lap Control Flow and Pit-Stop Timing Accounting

### 10.1 Exact Per-Lap Execution Sequence (`RaceSession::advanceLap`)

For each racing lap $k = \text{completed\_laps} + 1 \in \{1, \ldots, L\}$:

1. **Pre-Lap Guard:** Verify `state_ == SessionState::Running` and resolve active stint $j$, fitted physical set $\sigma_j$, role $r_j$, and physical compound $c_j = m_e(r_j)$.

2. **Fuel Mid-Lap Evaluation (Eq. 6.2a):** Compute mid-lap fuel mass $M_{\mathrm{fuel},k} = \max(0.0, M_{\mathrm{fuel},0} - \mu_{\mathrm{fuel}}(k - 0.5))$ and end-of-lap fuel mass $M_{\mathrm{fuel,end},k} = \max(0.0, M_{\mathrm{fuel},0} - \mu_{\mathrm{fuel}} k)$.

3. **Flying-Lap Time Evaluation (Eq. 6.1):** Evaluate $T_k$ using starting health $\mathbf{h}_k$:
   

   $$
   T_k = T_{\mathrm{ref}}(r, v) + \Delta T_{\mathrm{fuel},k} + \Delta T_{\mathrm{evol},k} + \Delta T_{\mathrm{start},k} + \Delta T_{\mathrm{comp}}(c_j, r) + \Delta T_{\mathrm{tyre},k}(\mathbf{h}_k) + \Delta T_{\mathrm{in/out},k}
   $$

   
   where $\Delta T_{\mathrm{start},k}$ is non-zero only on lap $k = 1$, and $\Delta T_{\mathrm{in/out},k} = \Delta T_{\mathrm{out}}$ is non-zero only on the first lap of a new stint after a pit stop ($j \ge 1, m = 1$).

4. **Elapsed Time & Fuel Update:** Add $T_k$ once to `elapsed_race_time_s_` and `total_flying_lap_time_s_`. Update `current_fuel_remaining_kg_`.

5. **Tyre State Update (Eq. 7.1–7.4, 8.1):** Compute per-corner depletion $\Delta h_{k,i} = D_{\mathrm{base}}(c_j, r, i) \cdot F_{\mathrm{env}}(c_j, \mathbf{w}_k) \cdot F_{\mathrm{usage}}(k)$ and update $\mathbf{h}_{k+1} = \max(\mathbf{0}, \mathbf{h}_k - \Delta \mathbf{h}_k)$. Increment `runtime_age_laps` by $+1$ and `completed_laps_` to $k$.

6. **Finish-Line Check (**$k == L$**):** If $k == L$, mark stint complete, record `LapTraceEntry`, transition session to `SessionState::Completed`, reset runtime inventory state, and return `EngineStatus::RaceCompleted` (never adding a post-finish pit stop even if $\mathbf{h}_{L+1}$ crossed a threshold).

7. **Post-Lap Threshold & Boundary Check (**$k < L$**):** Evaluate `MandatoryStopCheckResult` on $\mathbf{h}_{k+1}$ (`mean < 0.30` or `min_corner < 0.12`) and check `actual_laps_completed >= effective_target_laps`.

   * In `Preplanned` mode: execute atomic pit stop (`executePitStopAtCurrentLap`), add $P_j$ once to `elapsed_race_time_s_` and `total_pit_stop_loss_s_`, apply shortfall cascade if `shortfall > 0`, and fit stint $j+1$'s tyre set.

   * In `Interactive` mode: populate `PendingDecisionPrompt` and transition to `SessionState::DecisionRequired`.

### 10.2 Pit-Stop Time-Loss Accounting Boundary (No Double-Counting)

Following Framework Section 9.2 (Incremental Main-Straight Replacement Convention):

* $T_{L_j}$ (in-lap) and $T_{L_j+1}$ (out-lap) include the full uninterrupted main-straight racing time, plus $\Delta T_{\mathrm{out}}$ on lap $L_j + 1$ for cold-tyre warm-up after pit exit.

* $P_j$ (Equation 9.1) accounts strictly for the incremental pit-lane traversal loss $\Delta T_{\mathrm{pit-lane}} = \frac{L_{\mathrm{pit}}}{v_{\mathrm{pit}}/3.6} - T_{\mathrm{main-bypass}}$, stationary wheel-change service time $\tau_{\mathrm{stat},j}$, and pit entry/exit deceleration/acceleration loss $\Delta T_{\mathrm{pit-accel}}$:
  

  $$
  P_j = P_{\mathrm{ref}}(r) + \left(\tau_{\mathrm{stat},j} - \tau_{\mathrm{stat,ref}}\right)
  $$

* Consequently, $T_{\mathrm{race}}(J=2) - T_{\mathrm{race}}(J=1)$ at zero wear and equal compound pace equals $P_1 + \Delta T_{\mathrm{out}}$ exactly.

## 11. Key Public C++ Interface Contracts (`IMPLEMENTED`)

### 11.1 Data Access Layer (`include/f1_sim/data_access/data_access_layer.hpp`)

```
namespace f1_sim::data_access {

class DataAccessLayer {
public:
    explicit DataAccessLayer(
        std::filesystem::path data_root,
        std::shared_ptr<DiagnosticSink> diagnostic_sink = std::make_shared<InMemoryDiagnosticCollector>());

    Result<EventBundle> loadEventBundle(const std::string& event_id) const;
    Result<TrackLayoutVersion> loadTrackLayout(const std::string& track_id, int layout_version) const;
    Result<TrackSurfaceProfile> loadTrackSurface(
        const std::string& track_id, int surface_version,
        std::optional<int> expected_layout_version = std::nullopt) const;
    Result<EventConfiguration> loadEventConfiguration(const std::string& event_id) const;
    Result<EventRoleMapping> loadEventRoleMapping(const std::string& event_id) const;
    Result<PhysicalCompoundCatalogue> loadCompoundCatalogue(int season_year, int spec_version = 1) const;
    Result<PhysicalCompoundSpec> loadCompoundSpec(
        int season_year, int spec_version, PhysicalCompoundId compound_id) const;

    Result<void> checkBaselineSimulationSufficiency(const EventBundle& bundle) const;
    Diagnostic reportEngineDiscrepancy(const EngineDiscrepancyReport& report) const;
};

} // namespace f1_sim::data_access

```

### 11.2 Simulation Engine Session Controller (`include/f1_sim/simulation/race_session.hpp`)

```
namespace f1_sim::simulation {

class RaceSession {
public:
    explicit RaceSession(std::shared_ptr<data_access::DataAccessLayer> dal);

    // Event selection & bundle binding (resets configured tyre ages to 0 on event change)
    EngineResult<CapabilityReadinessReport> selectEvent(const std::string& event_id);
    EngineResult<CapabilityReadinessReport> loadFromEventBundle(
        const EventBundle& bundle, bool reset_inventory_ages = true);

    // Pre-race configuration (rejected with ConfigurationLockedWhileRunning if active)
    EngineResult<void> setRaceMode(RaceMode mode);
    EngineResult<void> setModelStage(ModelStage stage);
    EngineResult<void> setDriverMassKg(double driver_mass_kg);
    EngineResult<void> setVehicleConfig(const VehicleConfig& vehicle_config);
    EngineResult<void> setEnvironmentConfig(const EnvironmentScenarioConfig& env_config);
    EngineResult<void> setModelParameters(const ModelParameters& model_params);
    EngineResult<void> setStrategyPlan(const StrategyPlan& strategy_plan);
    EngineResult<TyreSetState> setConfiguredTyreAge(const std::string& set_id, int age_laps);
    EngineResult<void> resetConfiguredTyreAgesToDefault();

    // Readiness & pre-race validation
    CapabilityReadinessReport assessReadiness() const;
    EngineResult<CapabilityReadinessReport> validate();

    // Execution lifecycle & checkpointing
    EngineResult<SessionSnapshot> start();
    EngineResult<LapTraceEntry> advanceLap();
    EngineResult<SessionSnapshot> runUntilStopOrCompletion();
    EngineResult<SessionSnapshot> pause();
    EngineResult<SessionSnapshot> resume();
    EngineResult<SessionSnapshot> applyDecision(const InteractiveDecision& decision);
    EngineResult<RaceResult> cancel();
    EngineResult<void> resetForNewRun();

    // Read-only state queries
    SessionState state() const noexcept;
    SessionSnapshot snapshot() const;
    EngineResult<void> restoreSnapshot(const SessionSnapshot& snap);
    const std::optional<PendingDecisionPrompt>& pendingDecision() const noexcept;
    const std::optional<RaceResult>& lastResult() const noexcept;
};

} // namespace f1_sim::simulation

```

## 12. Error/Diagnostic Strategy and Capability Readiness Gating

`StrategyAndReadinessValidator::assessReadiness` evaluates five orthogonal readiness dimensions:

| Dimension | Field in `CapabilityReadinessReport` | Semantics | 
| ----- | ----- | ----- | 
| **1. Schema / Load Validity** | `bool bundle_load_valid` | `true` iff `DataAccessLayer::loadEventBundle` returned `LoadStatus::Success`. | 
| **2. User Input Validity** | `bool user_input_valid` | `true` iff driver mass (`[40, 160] kg`), fuel load (`[0, 115] kg`), environment ranges, and parameter ranges are finite and within domain bounds. | 
| **3. Mathematical Capability** | `bool mathematical_capability_sufficient` | `true` iff `ref_lap_time_s`, `pit_loss_total_s` (or pit components), all 3 nominated compounds' `(pace_offset_ref_s, base_wear_rate_per_lap, linear_deg_coeff_s)`, and any age $> 0$ starting-health derivations are available, and `surface_wetness <= 0.05`. | 
| **4. Calibration / Fidelity Status** | `FidelityStatus fidelity_status` | `Calibrated` (all inputs `Calibrated`/`PublishedObserved`), `EstimatedOrAssumedLowerFidelity` (uses `Estimated`/`Assumed` parameters), `UncalibratedIncomplete`, or `InvalidOrBlocked`. | 
| **5. Execution Permission** | `ExecutionPermission execution_permission` | `AllowedCalibrated`, `AllowedLowerFidelity`, `BlockedInvalidBundle`, `BlockedInvalidInput`, `BlockedMissingCalibration`, or `BlockedUnsupportedWetConditions`. | 

## 13. Build System, Test Structure, and Local Commands

### 13.1 Build Targets (`Makefile` & `CMakeLists.txt`)

* **Zero External Third-Party Dependencies:** The entire DAL and Simulation Engine use standard ISO C++17 (`<filesystem>`, `<optional>`, `<variant>`, `<array>`, `<vector>`, `<map>`, `<set>`, `<cmath>`, `<mutex>`).

* **Static Libraries:**

  * `build/libf1_sim_dal.a` (`f1_sim_dal` in CMake)

  * `build/libf1_sim_engine.a` (`f1_sim_engine` in CMake)

* **Verification Test Binaries:**

  * `build/dal_tests` (`f1_sim_dal_tests` in CMake) — 9 test suites covering all 11 mandatory DAL checks.

  * `build/simulation_tests` (`f1_sim_simulation_tests` in CMake) — 8 test suites covering strategy validation, 7-set inventory lifecycle, strict `< 30%` / `< 12%` health thresholds, Preplanned shortfall cascade, Interactive decisions & mandatory stop precedence, pause/snapshot/restore determinism, DAL capability gating, and Mathematical Framework Section 12 conformance (`923.000 s` and `925.483936 s`).

### 13.2 Local Build & Verification Commands

```
# Build both libraries and both test suites and run all tests via GNU Make:
make clean && make test

# Or via CMake (when cmake is installed on the host):
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

```

## 14. Current Model Stage, Baseline vs. Extension Boundaries, and Data Limitations

| Feature / Stage | Current Status | Notes & Boundaries | 
| ----- | ----- | ----- | 
| **Stage M0 — Deterministic Baseline** | **IMPLEMENTED** | Exact conformance with Synthetic Worked Example 1 (`923.000 s`) and `TEST_01`–`TEST_08`. | 
| **Stage M1 — Track-Aware Static Stress & Evolution** | **IMPLEMENTED** | Uses `TrackLayoutVersion` energy indices, `TrackSurfaceProfile` abrasiveness score, and $\Delta T_{\mathrm{evol},k}$ (Eq. 6.3, 7.4). | 
| **Stage M2 — Dynamic Environment & Forecast** $T_{\mathrm{trk}}$ | **IMPLEMENTED** | Supports constant or lap-indexed `LapEnvironment`, linearised/exponential $F_{\mathrm{env}}$ (Eq. 8.1a/b), neutral humidity ($\psi_{\mathrm{hum}} = 0$), and radiative $T_{\mathrm{trk}}$ inference (Eq. 8.2). | 
| **Stage M3 — Nonlinear Cliff & Four-Corner Asymmetry** | **IMPLEMENTED** (Opt-In) | Gated behind `ModelStage::M3_NonlinearCliffAndFourCorner`. Reproduces Synthetic Worked Example 2 (`925.483936 s`) and `TEST_10`. Baseline stages M0–M2 strictly maintain symmetric 4-corner health. | 
| **Stage M4 — Dynamic Bulk/Surface Tyre Temperature** $\boldsymbol{\theta}_k$ | **PLANNED** | Deferred per Framework Section 7.6 due to lack of public TPMS/infrared telemetry. | 
| **Stage M5 — Wet/Intermediate Crossover, Safety Car, Traffic** | **PLANNED** | Wet conditions (`surface_wetness > 0.05`) are actively guarded and blocked with `BlockedUnsupportedWetConditions`. | 
| **2026 Silverstone Empirical Calibration (`data/`)** | **PARTIAL** (Structural Shell) | `data/compounds/2026_pirelli_slick_catalogue_v1.json` has `null` wear/degradation coefficients because the 2026 regulation cars have not yet been empirically fitted from race telemetry. Running `2026_british_gp` requires supplying `ModelParameters` (tagged `Estimated` or `Calibrated`). | 

## 15. Future Web-App Integration Contract (`PLANNED` Adapter Layer)

When the web application is built in a subsequent task, the backend API adapter (e.g., C++ HTTP service or Node/Python binding wrapping `f1_sim::simulation::RaceSession`) should expose the following framework-neutral operations mapped directly to `RaceSession` methods:

| Logical API Endpoint / Command | Underlying C++ `RaceSession` Call | Request Payload Shape | Response Payload Shape | 
| ----- | ----- | ----- | ----- | 
| `POST /api/sessions` | `RaceSession(dal)` + `selectEvent(event_id)` | `{ "event_id": "2026_british_gp" }` | `{ "session_id", "state": "CONFIGURED", "readiness": CapabilityReadinessReport, "inventory": TyreSetState[7] }` | 
| `GET /api/sessions/{id}/readiness` | `session.assessReadiness()` | None | `CapabilityReadinessReport` (bundle validity, input validity, capability, fidelity, parameter gaps, diagnostics) | 
| `PATCH /api/sessions/{id}/inventory` | `session.setConfiguredTyreAge(set_id, age_laps)` | `{ "ages": [{ "set_id": "S1", "age_laps": 3 }] }` | `{ "inventory": TyreSetState[7], "diagnostics": Diagnostic[] }` (Rejected with `409 Conflict` if session is active) | 
| `PUT /api/sessions/{id}/config` | `setRaceMode`, `setDriverMassKg`, `setVehicleConfig`, `setEnvironmentConfig`, `setModelParameters`, `setStrategyPlan` | `{ "race_mode": "PREPLANNED" | "INTERACTIVE", "driver_mass_kg": 75.0, "strategy": StintSpec[], ... }` | `{ "state": "CONFIGURED", "readiness": CapabilityReadinessReport }` | 
| `POST /api/sessions/{id}/validate` | `session.validate()` | None | `EngineResult<CapabilityReadinessReport>` (allocated set IDs per stint + field-level diagnostics) | 
| `POST /api/sessions/{id}/start` | `session.start()` | None | `EngineResult<SessionSnapshot>` (`state: "RUNNING"`) | 
| `POST /api/sessions/{id}/advance` | `session.advanceLap()` or `session.runUntilStopOrCompletion()` | `{ "mode": "SINGLE_LAP" | "UNTIL_STOP_OR_COMPLETION" }` | `EngineResult<SessionSnapshot>` (transitions to `DECISION_REQUIRED` or `COMPLETED`) | 
| `POST /api/sessions/{id}/pause` | `session.pause()` | None | `EngineResult<SessionSnapshot>` (`state: "PAUSED"`) | 
| `POST /api/sessions/{id}/resume` | `session.resume()` | None | `EngineResult<SessionSnapshot>` (`state: "RUNNING"`) | 
| `POST /api/sessions/{id}/decision` | `session.applyDecision(decision)` | `InteractiveDecision` (`action_type`, `extension_laps`, `revised_remaining_stints`, `user_note`) | `EngineResult<SessionSnapshot>` (transitions to `PAUSED` on valid decision; stays `DECISION_REQUIRED` with field errors if invalid) | 
| `POST /api/sessions/{id}/cancel` | `session.cancel()` | None | `EngineResult<RaceResult>` (`state: "CANCELLED"`, runtime inventory reset) | 
| `GET /api/sessions/{id}/snapshot` | `session.snapshot()` | None | `SessionSnapshot` (live lap, stint progress, 4-corner tyre health, fuel, pending decision prompt, event log) | 
| `GET /api/sessions/{id}/result` | `session.lastResult()` | None | `RaceResult` (total time, flying vs pit time, per-lap trace, pit stop records, interventions, fidelity note) | 

## 16. Future Strategy Optimiser Integration Point (`PLANNED`)

* **Current Status:** The Stint-Boundary DAG Strategy Optimiser (`f1_sim::optimiser`, Mathematical Framework Section 10.4) is **PLANNED** (not yet implemented as a user-facing solver).

* **How It Reuses the Implemented Engine Evaluator:**

  * `LapStintEvaluator::evaluateStintRollout(l_start, l_end, role, initial_age, initial_health, bundle, vehicle, env, params, stage)` is **already implemented and tested** in `include/f1_sim/simulation/lap_evaluator.hpp`.

  * When `f1_sim::optimiser` is implemented later, Step 1 of the DAG solver (`SolveOptimalStrategyDAG` in Framework Section 13.2) will call `LapStintEvaluator::evaluateStintRollout(...)` to precompute the $\mathcal{O}(\vert{}\mathcal{R}\vert{} \cdot L^2)$ stint cost matrix $C_{\mathrm{stint}}(\ell_0, \ell_1, r, \sigma)$ and `LapStintEvaluator::evaluatePitStopLoss(...)` to compute $P_j(\ell_0, r)$.

  * After backtracking the top-$M$ optimal strategies $\pi^{(1)}, \ldots, \pi^{(M)}$ from the DAG, the optimiser can pass any candidate $\pi^{(m)}$ directly into `RaceSession` (`RaceMode::Preplanned`) to generate the full `RaceResult` trace, guaranteeing 100% mathematical identity between optimisation and simulation.

## 17. Implementation Status Summary & Onboarding Guide for Next Developer/Agent

### 17.1 Subsystem Status Matrix

| Subsystem / Deliverable | Status | Location | 
| ----- | ----- | ----- | 
| Mathematical Framework Specification (Stages M0–M5) | **IMPLEMENTED** | `docs/f1_simulator_mathematical_framework.md` | 
| 2026 British GP / Silverstone Dataset Batch | **IMPLEMENTED** | `data/{tracks,events,compounds}/*.json` | 
| Read-Only Data Access Layer (`f1_sim::data_access`) | **IMPLEMENTED** | `include/f1_sim/data_access/`, `src/data_access/`, `tests/data_access/dal_tests.cpp` | 
| C++17 Race Simulation Engine (`f1_sim::simulation`) | **IMPLEMENTED** | `include/f1_sim/simulation/`, `src/simulation/`, `tests/simulation/simulation_tests.cpp` | 
| Stint-Boundary DAG Strategy Optimiser (`f1_sim::optimiser`) | **PLANNED** | Reuses `LapStintEvaluator::evaluateStintRollout` | 
| Historical Telemetry Calibration Pipeline (FastF1/OpenF1) | **PLANNED** | Populates `PhysicalCompoundCatalogue` & `CalibratedModelParameters` | 
| Web Application UI & HTTP/WebSocket Adapter | **PLANNED** | Consumes `RaceSession` contract in Section 15 | 

### 17.2 How to Onboard the Next Developer / Agent

1. **Verify the Build & Tests First:** Run `make clean && make test` from the repository root. Confirm both `./build/dal_tests data` (9/9) and `./build/simulation_tests data` (8/8) pass with zero warnings.

2. **Respect the DAL & Data Integrity Boundary:** Never modify existing files under `data/` or `research/` as a side effect of code or tests, and never parse JSON outside `src/data_access/`.

3. **Use `RaceSession` for Stateful Workflows and `LapStintEvaluator` for Stateless Rollouts:**

   * When building the Web API adapter, instantiate one `RaceSession` per user session (or persist/restore `SessionSnapshot` via `snapshot()` / `restoreSnapshot()`).

   * When building the Strategy Optimiser, call `LapStintEvaluator::evaluateStintRollout` and `LapStintEvaluator::evaluatePitStopLoss`—never duplicate lap or tyre wear equations.