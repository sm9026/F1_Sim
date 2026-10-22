# F1_Sim — Simulation Engine Implementation & Engineering Decisions

**Subsystem:** `f1_sim::simulation`  
**Companion Whole-Program Reference:** `docs/architecture/program_blueprint.md`  
**Mathematical Authority:** `docs/f1_simulator_mathematical_framework.md`

---

## 1. Subsystem Inventory & Responsibilities

The C++17 Race Simulation Engine (`libf1_sim_engine.a`) is implemented across four public headers in `include/f1_sim/simulation/` and four translation units in `src/simulation/`:

| File Path | Primary Class / Structs | Architectural Role |
| :--- | :--- | :---
| `include/f1_sim/simulation/engine_types.hpp` | `ModelStage`, `RaceMode`, `SessionState`, `FidelityStatus`, `ExecutionPermission`, `EngineStatus`, `EngineResult<T>`, `CornerHealthVector`, `TyreSetState`, `VehicleConfig`, `EnvironmentScenarioConfig`, `ModelParameters`, `StintSpec`, `StintRuntimeRecord`, `CapabilityReadinessReport`, `LapTimeBreakdown`, `TyreStepBreakdown`, `PitStopRecord`, `LapTraceEntry`, `PendingDecisionPrompt`, `InteractiveDecision`, `StrategyInterventionRecord`, `SessionSnapshot`, `RaceResult` | Domain types, state representations, decision structures, diagnostics, and result traces. |
| `include/f1_sim/simulation/lap_evaluator.hpp` & `src/simulation/lap_evaluator.cpp` | `LapStintEvaluator`, `ResolvedCompoundParams`, `StintRolloutEvaluation` | Stateless, deterministic single-source mathematical evaluator implementing Equations (6.1)–(6.4), (7.1)–(7.6), (8.1)–(8.2), and (9.1)–(9.2). Shared across Preplanned mode, Interactive mode, inventory age-to-health derivation, and future Stint-Boundary DAG strategy optimisation. |
| `include/f1_sim/simulation/tyre_inventory.hpp` & `src/simulation/tyre_inventory.cpp` | `TyreInventoryManager` | Enforces the fixed 7-set physical tyre inventory (`S1, S2, S3, M1, M2, H1, H2`), age-only user configuration, dynamic starting-health derivation, deterministic set allocation, event-switch age resets, and post-run runtime wear resets. |
| `include/f1_sim/simulation/strategy_validator.hpp` & `src/simulation/strategy_validator.cpp` | `StrategyAndReadinessValidator` | Separates DAL bundle-load validity from simulation capability readiness (`assessReadiness`), validates initial strategies (`validateInitialStrategy`), and validates mid-race interactive decisions/extensions (`validateInteractiveDecision`). |
| `include/f1_sim/simulation/race_session.hpp` & `src/simulation/race_session.cpp` | `RaceSession` | Stateful session controller and state machine owning configuration, immutable `EventBundle` snapshot, runtime progress, pause/resume checkpointing, and terminal result generation. |

---

## 2. Key Engineering Decisions & Framework Reconciliation

### 2.1 Four-Corner Operational Interface vs. Baseline M0–M2 Shared Scalar Mathematics

* **Context:** The operational mandatory-stop rule specifies both an aggregate threshold ($\text{mean}(\mathbf{h}) < 30\%$) and an individual-corner threshold ($\min_i h_i < 12\%$), whereas `docs/f1_simulator_mathematical_framework.md` Section 1.3 and Section 7.3 specify that baseline stages M0–M2 bind all four corners (`FL, FR, RL, RR`) to a shared aggregate health scalar, deferring asymmetric four-corner evolution to Stage M3.
* **Implemented Resolution:**
  * Tyre health is stored and exposed throughout the engine as `CornerHealthVector` (`std::array<double, 4>` for `FL, FR, RL, RR`).
  * In baseline stages `M0_DeterministicBaseline`, `M1_TrackAwareCalibratedDry`, and `M2_DynamicEnvironmentDry`, `LapStintEvaluator::computeCornerAsymmetryMultipliers` returns `{1.0, 1.0, 1.0, 1.0}`, so all four corners evolve identically without silently introducing M3 asymmetric wear into the baseline.
  * When `ModelStage::M3_NonlinearCliffAndFourCorner` is explicitly selected, deterministic corner asymmetry multipliers $\lambda_{r,i}$ (derived from circuit left/right corner counts $N_L, N_R$ and front/rear weight distribution, normalized so $\frac{1}{4}\sum_i \lambda_{r,i} = 1.0$) differentiate the four corners.
  * `LapStintEvaluator::evaluateMandatoryStopThresholds` always evaluates both strict inequalities (`meanHealth() < 0.30` and `minCornerHealth() < 0.12`) across the four corner values.

### 2.2 Configurable Driver Mass Integration (Default 75.0 kg)

