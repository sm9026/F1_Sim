#include "f1_sim/simulation/race_session.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace f1_sim {
namespace simulation {

RaceSession::RaceSession(std::shared_ptr<data_access::DataAccessLayer> dal)
    : dal_(std::move(dal)) {}

void RaceSession::appendEvent(
    SimulationEventType type,
    int race_lap,
    int stint_index,
    std::string message) {
    SimulationEvent ev;
    ev.event_sequence_id = event_log_.size() + 1;
    ev.type = type;
    ev.race_lap = race_lap;
    ev.stint_index = stint_index;
    ev.message = std::move(message);
    event_log_.push_back(std::move(ev));
}

EngineResult<CapabilityReadinessReport> RaceSession::selectEvent(const std::string& event_id) {
    if (isActiveRunState(state_)) {
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change selected event while a simulation is running or paused.",
                event_id,
                "",
                "/session/event_id",
                "Session not active",
                toString(state_)
            }});
    }

    if (!dal_) {
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::InsufficientCapabilityOrCalibration,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::UnexpectedInternalFailure,
                "DataAccessLayer instance is null.",
                event_id,
                "",
                "/dal",
                "Valid DataAccessLayer",
                "nullptr"
            }});
    }

    auto load_res = dal_->loadEventBundle(event_id);
    last_bundle_load_status_ = load_res.status;
    last_bundle_load_diagnostics_ = load_res.diagnostics;

    if (!load_res.ok()) {
        bundle_ = std::nullopt;
        state_ = SessionState::Unconfigured;
        auto readiness = assessReadiness();
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::ValidationFailed, readiness.diagnostics);
    }

    return loadFromEventBundle(*load_res.value, true);
}

EngineResult<CapabilityReadinessReport> RaceSession::loadFromEventBundle(
    const EventBundle& bundle,
    bool reset_inventory_ages) {
    if (isActiveRunState(state_)) {
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot bind a new EventBundle while a simulation is running or paused.",
                bundle.event_config.event_id,
                "",
                "/session/event_bundle",
                "Session not active",
                toString(state_)
            }});
    }

    const bool event_changed =
        !bundle_.has_value() ||
        bundle_->event_config.event_id != bundle.event_config.event_id ||
        bundle_->event_config.track_id != bundle.event_config.track_id;

    bundle_ = bundle;
    last_bundle_load_status_ = LoadStatus::Success;

    if (reset_inventory_ages || event_changed) {
        inventory_.resetForNewEvent(&(*bundle_), vehicle_config_, model_params_, model_stage_);
    } else {
        inventory_.recomputeDerivedStartingHealth(&(*bundle_), vehicle_config_, model_params_, model_stage_);
    }

    state_ = SessionState::Configured;
    event_log_.clear();
    appendEvent(
        SimulationEventType::EventSelected,
        0,
        0,
        "Selected event '" + bundle.event_config.event_id + "' on track '" +
            bundle.track_layout.track_id + "' (" +
            std::to_string(bundle.event_config.race_distance_laps) + " laps); tyre inventory reset to defaults.");

    auto readiness = assessReadiness();
    fidelity_status_ = readiness.fidelity_status;
    return EngineResult<CapabilityReadinessReport>::success(std::move(readiness));
}

EngineResult<void> RaceSession::setRaceMode(RaceMode mode) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change RaceMode while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/race_mode",
                "Session not active",
                toString(state_)
            }});
    }
    race_mode_ = mode;
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setModelStage(ModelStage stage) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change ModelStage while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/model_stage",
                "Session not active",
                toString(state_)
            }});
    }
    model_stage_ = stage;
    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setDriverMassKg(double driver_mass_kg) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change driver_mass_kg while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/vehicle_config/driver_mass_kg",
                "Session not active",
                toString(state_)
            }});
    }
    if (!std::isfinite(driver_mass_kg) || driver_mass_kg < 40.0 || driver_mass_kg > 160.0) {
        return EngineResult<void>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "driver_mass_kg must be finite and within [40.0, 160.0] kg.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/vehicle_config/driver_mass_kg",
                "[40.0, 160.0]",
                std::to_string(driver_mass_kg)
            }});
    }
    vehicle_config_.driver_mass_kg = driver_mass_kg;
    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setVehicleConfig(const VehicleConfig& vehicle_config) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change VehicleConfig while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/vehicle_config",
                "Session not active",
                toString(state_)
            }});
    }
    vehicle_config_ = vehicle_config;
    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setEnvironmentConfig(const EnvironmentScenarioConfig& env_config) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change EnvironmentScenarioConfig while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/environment_config",
                "Session not active",
                toString(state_)
            }});
    }
    env_config_ = env_config;
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setModelParameters(const ModelParameters& model_params) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot change ModelParameters while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/model_parameters",
                "Session not active",
                toString(state_)
            }});
    }
    model_params_ = model_params;
    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<void> RaceSession::setStrategyPlan(const StrategyPlan& strategy_plan) {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot replace StrategyPlan while a simulation is running or paused (use applyDecision in Interactive mode).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/strategy",
                "Session not active",
                toString(state_)
            }});
    }
    strategy_plan_ = strategy_plan;
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

EngineResult<TyreSetState> RaceSession::setConfiguredTyreAge(const std::string& set_id, int age_laps) {
    auto res = inventory_.setConfiguredAge(
        set_id,
        age_laps,
        isActiveRunState(state_),
        eventBundle(),
        vehicle_config_,
        model_params_,
        model_stage_);
    if (!res.ok()) {
        return EngineResult<TyreSetState>::failure(res.status, std::move(res.diagnostics));
    }

    if (bundle_.has_value() && state_ == SessionState::Validated) {
        state_ = SessionState::Configured;
    }

    const TyreSetState* updated = inventory_.findSet(set_id);
    appendEvent(
        SimulationEventType::InventoryAgeUpdated,
        0,
        0,
        "Configured age for set '" + set_id + "' set to " + std::to_string(age_laps) + " laps.");

    return EngineResult<TyreSetState>::success(*updated, EngineStatus::Ok, std::move(res.diagnostics));
}

