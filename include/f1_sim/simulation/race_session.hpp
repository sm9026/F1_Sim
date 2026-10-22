#ifndef F1_SIM_SIMULATION_RACE_SESSION_HPP
#define F1_SIM_SIMULATION_RACE_SESSION_HPP

#include "f1_sim/data_access/data_access_layer.hpp"
#include "f1_sim/simulation/engine_types.hpp"
#include "f1_sim/simulation/lap_evaluator.hpp"
#include "f1_sim/simulation/strategy_validator.hpp"
#include "f1_sim/simulation/tyre_inventory.hpp"

#include <memory>

namespace f1_sim {
namespace simulation {

/**
 * @brief Interface-independent C++ Race Simulation Engine and Session Controller
 *        (`f1_sim::simulation::RaceSession`).
 *
 * Implements the complete race simulation lifecycle, integrating with `DataAccessLayer`
 * for read-only local dataset resolution and supporting both:
 *  - `RaceMode::Preplanned` (automated execution with mandatory-stop shortfall cascading)
 *  - `RaceMode::Interactive` (pauses at planned pit windows and mandatory stops, emitting
 *    structured `PendingDecisionPrompt` snapshots and validating user plan revisions/extensions)
 *
 * State Separation Guarantees (Architecture Spec Section 7):
 *  1. Configuration state (driver mass, environment, model parameters, strategy, configured tyre ages)
 *     persists across runs until changed by the caller.
 *  2. Immutable `EventBundle` snapshot is loaded through the DAL and reused safely across runs.
 *  3. Runtime state (lap index, current stint/set, runtime tyre wear/age, fuel, elapsed time,
 *     stops, shortfalls, pending decision) is isolated to the current run and automatically
 *     reset to the configured pre-race condition when a run completes, fails, or is cancelled.
 *  4. Result/trace state (`RaceResult`) is preserved after run termination for inspection.
 */
class RaceSession {
public:
    explicit RaceSession(std::shared_ptr<data_access::DataAccessLayer> dal);

    /**
     * @brief Select and load an event bundle by `event_id` through the DAL.
     *
     * Changing the selected event/track refreshes the immutable `EventBundle` and
     * resets all 7 tyre sets' configured ages to default (0 laps, 100% health).
     * Rejected if a simulation is currently active (`Running`, `Paused`, `DecisionRequired`).
     */
    EngineResult<CapabilityReadinessReport> selectEvent(const std::string& event_id);

    /**
     * @brief Bind an already-loaded immutable `EventBundle` directly (e.g., for reuse across
     *        multiple sessions or in unit test fixtures). If `reset_inventory_ages` is true
     *        or the event/track changed, resets configured tyre ages to 0 laps.
     */
    EngineResult<CapabilityReadinessReport> loadFromEventBundle(
        const EventBundle& bundle,
        bool reset_inventory_ages = true);

    // ========================================================================
    // Configuration Operations (Rejected while Running / Paused / DecisionRequired)
    // ========================================================================
    EngineResult<void> setRaceMode(RaceMode mode);
    EngineResult<void> setModelStage(ModelStage stage);
    EngineResult<void> setDriverMassKg(double driver_mass_kg);
    EngineResult<void> setVehicleConfig(const VehicleConfig& vehicle_config);
    EngineResult<void> setEnvironmentConfig(const EnvironmentScenarioConfig& env_config);
    EngineResult<void> setModelParameters(const ModelParameters& model_params);
    EngineResult<void> setStrategyPlan(const StrategyPlan& strategy_plan);

    /**
     * @brief Configure the pre-race age (in integer laps >= 0) of one physical tyre set
     *        ("S1", "S2", "S3", "M1", "M2", "H1", "H2").
     *        Health cannot be edited directly; it is derived automatically from `age_laps`.
     *        Rejected while the simulation is `Running`, `Paused`, or `DecisionRequired`.
     */
    EngineResult<TyreSetState> setConfiguredTyreAge(const std::string& set_id, int age_laps);