* **Context:** `Simulation_Engine_Design_Specification.md` Section 6.3 requires driver mass to default to `75.0 kg` and be adjustable for experiments, with total vehicle mass and fuel effects handled according to the mathematical framework rather than ad-hoc lap-time additions.
* **Implemented Resolution:**
  * In FIA Technical Regulations, the reference minimum car dry mass $M_{\mathrm{dry}} = 798.0\,\mathrm{kg}$ includes the nominal $75.0\,\mathrm{kg}$ driver + ballast.
  * `VehicleConfig` stores `base_dry_mass_with_ref_driver_kg = 798.0` and `driver_mass_kg = 75.0` (`driverMassDeltaKg() = driver_mass_kg - 75.0`).
  * At the default `75.0 kg` setting, `driverMassDeltaKg() == 0.0`, reducing identically to Framework Equations (6.2b) and (7.4).
  * When `driver_mass_kg` is varied, $\Delta M_{\mathrm{drv}} = M_{\mathrm{drv}} - 75.0\,\mathrm{kg}$ enters both the reversible mass pace offset $\Delta T_{\mathrm{fuel},k} = \gamma_{\mathrm{fuel}}(r)((M_{\mathrm{fuel},k} + \Delta M_{\mathrm{drv}}) - M_{\mathrm{ref}})$ and the mechanical tyre load multiplier $F_{\mathrm{usage}}(k) = 1 + \kappa_{\mathrm{load}} \frac{(M_{\mathrm{fuel},k} + \Delta M_{\mathrm{drv}}) - M_{\mathrm{ref}}}{M_{\mathrm{dry}} + M_{\mathrm{ref}}}$.

### 2.3 Pre-Race Tyre Age to Starting Health Derivation

* **Context:** Users may edit a tyre set's prior age (`configured_age_laps >= 0`) while no simulation is running, but may never directly edit tyre health.
* **Implemented Resolution:**
  * For `configured_age_laps == 0`, derived starting health is `1.0` (`100%`) across all four corners by definition.
  * For `configured_age_laps > 0`, `LapStintEvaluator::deriveStartingHealthFromAge` applies the static baseline depletion rate $D_{\mathrm{base}}(c, r, i)$ (Equation 7.3 under reference pre-race conditions $F_{\mathrm{env}} = 1.0, F_{\mathrm{usage}} = 1.0$):
    $$h_{\mathrm{init},i}(a_0) = \max\Big(0.0,\; 1.0 - a_0 \cdot D_{\mathrm{base}}(c, r, i)\Big)$$
  * If `configured_age_laps > 0` is set when the mapped physical compound has an uncalibrated (`null`) `base_wear_rate_per_lap` ($d_{0,c}$), the engine does **not** invent a health curve: it sets `derived_starting_health = std::nullopt`, marks `health_status = TyreSetHealthStatus::MissingCalibrationForAgeDerivation`, emits an `InsufficientDataForCapability` diagnostic, and blocks simulation start until $d_{0,c}$ is supplied or age is reset to `0`.

### 2.4 Preplanned Shortfall Cascade vs. Interactive Re-Planning

* **Preplanned Mode (`RaceMode::Preplanned`):**
  * When a mandatory health threshold (`< 30%` mean or `< 12%` any corner) ends stint $j$ early at lap $k < L$, the stint's shortfall against its current effective target is `shortfall = effective_target_laps - actual_laps_completed`.
  * `accumulated_shortfall_laps` is incremented by `shortfall`, and stint $j+1$'s uncapped cascaded target becomes `original_target_laps + accumulated_shortfall_laps`.
  * Its `effective_target_laps` is `std::min(uncapped_cascaded_target_laps, L - completed_laps)`, ensuring that on the final stint the car never runs past race distance $L$.
  * Both `uncapped_cascaded_target_laps` (e.g., `37` in the `10, 20, 30` specification example) and `effective_target_laps` (`35`, capped by remaining race distance `60 - 25 = 35`) are recorded in `StintRuntimeRecord` and `PitStopRecord`.
* **Interactive Mode (`RaceMode::Interactive`):**
  * At any planned stint boundary or mandatory health stop at lap $k < L$, the session transitions to `SessionState::DecisionRequired` before executing the pit stop.
  * If a mandatory health stop triggered, `PendingDecisionPrompt::mandatory_stop_required` is `true`, `ExtendCurrentStint` is rejected with `EngineStatus::MandatoryStopCannotBeOverridden`, and the Preplanned shortfall cascade is **never** automatically applied to remaining stints.
  * When `ExtendCurrentStint` is chosen at a non-mandatory planned boundary, `extension_laps` is added to the current stint's completed laps (`actual_laps + extension_laps`) and is **never** automatically subtracted from the next stint; the caller must supply `revised_remaining_stints` such that `extension_laps + sum(revised_remaining_stints) == L - k`.
  * Applying a valid decision transitions the session to `SessionState::Paused` so that an explicit `resume()` call is required to continue lap execution.