EngineResult<void> RaceSession::resetConfiguredTyreAgesToDefault() {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot reset configured tyre ages while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/inventory",
                "Session not active",
                toString(state_)
            }});
    }
    inventory_.resetForNewEvent(eventBundle(), vehicle_config_, model_params_, model_stage_);
    if (bundle_.has_value()) {
        state_ = SessionState::Configured;
    }
    return EngineResult<void>::success();
}

CapabilityReadinessReport RaceSession::assessReadiness() const {
    return StrategyAndReadinessValidator::assessReadiness(
        eventBundle(),
        last_bundle_load_status_,
        last_bundle_load_diagnostics_,
        vehicle_config_,
        env_config_,
        model_params_,
        inventory_,
        model_stage_,
        dal_.get());
}

EngineResult<CapabilityReadinessReport> RaceSession::validate() {
    if (isActiveRunState(state_)) {
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot re-run pre-race validate() while a simulation is running or paused.",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "CONFIGURED or VALIDATED or terminal state",
                toString(state_)
            }});
    }

    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);

    CapabilityReadinessReport readiness = assessReadiness();
    fidelity_status_ = readiness.fidelity_status;

    if (!bundle_.has_value() || !readiness.bundle_load_valid) {
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::ValidationFailed, readiness.diagnostics);
    }

    auto strat_res = StrategyAndReadinessValidator::validateInitialStrategy(
        strategy_plan_, *bundle_, inventory_);
    if (!strat_res.ok()) {
        readiness.user_input_valid = false;
        readiness.diagnostics.insert(
            readiness.diagnostics.end(),
            strat_res.diagnostics.begin(),
            strat_res.diagnostics.end());
        return EngineResult<CapabilityReadinessReport>::failure(
            EngineStatus::ValidationFailed, std::move(readiness.diagnostics));
    }

    if (!readiness.canExecute()) {
        EngineStatus err_status = EngineStatus::InsufficientCapabilityOrCalibration;
        if (readiness.execution_permission == ExecutionPermission::BlockedUnsupportedWetConditions) {
            err_status = EngineStatus::UnsupportedWetConditions;
        } else if (readiness.execution_permission == ExecutionPermission::BlockedInvalidInput) {
            err_status = EngineStatus::ValidationFailed;
        }
        return EngineResult<CapabilityReadinessReport>::failure(
            err_status, std::move(readiness.diagnostics));
    }

    state_ = SessionState::Validated;
    appendEvent(
        SimulationEventType::SessionValidated,
        0,
        0,
        std::string("Session and strategy validated (fidelity: ") +
            toString(readiness.fidelity_status) + ").");

    return EngineResult<CapabilityReadinessReport>::success(
        readiness, EngineStatus::Ok, readiness.diagnostics);
}

EngineResult<SessionSnapshot> RaceSession::start() {
    if (isActiveRunState(state_)) {
        return EngineResult<SessionSnapshot>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Simulation is already active; cannot call start().",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "CONFIGURED or VALIDATED",
                toString(state_)
            }});
    }

    auto val_res = validate();
    if (!val_res.ok()) {
        return EngineResult<SessionSnapshot>::failure(val_res.status, std::move(val_res.diagnostics));
    }

    auto alloc_res = StrategyAndReadinessValidator::validateInitialStrategy(
        strategy_plan_, *bundle_, inventory_);
    if (!alloc_res.ok()) {
        return EngineResult<SessionSnapshot>::failure(alloc_res.status, std::move(alloc_res.diagnostics));
    }
    const std::vector<std::string>& allocated_sets = *alloc_res.value;

    inventory_.resetRuntimeStateToConfigured();
    completed_laps_ = 0;
    current_stint_index_ = 0;
    active_tyre_set_id_.clear();
    accumulated_shortfall_laps_ = 0;
    elapsed_race_time_s_ = 0.0;
    total_flying_lap_time_s_ = 0.0;
    total_pit_stop_loss_s_ = 0.0;
    current_fuel_remaining_kg_ = vehicle_config_.initial_fuel_load_kg;
    total_fuel_consumed_kg_ = 0.0;

    stint_records_.clear();
    pending_decision_ = std::nullopt;
    pending_mandatory_check_ = MandatoryStopCheckResult{};
    pending_coincided_with_planned_boundary_ = false;
    lap_trace_.clear();
    pit_stops_.clear();
    interventions_.clear();
    session_diagnostics_ = val_res.value->diagnostics;
    last_result_ = std::nullopt;

    int cum_start_lap = 1;
    for (std::size_t j = 0; j < strategy_plan_.stints.size(); ++j) {
        const StintSpec& spec = strategy_plan_.stints[j];
        const TyreSetState* set_state = inventory_.findSet(allocated_sets[j]);
        StintRuntimeRecord rec;
        rec.stint_index = static_cast<int>(j);
        rec.role = spec.role;
        rec.physical_compound = *bundle_->role_mapping.compoundForRole(spec.role);
        rec.assigned_set_id = allocated_sets[j];
        rec.set_configured_age_at_fit_laps = set_state ? set_state->configured_age_laps : 0;
        rec.set_starting_health_at_fit =
            (set_state && set_state->derived_starting_health.has_value())
                ? *set_state->derived_starting_health
                : CornerHealthVector::uniform(1.0);
        rec.start_race_lap = cum_start_lap;
        rec.original_target_laps = spec.target_laps;
        rec.uncapped_cascaded_target_laps = spec.target_laps;
        rec.effective_target_laps = spec.target_laps;
        rec.actual_laps_completed = 0;
        rec.shortfall_laps = 0;
        rec.accumulated_shortfall_into_stint = 0;
        rec.started = (j == 0);
        rec.completed = false;
        rec.ended_by_mandatory_stop = false;
        rec.cumulative_flying_time_s = 0.0;
        rec.final_health = rec.set_starting_health_at_fit;
        stint_records_.push_back(std::move(rec));
        cum_start_lap += spec.target_laps;
    }

    auto fit_res = inventory_.fitSetForStint(stint_records_[0].assigned_set_id, 0);
    if (!fit_res.ok()) {
        finalizeTerminalState(SessionState::Failed, "Failed to fit opening tyre set.");
        return EngineResult<SessionSnapshot>::failure(fit_res.status, std::move(fit_res.diagnostics));
    }
    active_tyre_set_id_ = stint_records_[0].assigned_set_id;
    state_ = SessionState::Running;

    appendEvent(
        SimulationEventType::SessionStarted,
        0,
        0,
        std::string("Started race simulation in ") + toString(race_mode_) +
            " mode on set " + active_tyre_set_id_ + " (" +
            data_access::toString(stint_records_[0].role) + " -> " +
            data_access::toString(stint_records_[0].physical_compound) + ").");

    return EngineResult<SessionSnapshot>::success(snapshot());
}