    /**
     * @brief Reset all 7 tyre sets' configured ages to 0 laps (100% health).
     *        Rejected while a simulation is active.
     */
    EngineResult<void> resetConfiguredTyreAgesToDefault();

    // ========================================================================
    // Readiness & Validation Operations
    // ========================================================================
    CapabilityReadinessReport assessReadiness() const;

    /**
     * @brief Validate both capability readiness and the configured strategy plan.
     *        Transitions session to `SessionState::Validated` if valid and executable.
     */
    EngineResult<CapabilityReadinessReport> validate();

    // ========================================================================
    // Session Lifecycle & Execution Operations
    // ========================================================================
    /**
     * @brief Start a new race simulation run from lap 0.
     *
     * Resets any prior terminal runtime state to the configured pre-race condition,
     * validates configuration & strategy, fits the opening stint's assigned tyre set,
     * and transitions to `SessionState::Running`.
     */
    EngineResult<SessionSnapshot> start();

    /**
     * @brief Advance the simulation by exactly one racing lap (`k -> k + 1`).
     *
     * Requires `SessionState::Running`.
     * Evaluates flying lap time (Eq. 6.1), updates fuel and tyre health (Eq. 7.1),
     * checks mandatory tyre-health thresholds (< 30% mean or < 12% any corner),
     * and handles stint boundaries according to the active `RaceMode`:
     *  - In `Preplanned` mode, automatically executes planned or mandatory pit stops
     *    (applying the shortfall cascade on early mandatory stops) and remains `Running`
     *    until lap `L` completes (`SessionState::Completed`).
     *  - In `Interactive` mode, if lap `k < L` reaches a planned stint boundary or
     *    triggers a mandatory health stop, transitions to `SessionState::DecisionRequired`
     *    before executing the stop and emits a `PendingDecisionPrompt`.
     */
    EngineResult<LapTraceEntry> advanceLap();

    /**
     * @brief Run forward continuously until the race completes (`SessionState::Completed`),
     *        pauses for an interactive decision (`SessionState::DecisionRequired`),
     *        or encounters a terminal failure (`SessionState::Failed`).
     */
    EngineResult<SessionSnapshot> runUntilStopOrCompletion();

    /**
     * @brief Pause an active run at the current safe lap boundary (`Running -> Paused`).
     */
    EngineResult<SessionSnapshot> pause();

    /**
     * @brief Resume a paused run (`Paused -> Running`).
     *        Rejected if the session is in `DecisionRequired` (a valid decision must be
     *        committed via `applyDecision` first) or in a terminal/unstarted state.
     */
    EngineResult<SessionSnapshot> resume();

    /**
     * @brief Submit and validate a user decision when in `SessionState::DecisionRequired`
     *        (Interactive mode only).
     *
     * If valid:
     *  - Commits any plan revision and logs a `StrategyInterventionRecord`.
     *  - If the decision pits at the current lap (`KeepOriginalPlan` or `PitNowWithRevisedPlan`),
     *    executes the pit stop atomically at the current lap and fits the next stint's tyre set.
     *  - If the decision extends the current stint (`ExtendCurrentStint`, allowed only when
     *    `mandatory_stop_required == false`), adds `extension_laps` to the current stint's
     *    target and updates future stints.
     *  - Transitions from `SessionState::DecisionRequired` to `SessionState::Paused` so the
     *    caller can explicitly call `resume()` (merely applying a decision does not silently
     *    advance laps).
     *
     * If invalid:
     *  - Keeps the session at `SessionState::DecisionRequired` without mutating race state
     *    and returns structured validation diagnostics.
     */
    EngineResult<SessionSnapshot> applyDecision(const InteractiveDecision& decision);

    /**
     * @brief Cancel an active (`Running`, `Paused`, `DecisionRequired`) simulation run.
     *        Transitions to `SessionState::Cancelled`, preserves partial trace/result, and
     *        immediately resets runtime tyre inventory state to the configured pre-race condition.
     */
    EngineResult<RaceResult> cancel();