EngineResult<LapTraceEntry> RaceSession::advanceLap() {
    if (state_ != SessionState::Running) {
        return EngineResult<LapTraceEntry>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot advanceLap() when session is in state ") + toString(state_) +
                    " (requires RUNNING).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "RUNNING",
                toString(state_)
            }});
    }

    const int total_laps = bundle_->event_config.race_distance_laps;
    const int next_lap = completed_laps_ + 1;
    StintRuntimeRecord& active_stint = stint_records_[static_cast<std::size_t>(current_stint_index_)];
    const TyreSetState* active_set = inventory_.findSet(active_tyre_set_id_);
    if (active_set == nullptr) {
        finalizeTerminalState(SessionState::Failed, "Active tyre set missing during lap evaluation.");
        return EngineResult<LapTraceEntry>::failure(
            EngineStatus::RuntimeConstraintFailure, session_diagnostics_);
    }

    const ResolvedCompoundParams comp = LapStintEvaluator::resolveCompoundParameters(
        *bundle_, active_stint.physical_compound, model_params_);
    const int stint_lap_m = active_stint.actual_laps_completed + 1;
    const CornerHealthVector health_start = active_set->runtime_health;
    const int age_start = active_set->runtime_age_laps;

    auto lap_res = LapStintEvaluator::evaluateFlyingLap(
        next_lap,
        current_stint_index_,
        stint_lap_m,
        health_start,
        *bundle_,
        comp,
        vehicle_config_,
        model_params_,
        model_stage_);
    if (!lap_res.ok()) {
        session_diagnostics_.insert(
            session_diagnostics_.end(),
            lap_res.diagnostics.begin(),
            lap_res.diagnostics.end());
        finalizeTerminalState(SessionState::Failed, "Flying-lap evaluation failed.");
        return EngineResult<LapTraceEntry>::failure(lap_res.status, std::move(lap_res.diagnostics));
    }

    auto step_res = LapStintEvaluator::stepTyreHealth(
        next_lap,
        health_start,
        *bundle_,
        comp,
        vehicle_config_,
        env_config_,
        model_params_,
        model_stage_);
    if (!step_res.ok()) {
        session_diagnostics_.insert(
            session_diagnostics_.end(),
            step_res.diagnostics.begin(),
            step_res.diagnostics.end());
        finalizeTerminalState(SessionState::Failed, "Tyre health step failed.");
        return EngineResult<LapTraceEntry>::failure(step_res.status, std::move(step_res.diagnostics));
    }

    const double lap_time_s = lap_res.value->total_flying_lap_time_s;
    elapsed_race_time_s_ += lap_time_s;
    total_flying_lap_time_s_ += lap_time_s;
    active_stint.cumulative_flying_time_s += lap_time_s;
    active_stint.actual_laps_completed += 1;
    active_stint.final_health = step_res.value->health_end;

    double mid_fuel_kg = 0.0;
    double end_fuel_kg = 0.0;
    LapStintEvaluator::computeLapFuelState(
        next_lap, *bundle_, vehicle_config_, mid_fuel_kg, end_fuel_kg);
    current_fuel_remaining_kg_ = end_fuel_kg;
    total_fuel_consumed_kg_ =
        std::max(0.0, vehicle_config_.initial_fuel_load_kg - current_fuel_remaining_kg_);

    inventory_.updateActiveSetAfterLap(active_tyre_set_id_, step_res.value->health_end);
    completed_laps_ = next_lap;

    const MandatoryStopCheckResult mand_check =
        LapStintEvaluator::evaluateMandatoryStopThresholds(step_res.value->health_end);

    LapTraceEntry trace;
    trace.race_lap = next_lap;
    trace.stint_index = current_stint_index_;
    trace.stint_lap_number = stint_lap_m;
    trace.tyre_set_id = active_tyre_set_id_;
    trace.role = active_stint.role;
    trace.physical_compound = active_stint.physical_compound;
    trace.tyre_age_start_laps = age_start;
    trace.tyre_age_end_laps = age_start + 1;
    trace.health_start = health_start;
    trace.health_end = step_res.value->health_end;
    trace.fuel_mid_lap_kg = mid_fuel_kg;
    trace.fuel_end_lap_kg = end_fuel_kg;
    trace.effective_vehicle_mass_mid_lap_kg = vehicle_config_.effectiveDryMassKg() + mid_fuel_kg;
    trace.lap_breakdown = *lap_res.value;
    trace.tyre_breakdown = *step_res.value;
    trace.cumulative_elapsed_time_s = elapsed_race_time_s_;
    trace.mandatory_check_after_lap = mand_check;

    appendEvent(
        SimulationEventType::LapCompleted,
        next_lap,
        current_stint_index_,
        "Completed lap " + std::to_string(next_lap) + "/" + std::to_string(total_laps) +
            " in " + std::to_string(lap_time_s) + " s (mean health: " +
            std::to_string(step_res.value->health_end.meanHealthPct()) + "%).");

    if (completed_laps_ == total_laps) {
        active_stint.completed = true;
        lap_trace_.push_back(trace);
        finalizeTerminalState(SessionState::Completed, "Race completed at lap " + std::to_string(total_laps) + ".");
        return EngineResult<LapTraceEntry>::success(std::move(trace), EngineStatus::RaceCompleted);
    }

    const bool planned_boundary =
        (active_stint.actual_laps_completed >= active_stint.effective_target_laps);
    const bool mandatory_trigger = mand_check.mandatory_stop_required;

    if (mandatory_trigger) {
        appendEvent(
            SimulationEventType::MandatoryStopTriggered,
            completed_laps_,
            current_stint_index_,
            std::string("Mandatory tyre-health threshold breached after lap ") +
                std::to_string(completed_laps_) + " (mean=" +
                std::to_string(mand_check.observed_mean_health * 100.0) + "%, min_corner=" +
                std::to_string(mand_check.observed_min_corner_health * 100.0) + "%).");
    }

    if (mandatory_trigger || planned_boundary) {
        if (race_mode_ == RaceMode::Preplanned) {
            const PitStopReason stop_reason =
                (mandatory_trigger && planned_boundary)
                    ? PitStopReason::MandatoryAndPlannedBoundary
                    : (mandatory_trigger
                           ? PitStopReason::MandatoryHealthThreshold
                           : PitStopReason::PlannedStrategyBoundary);

            auto pit_res = executePitStopAtCurrentLap(
                stop_reason, mand_check, mandatory_trigger && planned_boundary, true);
            if (!pit_res.ok()) {
                lap_trace_.push_back(trace);
                finalizeTerminalState(
                    SessionState::Failed,
                    "Preplanned pit stop failed at lap " + std::to_string(completed_laps_) +
                        " (no remaining stints to cover remaining race distance).");
                return EngineResult<LapTraceEntry>::failure(pit_res.status, std::move(pit_res.diagnostics));
            }

            trace.pit_stop_occurred_after_lap = true;
            trace.pit_stop_loss_after_lap_s = pit_stops_.back().total_stop_time_loss_s;
            trace.pit_stop_reason = pit_stops_.back().reason;
            trace.cumulative_elapsed_time_s = elapsed_race_time_s_;
            lap_trace_.push_back(trace);
            return EngineResult<LapTraceEntry>::success(std::move(trace), EngineStatus::Ok);
        } else {
            const int shortfall =
                std::max(0, active_stint.effective_target_laps - active_stint.actual_laps_completed);
            active_stint.shortfall_laps = shortfall;
            active_stint.ended_by_mandatory_stop = mandatory_trigger;

            PendingDecisionPrompt prompt;
            prompt.reason = mandatory_trigger
                ? DecisionReason::MandatoryHealthStopReplanRequired
                : DecisionReason::PlannedStintBoundary;
            prompt.completed_race_lap = completed_laps_;
            prompt.remaining_race_laps = total_laps - completed_laps_;
            prompt.current_stint_index = current_stint_index_;
            prompt.current_tyre_set_id = active_tyre_set_id_;
            prompt.current_role = active_stint.role;
            prompt.current_compound = active_stint.physical_compound;
            prompt.current_stint_actual_laps = active_stint.actual_laps_completed;
            prompt.current_stint_target_laps = active_stint.effective_target_laps;
            prompt.current_stint_shortfall_laps = shortfall;
            prompt.current_tyre_health = step_res.value->health_end;
            prompt.current_fuel_remaining_kg = current_fuel_remaining_kg_;
            prompt.cumulative_elapsed_time_s = elapsed_race_time_s_;
            prompt.mandatory_stop_required = mandatory_trigger;
            prompt.extension_permitted = !mandatory_trigger;
            prompt.current_stint_plan = stint_records_;
            prompt.explanation = mandatory_trigger
                ? "Mandatory tyre-health threshold crossed after lap " +
                      std::to_string(completed_laps_) +
                      ". Extension is not permitted; please confirm or revise remaining stints to cover " +
                      std::to_string(prompt.remaining_race_laps) + " remaining laps."
                : "Planned stint boundary reached after lap " + std::to_string(completed_laps_) +
                      ". Keep original plan, extend current stint, or revise remaining stints.";

            pending_decision_ = std::move(prompt);
            pending_mandatory_check_ = mand_check;
            pending_coincided_with_planned_boundary_ = (mandatory_trigger && planned_boundary);
            state_ = SessionState::DecisionRequired;

            appendEvent(
                SimulationEventType::DecisionRequiredEmitted,
                completed_laps_,
                current_stint_index_,
                pending_decision_->explanation);

            lap_trace_.push_back(trace);
            return EngineResult<LapTraceEntry>::success(
                std::move(trace), EngineStatus::DecisionRequired);
        }
    }

    lap_trace_.push_back(trace);
    return EngineResult<LapTraceEntry>::success(std::move(trace), EngineStatus::Ok);
}

EngineResult<SessionSnapshot> RaceSession::runUntilStopOrCompletion() {
    if (state_ != SessionState::Running) {
        return EngineResult<SessionSnapshot>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot call runUntilStopOrCompletion() when session is in state ") +
                    toString(state_) + " (requires RUNNING).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "RUNNING",
                toString(state_)
            }});
    }

    while (state_ == SessionState::Running) {
        auto step = advanceLap();
        if (!step.ok()) {
            return EngineResult<SessionSnapshot>::failure(step.status, std::move(step.diagnostics));
        }
        if (step.status == EngineStatus::DecisionRequired ||
            step.status == EngineStatus::RaceCompleted) {
            return EngineResult<SessionSnapshot>::success(snapshot(), step.status);
        }
    }

    return EngineResult<SessionSnapshot>::success(snapshot());
}

EngineResult<SessionSnapshot> RaceSession::pause() {
    if (state_ != SessionState::Running) {
        return EngineResult<SessionSnapshot>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot pause() when session is in state ") + toString(state_) +
                    " (requires RUNNING).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "RUNNING",
                toString(state_)
            }});
    }
    state_ = SessionState::Paused;
    appendEvent(
        SimulationEventType::SessionPaused,
        completed_laps_,
        current_stint_index_,
        "Simulation paused at safe lap boundary after lap " + std::to_string(completed_laps_) + ".");
    return EngineResult<SessionSnapshot>::success(snapshot());
}

EngineResult<SessionSnapshot> RaceSession::resume() {
    if (state_ != SessionState::Paused) {
        return EngineResult<SessionSnapshot>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot resume() when session is in state ") + toString(state_) +
                    " (requires PAUSED; if DECISION_REQUIRED, applyDecision must be called first).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "PAUSED",
                toString(state_)
            }});
    }
    state_ = SessionState::Running;
    appendEvent(
        SimulationEventType::SessionResumed,
        completed_laps_,
        current_stint_index_,
        "Simulation resumed from lap " + std::to_string(completed_laps_) + ".");
    return EngineResult<SessionSnapshot>::success(snapshot());
}