    /**
     * @brief Prepare the session for a fresh run after a terminal state (`Completed`, `Failed`,
     *        `Cancelled`), restoring `SessionState::Configured` while keeping all user configuration
     *        and configured tyre ages intact.
     */
    EngineResult<void> resetForNewRun();

    // ========================================================================
    // Read-Only Queries & Checkpointing
    // ========================================================================
    SessionState state() const noexcept { return state_; }
    RaceMode raceMode() const noexcept { return race_mode_; }
    ModelStage modelStage() const noexcept { return model_stage_; }
    const EventBundle* eventBundle() const noexcept {
        return bundle_.has_value() ? &(*bundle_) : nullptr;
    }
    const VehicleConfig& vehicleConfig() const noexcept { return vehicle_config_; }
    const EnvironmentScenarioConfig& environmentConfig() const noexcept { return env_config_; }
    const ModelParameters& modelParameters() const noexcept { return model_params_; }
    const StrategyPlan& strategyPlan() const noexcept { return strategy_plan_; }
    const std::vector<TyreSetState>& tyreInventory() const noexcept { return inventory_.sets(); }
    const std::optional<PendingDecisionPrompt>& pendingDecision() const noexcept { return pending_decision_; }
    const std::optional<RaceResult>& lastResult() const noexcept { return last_result_; }

    /**
     * @brief Obtain a complete, non-mutating snapshot of the session state.
     */
    SessionSnapshot snapshot() const;

    /**
     * @brief Restore session state from a previously captured `SessionSnapshot` for
     *        deterministic pause/resume across checkpoints.
     */
    EngineResult<void> restoreSnapshot(const SessionSnapshot& snap);

private:
    void appendEvent(SimulationEventType type, int race_lap, int stint_index, std::string message);
    EngineResult<void> executePitStopAtCurrentLap(
        PitStopReason reason,
        const MandatoryStopCheckResult& mandatory_check,
        bool coincided_with_planned_boundary,
        bool apply_preplanned_cascade);
    void finalizeTerminalState(SessionState terminal_state, const std::string& summary_message);
    std::vector<StintSpec> currentPlanAsSpecs() const;

    std::shared_ptr<data_access::DataAccessLayer> dal_;
    std::optional<EventBundle> bundle_;
    LoadStatus last_bundle_load_status_ = LoadStatus::FileNotFound;
    std::vector<Diagnostic> last_bundle_load_diagnostics_;

    SessionState state_ = SessionState::Unconfigured;
    RaceMode race_mode_ = RaceMode::Preplanned;
    ModelStage model_stage_ = ModelStage::M0_DeterministicBaseline;
    FidelityStatus fidelity_status_ = FidelityStatus::UncalibratedIncomplete;

    VehicleConfig vehicle_config_{};
    EnvironmentScenarioConfig env_config_{};
    ModelParameters model_params_{};
    StrategyPlan strategy_plan_{};
    TyreInventoryManager inventory_;

    // Active run state
    int completed_laps_ = 0;
    int current_stint_index_ = 0;
    std::string active_tyre_set_id_;
    int accumulated_shortfall_laps_ = 0;
    double elapsed_race_time_s_ = 0.0;
    double total_flying_lap_time_s_ = 0.0;
    double total_pit_stop_loss_s_ = 0.0;
    double current_fuel_remaining_kg_ = 0.0;
    double total_fuel_consumed_kg_ = 0.0;

    std::vector<StintRuntimeRecord> stint_records_;
    std::optional<PendingDecisionPrompt> pending_decision_;
    MandatoryStopCheckResult pending_mandatory_check_{};
    bool pending_coincided_with_planned_boundary_ = false;

    std::vector<LapTraceEntry> lap_trace_;
    std::vector<PitStopRecord> pit_stops_;
    std::vector<StrategyInterventionRecord> interventions_;
    std::vector<SimulationEvent> event_log_;
    std::vector<Diagnostic> session_diagnostics_;
    std::optional<RaceResult> last_result_;
};

} // namespace simulation
} // namespace f1_sim

#endif // F1_SIM_SIMULATION_RACE_SESSION_HPP