std::vector<StintSpec> RaceSession::currentPlanAsSpecs() const {
    std::vector<StintSpec> specs;
    specs.reserve(stint_records_.size());
    for (const auto& r : stint_records_) {
        StintSpec sp;
        sp.role = r.role;
        sp.target_laps = r.effective_target_laps;
        sp.requested_set_id = r.assigned_set_id;
        specs.push_back(sp);
    }
    return specs;
}

EngineResult<SessionSnapshot> RaceSession::applyDecision(const InteractiveDecision& decision) {
    if (race_mode_ != RaceMode::Interactive || state_ != SessionState::DecisionRequired ||
        !pending_decision_.has_value()) {
        return EngineResult<SessionSnapshot>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot applyDecision() when session is in state ") + toString(state_) +
                    " or mode " + toString(race_mode_) + " (requires INTERACTIVE & DECISION_REQUIRED).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "DECISION_REQUIRED in INTERACTIVE mode",
                toString(state_)
            }});
    }

    auto val_res = StrategyAndReadinessValidator::validateInteractiveDecision(
        decision,
        *pending_decision_,
        *bundle_,
        inventory_,
        stint_records_,
        current_stint_index_,
        completed_laps_);
    if (!val_res.ok()) {
        return EngineResult<SessionSnapshot>::failure(val_res.status, std::move(val_res.diagnostics));
    }

    const std::vector<std::string>& allocated_future_sets = *val_res.value;
    const std::vector<StintSpec> plan_before = currentPlanAsSpecs();

    std::vector<StintSpec> future_specs;
    if (decision.revised_remaining_stints.has_value()) {
        future_specs = *decision.revised_remaining_stints;
    } else {
        for (std::size_t j = static_cast<std::size_t>(current_stint_index_ + 1);
             j < stint_records_.size(); ++j) {
            StintSpec sp;
            sp.role = stint_records_[j].role;
            sp.target_laps = stint_records_[j].effective_target_laps;
            sp.requested_set_id = stint_records_[j].assigned_set_id;
            future_specs.push_back(sp);
        }
    }

    StintRuntimeRecord& active_stint = stint_records_[static_cast<std::size_t>(current_stint_index_)];

    if (decision.action_type == DecisionActionType::ExtendCurrentStint) {
        active_stint.effective_target_laps =
            active_stint.actual_laps_completed + decision.extension_laps;
        active_stint.uncapped_cascaded_target_laps = active_stint.effective_target_laps;
        active_stint.shortfall_laps = 0;

        stint_records_.resize(static_cast<std::size_t>(current_stint_index_ + 1));
        int next_start_lap = completed_laps_ + decision.extension_laps + 1;
        for (std::size_t i = 0; i < future_specs.size(); ++i) {
            const StintSpec& sp = future_specs[i];
            const TyreSetState* set_state = inventory_.findSet(allocated_future_sets[i]);
            StintRuntimeRecord rec;
            rec.stint_index = current_stint_index_ + 1 + static_cast<int>(i);
            rec.role = sp.role;
            rec.physical_compound = *bundle_->role_mapping.compoundForRole(sp.role);
            rec.assigned_set_id = allocated_future_sets[i];
            rec.set_configured_age_at_fit_laps = set_state ? set_state->configured_age_laps : 0;
            rec.set_starting_health_at_fit =
                (set_state && set_state->derived_starting_health.has_value())
                    ? *set_state->derived_starting_health
                    : CornerHealthVector::uniform(1.0);
            rec.start_race_lap = next_start_lap;
            rec.original_target_laps = sp.target_laps;
            rec.uncapped_cascaded_target_laps = sp.target_laps;
            rec.effective_target_laps = sp.target_laps;
            stint_records_.push_back(std::move(rec));
            next_start_lap += sp.target_laps;
        }
    } else {
        stint_records_.resize(static_cast<std::size_t>(current_stint_index_ + 1));
        int next_start_lap = completed_laps_ + 1;
        for (std::size_t i = 0; i < future_specs.size(); ++i) {
            const StintSpec& sp = future_specs[i];
            const TyreSetState* set_state = inventory_.findSet(allocated_future_sets[i]);
            StintRuntimeRecord rec;
            rec.stint_index = current_stint_index_ + 1 + static_cast<int>(i);
            rec.role = sp.role;
            rec.physical_compound = *bundle_->role_mapping.compoundForRole(sp.role);
            rec.assigned_set_id = allocated_future_sets[i];
            rec.set_configured_age_at_fit_laps = set_state ? set_state->configured_age_laps : 0;
            rec.set_starting_health_at_fit =
                (set_state && set_state->derived_starting_health.has_value())
                    ? *set_state->derived_starting_health
                    : CornerHealthVector::uniform(1.0);
            rec.start_race_lap = next_start_lap;
            rec.original_target_laps = sp.target_laps;
            rec.uncapped_cascaded_target_laps = sp.target_laps;
            rec.effective_target_laps = sp.target_laps;
            stint_records_.push_back(std::move(rec));
            next_start_lap += sp.target_laps;
        }

        const PitStopReason reason = pending_decision_->mandatory_stop_required
            ? (pending_coincided_with_planned_boundary_
                   ? PitStopReason::MandatoryAndPlannedBoundary
                   : PitStopReason::MandatoryHealthThreshold)
            : ((decision.action_type == DecisionActionType::KeepOriginalPlan)
                   ? PitStopReason::PlannedStrategyBoundary
                   : PitStopReason::InteractiveUserPitDecision);

        auto pit_res = executePitStopAtCurrentLap(
            reason,
            pending_mandatory_check_,
            pending_coincided_with_planned_boundary_,
            false);
        if (!pit_res.ok()) {
            return EngineResult<SessionSnapshot>::failure(pit_res.status, std::move(pit_res.diagnostics));
        }

        if (!lap_trace_.empty() && lap_trace_.back().race_lap == completed_laps_) {
            lap_trace_.back().pit_stop_occurred_after_lap = true;
            lap_trace_.back().pit_stop_loss_after_lap_s = pit_stops_.back().total_stop_time_loss_s;
            lap_trace_.back().pit_stop_reason = pit_stops_.back().reason;
            lap_trace_.back().cumulative_elapsed_time_s = elapsed_race_time_s_;
        }
    }

    StrategyInterventionRecord inter;
    inter.sequence_id = interventions_.size() + 1;
    inter.completed_race_lap = completed_laps_;
    inter.mode = RaceMode::Interactive;
    inter.intervention_type = toString(decision.action_type);
    inter.stint_index = pending_decision_->current_stint_index;
    inter.extension_laps_requested = decision.extension_laps;
    inter.plan_before = plan_before;
    inter.plan_after = currentPlanAsSpecs();
    inter.summary = decision.user_note.empty()
        ? (std::string("Applied interactive decision ") + toString(decision.action_type) +
           " at lap " + std::to_string(completed_laps_) + ".")
        : decision.user_note;
    interventions_.push_back(std::move(inter));

    appendEvent(
        SimulationEventType::InteractiveDecisionApplied,
        completed_laps_,
        current_stint_index_,
        std::string("Committed interactive decision: ") + toString(decision.action_type) +
            " (session is now PAUSED awaiting explicit resume()).");

    pending_decision_ = std::nullopt;
    pending_mandatory_check_ = MandatoryStopCheckResult{};
    pending_coincided_with_planned_boundary_ = false;
    state_ = SessionState::Paused;

    return EngineResult<SessionSnapshot>::success(snapshot());
}

EngineResult<void> RaceSession::executePitStopAtCurrentLap(
    PitStopReason reason,
    const MandatoryStopCheckResult& mandatory_check,
    bool coincided_with_planned_boundary,
    bool apply_preplanned_cascade) {
    if (current_stint_index_ + 1 >= static_cast<int>(stint_records_.size())) {
        return EngineResult<void>::failure(
            EngineStatus::RuntimeConstraintFailure,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Mandatory pit stop triggered at lap " + std::to_string(completed_laps_) +
                    ", but no further stints exist in the strategy plan.",
                bundle_->event_config.event_id,
                "",
                "/strategy/stints",
                "Remaining stint to fit fresh tyre set",
                "final stint ended early before lap L"
            }});
    }

    const int stop_seq = static_cast<int>(pit_stops_.size()) + 1;
    auto pit_eval = LapStintEvaluator::evaluatePitStopLoss(
        stop_seq, completed_laps_, *bundle_, model_params_);
    if (!pit_eval.ok()) {
        return EngineResult<void>::failure(pit_eval.status, std::move(pit_eval.diagnostics));
    }

    StintRuntimeRecord& outgoing = stint_records_[static_cast<std::size_t>(current_stint_index_)];
    StintRuntimeRecord& incoming = stint_records_[static_cast<std::size_t>(current_stint_index_ + 1)];

    outgoing.completed = true;
    const int shortfall =
        std::max(0, outgoing.effective_target_laps - outgoing.actual_laps_completed);
    outgoing.shortfall_laps = shortfall;
    outgoing.ended_by_mandatory_stop =
        (reason == PitStopReason::MandatoryHealthThreshold ||
         reason == PitStopReason::MandatoryAndPlannedBoundary);

    const std::vector<StintSpec> plan_before = currentPlanAsSpecs();

    if (apply_preplanned_cascade && shortfall > 0) {
        accumulated_shortfall_laps_ += shortfall;
        incoming.accumulated_shortfall_into_stint = accumulated_shortfall_laps_;
        incoming.uncapped_cascaded_target_laps =
            incoming.original_target_laps + accumulated_shortfall_laps_;

        const int remaining_race_laps = bundle_->event_config.race_distance_laps - completed_laps_;
        incoming.effective_target_laps =
            std::min(incoming.uncapped_cascaded_target_laps, remaining_race_laps);

        StrategyInterventionRecord cascade_rec;
        cascade_rec.sequence_id = interventions_.size() + 1;
        cascade_rec.completed_race_lap = completed_laps_;
        cascade_rec.mode = RaceMode::Preplanned;
        cascade_rec.intervention_type = "PREPLANNED_SHORTFALL_CASCADE";
        cascade_rec.stint_index = current_stint_index_;
        cascade_rec.extension_laps_requested = 0;
        cascade_rec.plan_before = plan_before;
        cascade_rec.plan_after = currentPlanAsSpecs();
        cascade_rec.summary =
            "Stint " + std::to_string(current_stint_index_ + 1) +
            " ended early at lap " + std::to_string(completed_laps_) +
            " with shortfall " + std::to_string(shortfall) +
            " laps (accumulated shortfall: " + std::to_string(accumulated_shortfall_laps_) +
            "); next stint target updated from " + std::to_string(incoming.original_target_laps) +
            " to " + std::to_string(incoming.effective_target_laps) + " (uncapped: " +
            std::to_string(incoming.uncapped_cascaded_target_laps) + ").";
        interventions_.push_back(std::move(cascade_rec));

        appendEvent(
            SimulationEventType::PreplannedShortfallCascaded,
            completed_laps_,
            current_stint_index_ + 1,
            interventions_.back().summary);
    } else if (apply_preplanned_cascade) {
        const int remaining_race_laps = bundle_->event_config.race_distance_laps - completed_laps_;
        const bool is_final_stint =
            (current_stint_index_ + 1 == static_cast<int>(stint_records_.size()) - 1);
        if (is_final_stint && incoming.effective_target_laps > remaining_race_laps) {
            incoming.effective_target_laps = remaining_race_laps;
        }
    }

    auto fit_res = inventory_.fitSetForStint(incoming.assigned_set_id, current_stint_index_ + 1);
    if (!fit_res.ok()) {
        return fit_res;
    }

    const TyreSetState* fitted_set = inventory_.findSet(incoming.assigned_set_id);
    incoming.started = true;
    incoming.start_race_lap = completed_laps_ + 1;
    if (fitted_set != nullptr && fitted_set->derived_starting_health.has_value()) {
        incoming.set_configured_age_at_fit_laps = fitted_set->configured_age_laps;
        incoming.set_starting_health_at_fit = *fitted_set->derived_starting_health;
        incoming.final_health = *fitted_set->derived_starting_health;
    }

    PitStopRecord stop = *pit_eval.value;
    stop.reason = reason;
    stop.aggregate_threshold_breached = mandatory_check.aggregate_breached;
    stop.individual_threshold_breached = mandatory_check.individual_breached;
    stop.coincided_with_planned_boundary = coincided_with_planned_boundary;
    stop.outgoing_stint_index = current_stint_index_;
    stop.incoming_stint_index = current_stint_index_ + 1;
    stop.set_removed_id = outgoing.assigned_set_id;
    stop.role_removed = outgoing.role;
    stop.compound_removed = outgoing.physical_compound;
    stop.removed_set_final_health = outgoing.final_health;
    stop.set_fitted_id = incoming.assigned_set_id;
    stop.role_fitted = incoming.role;
    stop.compound_fitted = incoming.physical_compound;
    stop.fitted_set_configured_age_laps = incoming.set_configured_age_at_fit_laps;
    stop.fitted_set_starting_health = incoming.set_starting_health_at_fit;
    stop.shortened_planned_stint = (shortfall > 0);
    stop.outgoing_stint_target_laps = outgoing.effective_target_laps;
    stop.outgoing_stint_actual_laps = outgoing.actual_laps_completed;
    stop.stint_shortfall_laps = shortfall;
    stop.accumulated_shortfall_laps_after_stop = accumulated_shortfall_laps_;
    stop.next_stint_original_target_laps = incoming.original_target_laps;
    stop.next_stint_uncapped_cascaded_target_laps = incoming.uncapped_cascaded_target_laps;
    stop.next_stint_effective_target_laps = incoming.effective_target_laps;

    elapsed_race_time_s_ += stop.total_stop_time_loss_s;
    total_pit_stop_loss_s_ += stop.total_stop_time_loss_s;
    pit_stops_.push_back(stop);

    current_stint_index_ += 1;
    active_tyre_set_id_ = incoming.assigned_set_id;

    appendEvent(
        SimulationEventType::PitStopExecuted,
        completed_laps_,
        current_stint_index_,
        "Executed pit stop #" + std::to_string(stop.stop_sequence_number) +
            " at end of lap " + std::to_string(completed_laps_) + " (" +
            toString(stop.reason) + "): removed " + stop.set_removed_id +
            ", fitted " + stop.set_fitted_id + " (loss: " +
            std::to_string(stop.total_stop_time_loss_s) + " s).");

    return EngineResult<void>::success();
}

void RaceSession::finalizeTerminalState(
    SessionState terminal_state,
    const std::string& summary_message) {
    state_ = terminal_state;
    pending_decision_ = std::nullopt;

    SimulationEventType ev_type = SimulationEventType::SessionCompleted;
    if (terminal_state == SessionState::Failed) {
        ev_type = SimulationEventType::SessionFailed;
    } else if (terminal_state == SessionState::Cancelled) {
        ev_type = SimulationEventType::SessionCancelled;
    }
    appendEvent(ev_type, completed_laps_, current_stint_index_, summary_message);

    inventory_.resetRuntimeStateToConfigured();
    appendEvent(
        SimulationEventType::RuntimeStateReset,
        completed_laps_,
        current_stint_index_,
        "Runtime tyre inventory state reset to pre-race configured condition; configured ages preserved.");

    RaceResult res;
    res.terminal_state = terminal_state;
    res.completed_full_race_distance =
        (terminal_state == SessionState::Completed &&
         bundle_.has_value() &&
         completed_laps_ == bundle_->event_config.race_distance_laps);
    res.race_mode = race_mode_;
    res.model_stage = model_stage_;
    res.fidelity_status = fidelity_status_;
    res.simulator_math_version = "1.0.0";

    if (bundle_.has_value()) {
        res.event_id = bundle_->event_config.event_id;
        res.official_event_name = bundle_->event_config.official_event_name;
        res.track_id = bundle_->track_layout.track_id;
        res.layout_version = bundle_->track_layout.layout_version;
        res.surface_version = bundle_->track_surface.surface_version;
        res.season_year = bundle_->event_config.season_year;
        res.compound_catalogue_spec_version = bundle_->compound_catalogue.spec_version;
        res.configured_race_distance_laps = bundle_->event_config.race_distance_laps;
    }

    res.vehicle_config = vehicle_config_;
    res.environment_config = env_config_;
    res.model_parameters = model_params_;
    res.initial_strategy = strategy_plan_;

    res.completed_race_laps = completed_laps_;
    res.total_race_time_s = elapsed_race_time_s_;
    res.total_flying_lap_time_s = total_flying_lap_time_s_;
    res.total_pit_stop_time_loss_s = total_pit_stop_loss_s_;
    res.total_pit_stops = static_cast<int>(pit_stops_.size());

    int planned_count = 0;
    int mandatory_count = 0;
    for (const auto& p : pit_stops_) {
        if (p.reason == PitStopReason::MandatoryHealthThreshold ||
            p.reason == PitStopReason::MandatoryAndPlannedBoundary) {
            ++mandatory_count;
        } else {
            ++planned_count;
        }
    }
    res.planned_pit_stops = planned_count;
    res.mandatory_health_stops = mandatory_count;

    res.initial_fuel_kg = vehicle_config_.initial_fuel_load_kg;
    res.fuel_consumed_kg = total_fuel_consumed_kg_;
    res.final_fuel_remaining_kg = current_fuel_remaining_kg_;

    res.stints = stint_records_;
    res.lap_trace = lap_trace_;
    res.pit_stops = pit_stops_;
    res.interventions = interventions_;
    res.event_log = event_log_;
    res.diagnostics = session_diagnostics_;

    if (model_stage_ != ModelStage::M3_NonlinearCliffAndFourCorner) {
        res.compatibility_and_fidelity_note =
            std::string("Baseline Stage ") + toString(model_stage_) +
            " uses a shared aggregate tyre-health scalar across all four corners (FL, FR, RL, RR equal); "
            "fidelity status: " + toString(fidelity_status_) + ".";
    } else {
        res.compatibility_and_fidelity_note =
            std::string("Extension Stage M3 active (asymmetric four-corner wear and nonlinear cliff); "
                        "fidelity status: ") + toString(fidelity_status_) + ".";
    }

    last_result_ = std::move(res);
}

EngineResult<RaceResult> RaceSession::cancel() {
    if (!isActiveRunState(state_)) {
        return EngineResult<RaceResult>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Cannot cancel() when session is in state ") + toString(state_) +
                    " (requires RUNNING, PAUSED, or DECISION_REQUIRED).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "RUNNING|PAUSED|DECISION_REQUIRED",
                toString(state_)
            }});
    }

    finalizeTerminalState(
        SessionState::Cancelled,
        "Simulation cancelled at lap " + std::to_string(completed_laps_) + ".");
    return EngineResult<RaceResult>::success(*last_result_);
}

EngineResult<void> RaceSession::resetForNewRun() {
    if (isActiveRunState(state_)) {
        return EngineResult<void>::failure(
            EngineStatus::InvalidStateTransition,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Cannot call resetForNewRun() while a simulation is active (call cancel() first).",
                bundle_ ? bundle_->event_config.event_id : "session",
                "",
                "/session/state",
                "Terminal or Configured state",
                toString(state_)
            }});
    }
    if (!bundle_.has_value()) {
        state_ = SessionState::Unconfigured;
        return EngineResult<void>::success();
    }

    inventory_.recomputeDerivedStartingHealth(
        eventBundle(), vehicle_config_, model_params_, model_stage_);
    completed_laps_ = 0;
    current_stint_index_ = 0;
    active_tyre_set_id_.clear();
    accumulated_shortfall_laps_ = 0;
    elapsed_race_time_s_ = 0.0;
    total_flying_lap_time_s_ = 0.0;
    total_pit_stop_loss_s_ = 0.0;
    current_fuel_remaining_kg_ = vehicle_config_.initial_fuel_load_kg;
    total_fuel_consumed_kg_ = 0.0;
    pending_decision_ = std::nullopt;
    state_ = SessionState::Configured;
    return EngineResult<void>::success();
}

SessionSnapshot RaceSession::snapshot() const {
    SessionSnapshot snap;
    snap.session_state = state_;
    snap.race_mode = race_mode_;
    snap.model_stage = model_stage_;
    snap.fidelity_status = fidelity_status_;

    if (bundle_.has_value()) {
        snap.event_id = bundle_->event_config.event_id;
        snap.track_id = bundle_->track_layout.track_id;
        snap.layout_version = bundle_->track_layout.layout_version;
        snap.surface_version = bundle_->track_surface.surface_version;
        snap.season_year = bundle_->event_config.season_year;
        snap.total_race_laps = bundle_->event_config.race_distance_laps;
    }

    snap.vehicle_config = vehicle_config_;
    snap.environment_config = env_config_;
    snap.model_parameters = model_params_;
    snap.initial_strategy_plan = strategy_plan_;

    snap.completed_laps = completed_laps_;
    snap.current_stint_index = current_stint_index_;
    snap.active_tyre_set_id = active_tyre_set_id_;
    snap.accumulated_shortfall_laps = accumulated_shortfall_laps_;
    snap.elapsed_race_time_s = elapsed_race_time_s_;
    snap.total_flying_lap_time_s = total_flying_lap_time_s_;
    snap.total_pit_stop_loss_s = total_pit_stop_loss_s_;
    snap.current_fuel_remaining_kg = current_fuel_remaining_kg_;
    snap.total_fuel_consumed_kg = total_fuel_consumed_kg_;

    snap.tyre_inventory = inventory_.sets();
    snap.stint_records = stint_records_;
    snap.pending_decision = pending_decision_;
    snap.pending_mandatory_check = pending_mandatory_check_;
    snap.pending_coincided_with_planned_boundary = pending_coincided_with_planned_boundary_;
    snap.lap_trace = lap_trace_;
    snap.pit_stops = pit_stops_;
    snap.interventions = interventions_;
    snap.event_log = event_log_;
    snap.diagnostics = session_diagnostics_;
    return snap;
}

EngineResult<void> RaceSession::restoreSnapshot(const SessionSnapshot& snap) {
    if (!bundle_.has_value() || bundle_->event_config.event_id != snap.event_id) {
        if (!dal_) {
            return EngineResult<void>::failure(
                EngineStatus::ValidationFailed,
                {Diagnostic{
                    Severity::Error,
                    DiagnosticCode::CrossReferenceFailure,
                    "Cannot restore snapshot: matching EventBundle is not loaded.",
                    snap.event_id,
                    "",
                    "/snapshot/event_id",
                    "Loaded EventBundle",
                    "missing"
                }});
        }
        auto load_res = dal_->loadEventBundle(snap.event_id);
        if (!load_res.ok()) {
            return EngineResult<void>::failure(
                EngineStatus::ValidationFailed, std::move(load_res.diagnostics));
        }
        bundle_ = *load_res.value;
        last_bundle_load_status_ = LoadStatus::Success;
    }

    state_ = snap.session_state;
    race_mode_ = snap.race_mode;
    model_stage_ = snap.model_stage;
    fidelity_status_ = snap.fidelity_status;

    vehicle_config_ = snap.vehicle_config;
    env_config_ = snap.environment_config;
    model_params_ = snap.model_parameters;
    strategy_plan_ = snap.initial_strategy_plan;

    completed_laps_ = snap.completed_laps;
    current_stint_index_ = snap.current_stint_index;
    active_tyre_set_id_ = snap.active_tyre_set_id;
    accumulated_shortfall_laps_ = snap.accumulated_shortfall_laps;
    elapsed_race_time_s_ = snap.elapsed_race_time_s;
    total_flying_lap_time_s_ = snap.total_flying_lap_time_s;
    total_pit_stop_loss_s_ = snap.total_pit_stop_loss_s;
    current_fuel_remaining_kg_ = snap.current_fuel_remaining_kg;
    total_fuel_consumed_kg_ = snap.total_fuel_consumed_kg;

    inventory_.restoreSetsFromSnapshot(snap.tyre_inventory);
    stint_records_ = snap.stint_records;
    pending_decision_ = snap.pending_decision;
    pending_mandatory_check_ = snap.pending_mandatory_check;
    pending_coincided_with_planned_boundary_ = snap.pending_coincided_with_planned_boundary;
    lap_trace_ = snap.lap_trace;
    pit_stops_ = snap.pit_stops;
    interventions_ = snap.interventions;
    event_log_ = snap.event_log;
    session_diagnostics_ = snap.diagnostics;

    return EngineResult<void>::success();
}

} // namespace simulation
} // namespace f1_sim
