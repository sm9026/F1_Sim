#ifndef F1_SIM_SIMULATION_ENGINE_TYPES_HPP
#define F1_SIM_SIMULATION_ENGINE_TYPES_HPP

#include "f1_sim/data_access/diagnostic.hpp"
#include "f1_sim/data_access/domain_models.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace f1_sim {
namespace simulation {

using data_access::ConfidenceTier;
using data_access::Diagnostic;
using data_access::DiagnosticCode;
using data_access::EventBundle;
using data_access::LoadStatus;
using data_access::MissingPolicy;
using data_access::PhysicalCompoundId;
using data_access::PhysicalCompoundSpec;
using data_access::ProvenanceMetadata;
using data_access::Result;
using data_access::Severity;
using data_access::SourceStatus;
using data_access::TyreRole;

/**
 * @brief Mathematical framework stage selector (Section 1.3 & Section 13.1).
 *
 * Baseline stages M0-M2 bind the four tyre corners (FL, FR, RL, RR) to a shared
 * aggregate tyre-health scalar while preserving the 4-corner operational interface.
 * Stage M3 enables nonlinear wear cliffs (Eq. 7.6) and geometry-driven asymmetric
 * four-corner wear (Eq. 7.4).
 */
enum class ModelStage {
    M0_DeterministicBaseline = 0,
    M1_TrackAwareCalibratedDry = 1,
    M2_DynamicEnvironmentDry = 2,
    M3_NonlinearCliffAndFourCorner = 3
};

inline const char* toString(ModelStage stage) noexcept {
    switch (stage) {
        case ModelStage::M0_DeterministicBaseline:       return "M0_DETERMINISTIC_BASELINE";
        case ModelStage::M1_TrackAwareCalibratedDry:     return "M1_TRACK_AWARE_CALIBRATED_DRY";
        case ModelStage::M2_DynamicEnvironmentDry:       return "M2_DYNAMIC_ENVIRONMENT_DRY";
        case ModelStage::M3_NonlinearCliffAndFourCorner: return "M3_NONLINEAR_CLIFF_AND_FOUR_CORNER";
    }
    return "UNKNOWN_MODEL_STAGE";
}

/**
 * @brief Environmental multiplier functional form (Framework Section 8.1, Eq. 8.1a vs 8.1b).
 */
enum class EnvironmentModelForm {
    LinearisedFirstOrder, // Eq. (8.1b): 1.0 + kappa_trk * dT_trk + kappa_air * dT_air + psi_hum * dPhi
    ExponentialLogLinear  // Eq. (8.1a): exp(kappa_trk * dT_trk + kappa_air * dT_air + psi_hum * dPhi)
};

/**
 * @brief Operating mode of the race simulation engine (Design Spec Section 10).
 */
enum class RaceMode {
    Preplanned,
    Interactive
};

inline const char* toString(RaceMode mode) noexcept {
    switch (mode) {
        case RaceMode::Preplanned:  return "PREPLANNED";
        case RaceMode::Interactive: return "INTERACTIVE";
    }
    return "UNKNOWN_RACE_MODE";
}

/**
 * @brief Explicit session lifecycle state (Design Spec Section 10.3).
 */
enum class SessionState {
    Unconfigured = 0,
    Configured,
    Validated,
    Running,
    Paused,
    DecisionRequired,
    Completed,
    Failed,
    Cancelled
};

inline const char* toString(SessionState state) noexcept {
    switch (state) {
        case SessionState::Unconfigured:     return "UNCONFIGURED";
        case SessionState::Configured:       return "CONFIGURED";
        case SessionState::Validated:        return "VALIDATED";
        case SessionState::Running:          return "RUNNING";
        case SessionState::Paused:           return "PAUSED";
        case SessionState::DecisionRequired: return "DECISION_REQUIRED";
        case SessionState::Completed:        return "COMPLETED";
        case SessionState::Failed:           return "FAILED";
        case SessionState::Cancelled:        return "CANCELLED";
    }
    return "UNKNOWN_SESSION_STATE";
}

inline bool isTerminalState(SessionState state) noexcept {
    return state == SessionState::Completed ||
           state == SessionState::Failed ||
           state == SessionState::Cancelled;
}

inline bool isActiveRunState(SessionState state) noexcept {
    return state == SessionState::Running ||
           state == SessionState::Paused ||
           state == SessionState::DecisionRequired;
}

/**
 * @brief Calibration and fidelity classification of a simulation configuration or result.
 */
enum class FidelityStatus {
    Calibrated,
    EstimatedOrAssumedLowerFidelity,
    UncalibratedIncomplete,
    InvalidOrBlocked
};

inline const char* toString(FidelityStatus status) noexcept {
    switch (status) {
        case FidelityStatus::Calibrated:
            return "CALIBRATED";
        case FidelityStatus::EstimatedOrAssumedLowerFidelity:
            return "ESTIMATED_OR_ASSUMED_LOWER_FIDELITY";
        case FidelityStatus::UncalibratedIncomplete:
            return "UNCALIBRATED_INCOMPLETE";
        case FidelityStatus::InvalidOrBlocked:
            return "INVALID_OR_BLOCKED";
    }
    return "UNKNOWN_FIDELITY_STATUS";
}

/**
 * @brief High-level execution permission determined by capability & readiness gating.
 */
enum class ExecutionPermission {
    AllowedCalibrated,
    AllowedLowerFidelity,
    BlockedInvalidBundle,
    BlockedInvalidInput,
    BlockedMissingCalibration,
    BlockedUnsupportedWetConditions
};

inline const char* toString(ExecutionPermission perm) noexcept {
    switch (perm) {
        case ExecutionPermission::AllowedCalibrated:
            return "ALLOWED_CALIBRATED";
        case ExecutionPermission::AllowedLowerFidelity:
            return "ALLOWED_LOWER_FIDELITY";
        case ExecutionPermission::BlockedInvalidBundle:
            return "BLOCKED_INVALID_BUNDLE";
        case ExecutionPermission::BlockedInvalidInput:
            return "BLOCKED_INVALID_INPUT";
        case ExecutionPermission::BlockedMissingCalibration:
            return "BLOCKED_MISSING_CALIBRATION";
        case ExecutionPermission::BlockedUnsupportedWetConditions:
            return "BLOCKED_UNSUPPORTED_WET_CONDITIONS";
    }
    return "UNKNOWN_EXECUTION_PERMISSION";
}

/**
 * @brief Machine-readable engine operation status codes.
 */
enum class EngineStatus {
    Ok = 0,
    DecisionRequired,
    RaceCompleted,
    InvalidStateTransition,
    ConfigurationLockedWhileRunning,
    ValidationFailed,
    InsufficientCapabilityOrCalibration,
    UnsupportedWetConditions,
    MandatoryStopCannotBeOverridden,
    RuntimeConstraintFailure
};

inline const char* toString(EngineStatus status) noexcept {
    switch (status) {
        case EngineStatus::Ok:
            return "OK";
        case EngineStatus::DecisionRequired:
            return "DECISION_REQUIRED";
        case EngineStatus::RaceCompleted:
            return "RACE_COMPLETED";
        case EngineStatus::InvalidStateTransition:
            return "ERR_INVALID_STATE_TRANSITION";
        case EngineStatus::ConfigurationLockedWhileRunning:
            return "ERR_CONFIGURATION_LOCKED_WHILE_RUNNING";
        case EngineStatus::ValidationFailed:
            return "ERR_VALIDATION_FAILED";
        case EngineStatus::InsufficientCapabilityOrCalibration:
            return "ERR_INSUFFICIENT_CAPABILITY_OR_CALIBRATION";
        case EngineStatus::UnsupportedWetConditions:
            return "ERR_UNSUPPORTED_WET_CONDITIONS";
        case EngineStatus::MandatoryStopCannotBeOverridden:
            return "ERR_MANDATORY_STOP_CANNOT_BE_OVERRIDDEN";
        case EngineStatus::RuntimeConstraintFailure:
            return "ERR_RUNTIME_CONSTRAINT_FAILURE";
    }
    return "ERR_UNKNOWN_ENGINE_STATUS";
}

/**
 * @brief Generic result wrapper for Simulation Engine operations.
 */
template <typename T>
struct EngineResult {
    EngineStatus status = EngineStatus::Ok;
    std::optional<T> value = std::nullopt;
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept {
        return (status == EngineStatus::Ok ||
                status == EngineStatus::DecisionRequired ||
                status == EngineStatus::RaceCompleted) &&
               value.has_value();
    }

    static EngineResult<T> success(
        T val,
        EngineStatus st = EngineStatus::Ok,
        std::vector<Diagnostic> diags = {}) {
        EngineResult<T> res;
        res.status = st;
        res.value = std::move(val);
        res.diagnostics = std::move(diags);
        return res;
    }

    static EngineResult<T> failure(
        EngineStatus err_status,
        std::vector<Diagnostic> diags) {
        EngineResult<T> res;
        res.status = err_status;
        res.value = std::nullopt;
        res.diagnostics = std::move(diags);
        return res;
    }
};

template <>
struct EngineResult<void> {
    EngineStatus status = EngineStatus::Ok;
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept {
        return status == EngineStatus::Ok ||
               status == EngineStatus::DecisionRequired ||
               status == EngineStatus::RaceCompleted;
    }

    static EngineResult<void> success(
        EngineStatus st = EngineStatus::Ok,
        std::vector<Diagnostic> diags = {}) {
        EngineResult<void> res;
        res.status = st;
        res.diagnostics = std::move(diags);
        return res;
    }

    static EngineResult<void> failure(
        EngineStatus err_status,
        std::vector<Diagnostic> diags) {
        EngineResult<void> res;
        res.status = err_status;
        res.diagnostics = std::move(diags);
        return res;
    }
};

/**
 * @brief Individual tyre corner indices i in {FL, FR, RL, RR} (Framework Section 2).
 */
enum class TyreCorner : std::size_t {
    FL = 0,
    FR = 1,
    RL = 2,
    RR = 3
};

/**
 * @brief Four-corner tyre health state vector in [0, 1]^4, where 1.0 = 100% new health
 *        and 0.0 = 0% depleted terminal limit.
 *
 * In baseline stages M0-M2, all four corners evolve identically (shared scalar).
 * In stage M3, corners may evolve asymmetrically via circuit load multipliers lambda_{r,i}.
 */
struct CornerHealthVector {
    std::array<double, 4> corners{1.0, 1.0, 1.0, 1.0};

    static CornerHealthVector uniform(double h) noexcept {
        return CornerHealthVector{{h, h, h, h}};
    }

    double operator[](std::size_t idx) const noexcept { return corners[idx]; }
    double& operator[](std::size_t idx) noexcept { return corners[idx]; }

    double meanHealth() const noexcept {
        return 0.25 * (corners[0] + corners[1] + corners[2] + corners[3]);
    }

    double minCornerHealth() const noexcept {
        double m = corners[0];
        for (std::size_t i = 1; i < 4; ++i) {
            if (corners[i] < m) m = corners[i];
        }
        return m;
    }

    double maxCornerWear() const noexcept {
        return 1.0 - minCornerHealth();
    }

    double meanWear() const noexcept {
        return 1.0 - meanHealth();
    }

    std::array<double, 4> percentageValues() const noexcept {
        return {corners[0] * 100.0, corners[1] * 100.0, corners[2] * 100.0, corners[3] * 100.0};
    }

    double meanHealthPct() const noexcept { return meanHealth() * 100.0; }
    double minCornerHealthPct() const noexcept { return minCornerHealth() * 100.0; }
};

/**
 * @brief Mandatory health threshold constants and evaluation result (Design Spec Section 9).
 *
 * Strict inequalities:
 *  - Aggregate threshold T_A = 0.30 (30%): triggered iff meanHealth() < 0.30.
 *  - Individual threshold T_I = 0.12 (12%): triggered iff minCornerHealth() < 0.12.
 *  - Exact equality (0.30 or 0.12) does NOT trigger a mandatory stop.
 */
struct MandatoryStopThresholds {
    static constexpr double kAggregateHealthThreshold = 0.30;  // 30%
    static constexpr double kIndividualHealthThreshold = 0.12; // 12%
};

struct MandatoryStopCheckResult {
    bool aggregate_breached = false;
    bool individual_breached = false;
    bool mandatory_stop_required = false;
    double observed_mean_health = 1.0;
    double observed_min_corner_health = 1.0;
    std::size_t limiting_corner_index = 0;
};

/**
 * @brief Derived starting health readiness status for an inventory tyre set.
 */
enum class TyreSetHealthStatus {
    ReadyNewSet,
    ReadyDerivedFromAge,
    MissingCalibrationForAgeDerivation,
    DepletedBelowMandatoryThreshold
};

inline const char* toString(TyreSetHealthStatus status) noexcept {
    switch (status) {
        case TyreSetHealthStatus::ReadyNewSet:
            return "READY_NEW_SET";
        case TyreSetHealthStatus::ReadyDerivedFromAge:
            return "READY_DERIVED_FROM_AGE";
        case TyreSetHealthStatus::MissingCalibrationForAgeDerivation:
            return "MISSING_CALIBRATION_FOR_AGE_DERIVATION";
        case TyreSetHealthStatus::DepletedBelowMandatoryThreshold:
            return "DEPLETED_BELOW_MANDATORY_THRESHOLD";
    }
    return "UNKNOWN_TYRE_SET_HEALTH_STATUS";
}

/**
 * @brief Single physical tyre set in the fixed 7-set session inventory (3S, 2M, 2H).
 *
 * Separates persistent pre-race configuration (`configured_age_laps`, `derived_starting_health`)
 * from temporary single-run state (`runtime_age_laps`, `runtime_health`, `consumed_in_current_run`,
 * `currently_fitted`, `assigned_stint_index`).
 */
struct TyreSetState {
    std::string set_id; // "S1", "S2", "S3", "M1", "M2", "H1", "H2"
    TyreRole role = TyreRole::Soft;
    std::optional<PhysicalCompoundId> mapped_compound_id = std::nullopt;

    // Persistent configuration state (editable only when no simulation is running)
    int configured_age_laps = 0;
    std::optional<CornerHealthVector> derived_starting_health = CornerHealthVector::uniform(1.0);
    TyreSetHealthStatus health_status = TyreSetHealthStatus::ReadyNewSet;

    // Temporary runtime state (reset automatically when a simulation ends/cancels/fails)
    int runtime_age_laps = 0;
    CornerHealthVector runtime_health = CornerHealthVector::uniform(1.0);
    bool currently_fitted = false;
    bool consumed_in_current_run = false;
    std::optional<int> assigned_stint_index = std::nullopt;
};

/**
 * @brief Per-lap environmental state w_k (Framework Schema 3, Section 4.5 & Section 8).
 */
struct LapEnvironment {
    int lap_index = 1;
    std::string measurement_semantics = "MANUAL_SCENARIO";
    SourceStatus source_status = SourceStatus::Assumed;

    std::optional<double> track_temperature_c = 35.0; // If nullopt, inferred via Eq. (8.2)
    double air_temperature_c = 25.0;
    double relative_humidity_pct = 50.0;
    double air_pressure_hpa = 1013.25;
    double wind_speed_ms = 0.0;
    std::optional<double> wind_direction_deg = std::nullopt;
    double solar_radiation_wm2 = 500.0;
    double cloud_cover_pct = 50.0;
    double precipitation_rate_mmph = 0.0;
    double surface_wetness = 0.0;
};

/**
 * @brief Environmental scenario configuration (constant baseline or lap-indexed series).
 */
struct EnvironmentScenarioConfig {
    std::string scenario_id = "default_dry_reference";
    LapEnvironment constant_environment{};
    std::vector<LapEnvironment> lap_series{}; // If non-empty, supplies lap-varying environment
    EnvironmentModelForm model_form = EnvironmentModelForm::LinearisedFirstOrder;

    // Radiative balance parameters for Eq. (8.2) when track_temperature_c is nullopt
    double asphalt_albedo = 0.10;
    double radiative_conductance_wm2c = 18.0;
    double wind_convection_coeff_wsm3c = 3.5;
};

/**
 * @brief Vehicle and driver mass/fuel configuration (Framework Schema 6, Section 4.7 & Spec Section 6.3).
 *
 * Driver mass defaults to 75.0 kg and is configurable. Baseline FIA dry mass (798.0 kg)
 * includes the reference 75.0 kg driver mass. Any driver mass deviation (driver_mass_kg - 75.0)
 * enters total vehicle mass through the framework's mass-sensitivity pathways.
 */
struct VehicleConfig {
    std::string vehicle_id = "f1_2026_generic_car";
    SourceStatus source_status = SourceStatus::Assumed;

    double base_dry_mass_with_ref_driver_kg = 798.0; // M_dry at 75.0 kg reference driver mass
    double driver_mass_kg = 75.0;                    // Configurable driver mass (default 75.0 kg)
    static constexpr double kReferenceDriverMassKg = 75.0;
    static constexpr double kReferenceFuelMassKg = 50.0; // M_ref = 50.0 kg (Framework Section 6.1)

    double initial_fuel_load_kg = 100.0;             // M_fuel,0
    std::optional<double> fuel_burn_rate_kg_per_lap = std::nullopt; // If nullopt, derived as M_fuel,0 / L
    double front_weight_dist_frac = 0.46;
    double tyre_energy_load_mult = 1.00;             // zeta_v
    double load_wear_sensitivity_kappa = 0.80;       // kappa_load in Eq. (7.4)

    double effectiveDryMassKg() const noexcept {
        return base_dry_mass_with_ref_driver_kg + (driver_mass_kg - kReferenceDriverMassKg);
    }

    double driverMassDeltaKg() const noexcept {
        return driver_mass_kg - kReferenceDriverMassKg;
    }
};

/**
 * @brief Explicit compound parameter override or calibration supplement for a physical compound C1..C5.
 *
 * Used when the repository's PhysicalCompoundCatalogue has null/uncalibrated fields or when
 * running controlled mathematical conformance tests / scenario experiments.
 */
struct CompoundParameterOverride {
    PhysicalCompoundId compound_id = PhysicalCompoundId::C3;
    SourceStatus source_status = SourceStatus::Estimated;

    std::optional<double> pace_offset_ref_s;
    std::optional<double> base_wear_rate_per_lap;
    std::optional<double> linear_deg_coeff_s;
    std::optional<double> cliff_wear_threshold;
    std::optional<double> cliff_amplitude_s;
    std::optional<double> cliff_exponent;
    std::optional<double> thermal_deg_sens_per_c;
    std::optional<double> ambient_air_deg_sens_per_c;
};

/**
 * @brief Circuit and vehicle model parameters (Framework Schema 7: CalibratedModelParameters).
 *
 * Distinguishes required parameters without framework defaults (`ref_lap_time_s` and
 * pit-stop loss) from parameters with framework-authorised defaults (`fuel_lap_sens_s_per_kg`,
 * `lap1_standing_start_loss_s`, `outlap_warmup_loss_s`).
 */
struct ModelParameters {
    std::string param_set_id = "default_uncalibrated_shell";
    SourceStatus parameter_source_status = SourceStatus::UnknownUnavailable;

    // Required pace anchor (MissingPolicy::RejectSimulation in Framework Section 4.7)
    std::optional<double> ref_lap_time_s = std::nullopt; // T_ref(r, v)

    // Pit-stop loss parameters (Eq. 9.1): provide either pit_loss_total_s (P_ref)
    // OR the component parameters (main_straight_bypass_time_s + stationary_service_time_s + pit_accel_decel_loss_s).
    std::optional<double> pit_loss_total_s = std::nullopt; // P_ref(r)
    double reference_stationary_time_s = 2.40;             // tau_stat,ref
    double configured_stationary_time_s = 2.40;            // tau_stat,j
    std::optional<double> main_straight_bypass_time_s = std::nullopt; // T_main-bypass(r, v)
    std::optional<double> pit_accel_decel_loss_s = std::nullopt;      // dT_pit-accel(r, v)

    // Framework-authorised defaultable parameters (Framework Section 4.7)
    double fuel_lap_sens_s_per_kg = 0.032;       // gamma_fuel (default 0.032 s/kg)
    bool scale_fuel_sens_by_lap_length = false;  // Optional L_lap / 5000m scaling
    double lap1_standing_start_loss_s = 3.50;    // dT_start (default 3.5 s)
    double outlap_warmup_loss_s = 0.50;          // dT_out (default 0.5 s)
    double grip_scaling_psi = 1.00;              // psi_grip(r) (default 1.0)
    double abrasiveness_sensitivity_omega = 0.20;// omega_abr in Eq. (7.4) (0.0 in M0, 0.20 in M1+)
    double limiting_corner_weight_xi = 0.50;     // xi_lim in Eq. (7.5)
    double humidity_wear_sens_psi = 0.00;        // psi_hum_c === 0.0 (Framework Section 8.2)
    double structural_health_floor_h_min = 0.05; // h_min in Section 9.4 (5%)

    // Optional compound-specific parameter supplements/overrides keyed by PhysicalCompoundId
    std::map<PhysicalCompoundId, CompoundParameterOverride> compound_overrides;

    // Policy toggle: must be true to permit running with Estimated/Assumed parameters
    // when empirical Calibrated parameters are absent.
    bool allow_estimated_or_assumed_parameters = true;
};

/**
 * @brief User-provided stint specification in a strategy plan.
 */
struct StintSpec {
    TyreRole role = TyreRole::Soft;
    int target_laps = 0;
    std::optional<std::string> requested_set_id = std::nullopt; // Optional explicit set ID ("S1", etc.)
};

/**
 * @brief Runtime and result tracking for a single stint (separating original target,
 *        revised/cascaded target, actual completed laps, and shortfall).
 */
struct StintRuntimeRecord {
    int stint_index = 0; // 0-based index (0 .. J-1)
    TyreRole role = TyreRole::Soft;
    PhysicalCompoundId physical_compound = PhysicalCompoundId::C3;
    std::string assigned_set_id;
    int set_configured_age_at_fit_laps = 0;
    CornerHealthVector set_starting_health_at_fit = CornerHealthVector::uniform(1.0);

    int start_race_lap = 1; // 1-based race lap on which this stint started
    int original_target_laps = 0;
    int uncapped_cascaded_target_laps = 0;
    int effective_target_laps = 0;
    int actual_laps_completed = 0;
    int shortfall_laps = 0;
    int accumulated_shortfall_into_stint = 0;

    bool started = false;
    bool completed = false;
    bool ended_by_mandatory_stop = false;
    double cumulative_flying_time_s = 0.0;
    CornerHealthVector final_health = CornerHealthVector::uniform(1.0);
};

/**
 * @brief Complete strategy specification provided by the user.
 */
struct StrategyPlan {
    std::vector<StintSpec> stints;
};

/**
 * @brief Field-level gap entry in the engine's readiness and capability report.
 */
struct SchemaAndParameterGap {
    std::string schema_or_component;
    std::string field_path;
    SourceStatus status = SourceStatus::UnknownUnavailable;
    MissingPolicy missing_policy = MissingPolicy::RejectSimulation;
    bool covered_by_authorised_fallback = false;
    bool blocks_execution = false;
    std::string detail;
};

/**
 * @brief Comprehensive capability and readiness assessment (Architecture Spec Section 6).
 *
 * Explicitly separates DAL bundle load validity, user input validity, mathematical
 * capability, calibration status, and execution permission.
 */
struct CapabilityReadinessReport {
    bool bundle_load_valid = false;
    bool user_input_valid = false;
    bool mathematical_capability_sufficient = false;
    FidelityStatus fidelity_status = FidelityStatus::InvalidOrBlocked;
    ExecutionPermission execution_permission = ExecutionPermission::BlockedInvalidBundle;
    ModelStage evaluated_stage = ModelStage::M0_DeterministicBaseline;

    std::vector<std::string> supported_schemas;
    std::vector<std::string> missing_repository_schemas;
    std::vector<SchemaAndParameterGap> parameter_gaps;
    std::vector<Diagnostic> diagnostics;

    bool canExecute() const noexcept {
        return execution_permission == ExecutionPermission::AllowedCalibrated ||
               execution_permission == ExecutionPermission::AllowedLowerFidelity;
    }
};

/**
 * @brief Detailed additive breakdown of a single flying lap evaluation (Equation 6.1).
 */
struct LapTimeBreakdown {
    double ref_lap_time_s = 0.0;          // T_ref(r, v)
    double fuel_offset_s = 0.0;           // dT_fuel,k
    double track_evolution_offset_s = 0.0;// dT_evol,k
    double standing_start_offset_s = 0.0; // dT_start,k (non-zero only for k == 1)
    double compound_offset_s = 0.0;       // dT_comp(c, r)
    double tyre_degradation_loss_s = 0.0; // dT_tyre,k (linear + cliff in M3)
    double linear_tyre_deg_component_s = 0.0;
    double cliff_tyre_deg_component_s = 0.0;
    double outlap_warmup_loss_s = 0.0;    // dT_in/out,k (non-zero on first lap after pit stop)
    double other_offset_s = 0.0;          // dT_other,k (0.0 in baseline)
    double total_flying_lap_time_s = 0.0; // T_k
};

/**
 * @brief Detailed multiplicative breakdown of tyre health depletion on lap k (Equations 7.1-7.4, 8.1).
 */
struct TyreStepBreakdown {
    double base_wear_rate_d0 = 0.0;
    double length_scale_ratio = 1.0;
    std::array<double, 4> corner_stress_indices{1.0, 1.0, 1.0, 1.0}; // S_str(r, i)
    std::array<double, 4> static_depletion_rates{0.0, 0.0, 0.0, 0.0};// D_base(c, r, i)
    double environmental_multiplier_f_env = 1.0;                     // F_env(c, w_k)
    double usage_load_multiplier_f_usage = 1.0;                      // F_usage(k)
    double effective_track_temp_c = 35.0;
    bool track_temp_inferred_from_radiation = false;
    CornerHealthVector health_start = CornerHealthVector::uniform(1.0);
    std::array<double, 4> health_depletion_delta{0.0, 0.0, 0.0, 0.0};
    CornerHealthVector health_end = CornerHealthVector::uniform(1.0);
    double effective_wear_w_bar_start = 0.0;
};

/**
 * @brief Reason why a pit stop was executed.
 */
enum class PitStopReason {
    PlannedStrategyBoundary,
    MandatoryHealthThreshold,
    MandatoryAndPlannedBoundary,
    InteractiveUserPitDecision
};

inline const char* toString(PitStopReason reason) noexcept {
    switch (reason) {
        case PitStopReason::PlannedStrategyBoundary:
            return "PLANNED_STRATEGY_BOUNDARY";
        case PitStopReason::MandatoryHealthThreshold:
            return "MANDATORY_HEALTH_THRESHOLD";
        case PitStopReason::MandatoryAndPlannedBoundary:
            return "MANDATORY_AND_PLANNED_BOUNDARY";
        case PitStopReason::InteractiveUserPitDecision:
            return "INTERACTIVE_USER_PIT_DECISION";
    }
    return "UNKNOWN_PIT_STOP_REASON";
}

/**
 * @brief Detailed record of a completed pit stop (Design Spec Section 11 & Eq. 9.1).
 */
struct PitStopRecord {
    int stop_sequence_number = 1; // 1-based (1 .. J-1)
    int completed_race_lap = 1;   // Lap L_j at the end of which the stop occurred
    PitStopReason reason = PitStopReason::PlannedStrategyBoundary;
    bool aggregate_threshold_breached = false;
    bool individual_threshold_breached = false;
    bool coincided_with_planned_boundary = false;

    int outgoing_stint_index = 0;
    int incoming_stint_index = 1;
    std::string set_removed_id;
    TyreRole role_removed = TyreRole::Soft;
    PhysicalCompoundId compound_removed = PhysicalCompoundId::C3;
    CornerHealthVector removed_set_final_health = CornerHealthVector::uniform(1.0);

    std::string set_fitted_id;
    TyreRole role_fitted = TyreRole::Medium;
    PhysicalCompoundId compound_fitted = PhysicalCompoundId::C2;
    int fitted_set_configured_age_laps = 0;
    CornerHealthVector fitted_set_starting_health = CornerHealthVector::uniform(1.0);

    // Timing components (Eq. 9.1)
    double pit_lane_traversal_net_loss_s = 0.0;
    double stationary_service_time_s = 0.0;
    double pit_accel_decel_loss_s = 0.0;
    double total_stop_time_loss_s = 0.0;

    // Shortfall & cascade accounting
    bool shortened_planned_stint = false;
    int outgoing_stint_target_laps = 0;
    int outgoing_stint_actual_laps = 0;
    int stint_shortfall_laps = 0;
    int accumulated_shortfall_laps_after_stop = 0;
    int next_stint_original_target_laps = 0;
    int next_stint_uncapped_cascaded_target_laps = 0;
    int next_stint_effective_target_laps = 0;
};

/**
 * @brief Complete trace entry for a single completed race lap k in {1 .. L}.
 */
struct LapTraceEntry {
    int race_lap = 1;
    int stint_index = 0;
    int stint_lap_number = 1; // m in {1 .. n_j}
    std::string tyre_set_id;
    TyreRole role = TyreRole::Soft;
    PhysicalCompoundId physical_compound = PhysicalCompoundId::C3;

    int tyre_age_start_laps = 0;
    int tyre_age_end_laps = 1;
    CornerHealthVector health_start = CornerHealthVector::uniform(1.0);
    CornerHealthVector health_end = CornerHealthVector::uniform(1.0);

    double fuel_mid_lap_kg = 0.0;
    double fuel_end_lap_kg = 0.0;
    double effective_vehicle_mass_mid_lap_kg = 0.0;

    LapTimeBreakdown lap_breakdown{};
    TyreStepBreakdown tyre_breakdown{};

    bool pit_stop_occurred_after_lap = false;
    double pit_stop_loss_after_lap_s = 0.0;
    std::optional<PitStopReason> pit_stop_reason = std::nullopt;
    double cumulative_elapsed_time_s = 0.0;
    MandatoryStopCheckResult mandatory_check_after_lap{};
};

/**
 * @brief Reason why Interactive mode paused and requires a user decision.
 */
enum class DecisionReason {
    PlannedStintBoundary,
    MandatoryHealthStopReplanRequired
};

inline const char* toString(DecisionReason reason) noexcept {
    switch (reason) {
        case DecisionReason::PlannedStintBoundary:
            return "PLANNED_STINT_BOUNDARY";
        case DecisionReason::MandatoryHealthStopReplanRequired:
            return "MANDATORY_HEALTH_STOP_REPLAN_REQUIRED";
    }
    return "UNKNOWN_DECISION_REASON";
}

/**
 * @brief Structured decision-required prompt emitted by the engine in Interactive mode.
 */
struct PendingDecisionPrompt {
    DecisionReason reason = DecisionReason::PlannedStintBoundary;
    int completed_race_lap = 0;
    int remaining_race_laps = 0;
    int current_stint_index = 0;
    std::string current_tyre_set_id;
    TyreRole current_role = TyreRole::Soft;
    PhysicalCompoundId current_compound = PhysicalCompoundId::C3;
    int current_stint_actual_laps = 0;
    int current_stint_target_laps = 0;
    int current_stint_shortfall_laps = 0;
    CornerHealthVector current_tyre_health = CornerHealthVector::uniform(1.0);
    double current_fuel_remaining_kg = 0.0;
    double cumulative_elapsed_time_s = 0.0;

    bool mandatory_stop_required = false;
    bool extension_permitted = true;
    std::vector<StintRuntimeRecord> current_stint_plan;
    std::string explanation;
};

/**
 * @brief Action type submitted by the user at an Interactive mode decision point.
 */
enum class DecisionActionType {
    KeepOriginalPlan,       // Pit now onto next planned stint without modifying remaining stints
    ExtendCurrentStint,     // Add extension_laps > 0 to current stint and update remaining stints
    PitNowWithRevisedPlan   // Pit now onto revised remaining stints (lengths and/or roles)
};

inline const char* toString(DecisionActionType type) noexcept {
    switch (type) {
        case DecisionActionType::KeepOriginalPlan:      return "KEEP_ORIGINAL_PLAN";
        case DecisionActionType::ExtendCurrentStint:    return "EXTEND_CURRENT_STINT";
        case DecisionActionType::PitNowWithRevisedPlan: return "PIT_NOW_WITH_REVISED_PLAN";
    }
    return "UNKNOWN_DECISION_ACTION";
}

/**
 * @brief User decision payload submitted via applyDecision(...) in Interactive mode.
 */
struct InteractiveDecision {
    DecisionActionType action_type = DecisionActionType::KeepOriginalPlan;
    int extension_laps = 0; // Used when action_type == ExtendCurrentStint (must be > 0)
    std::optional<std::vector<StintSpec>> revised_remaining_stints = std::nullopt;
    std::string user_note;
};

/**
 * @brief Audit record of a committed user intervention or Preplanned cascade adjustment.
 */
struct StrategyInterventionRecord {
    std::size_t sequence_id = 1;
    int completed_race_lap = 0;
    RaceMode mode = RaceMode::Interactive;
    std::string intervention_type; // "PREPLANNED_SHORTFALL_CASCADE" or DecisionActionType label
    int stint_index = 0;
    int extension_laps_requested = 0;
    std::vector<StintSpec> plan_before;
    std::vector<StintSpec> plan_after;
    std::string summary;
};

/**
 * @brief Structured simulation event emitted during session lifecycle and lap progression.
 */
enum class SimulationEventType {
    EventSelected,
    InventoryAgeUpdated,
    SessionValidated,
    SessionStarted,
    LapCompleted,
    MandatoryStopTriggered,
    PreplannedShortfallCascaded,
    PitStopExecuted,
    DecisionRequiredEmitted,
    InteractiveDecisionApplied,
    SessionPaused,
    SessionResumed,
    SessionCompleted,
    SessionFailed,
    SessionCancelled,
    RuntimeStateReset
};

inline const char* toString(SimulationEventType type) noexcept {
    switch (type) {
        case SimulationEventType::EventSelected:               return "EVENT_SELECTED";
        case SimulationEventType::InventoryAgeUpdated:         return "INVENTORY_AGE_UPDATED";
        case SimulationEventType::SessionValidated:            return "SESSION_VALIDATED";
        case SimulationEventType::SessionStarted:              return "SESSION_STARTED";
        case SimulationEventType::LapCompleted:                return "LAP_COMPLETED";
        case SimulationEventType::MandatoryStopTriggered:      return "MANDATORY_STOP_TRIGGERED";
        case SimulationEventType::PreplannedShortfallCascaded: return "PREPLANNED_SHORTFALL_CASCADED";
        case SimulationEventType::PitStopExecuted:             return "PIT_STOP_EXECUTED";
        case SimulationEventType::DecisionRequiredEmitted:     return "DECISION_REQUIRED_EMITTED";
        case SimulationEventType::InteractiveDecisionApplied:  return "INTERACTIVE_DECISION_APPLIED";
        case SimulationEventType::SessionPaused:               return "SESSION_PAUSED";
        case SimulationEventType::SessionResumed:              return "SESSION_RESUMED";
        case SimulationEventType::SessionCompleted:            return "SESSION_COMPLETED";
        case SimulationEventType::SessionFailed:               return "SESSION_FAILED";
        case SimulationEventType::SessionCancelled:            return "SESSION_CANCELLED";
        case SimulationEventType::RuntimeStateReset:           return "RUNTIME_STATE_RESET";
    }
    return "UNKNOWN_SIMULATION_EVENT";
}

struct SimulationEvent {
    std::size_t event_sequence_id = 0;
    SimulationEventType type = SimulationEventType::EventSelected;
    int race_lap = 0;
    int stint_index = 0;
    std::string message;
};

/**
 * @brief Complete, self-contained session state snapshot supporting deterministic
 *        pause/resume and UI inspection without mutating the engine.
 */
struct SessionSnapshot {
    SessionState session_state = SessionState::Unconfigured;
    RaceMode race_mode = RaceMode::Preplanned;
    ModelStage model_stage = ModelStage::M0_DeterministicBaseline;
    FidelityStatus fidelity_status = FidelityStatus::UncalibratedIncomplete;

    std::string event_id;
    std::string track_id;
    int layout_version = 0;
    int surface_version = 0;
    int season_year = 0;
    int total_race_laps = 0;

    VehicleConfig vehicle_config{};
    EnvironmentScenarioConfig environment_config{};
    ModelParameters model_parameters{};
    StrategyPlan initial_strategy_plan{};

    int completed_laps = 0;
    int current_stint_index = 0;
    std::string active_tyre_set_id;
    int accumulated_shortfall_laps = 0;
    double elapsed_race_time_s = 0.0;
    double total_flying_lap_time_s = 0.0;
    double total_pit_stop_loss_s = 0.0;
    double current_fuel_remaining_kg = 0.0;
    double total_fuel_consumed_kg = 0.0;

    std::vector<TyreSetState> tyre_inventory;
    std::vector<StintRuntimeRecord> stint_records;
    std::optional<PendingDecisionPrompt> pending_decision = std::nullopt;
    MandatoryStopCheckResult pending_mandatory_check{};
    bool pending_coincided_with_planned_boundary = false;

    std::vector<LapTraceEntry> lap_trace;
    std::vector<PitStopRecord> pit_stops;
    std::vector<StrategyInterventionRecord> interventions;
    std::vector<SimulationEvent> event_log;
    std::vector<Diagnostic> diagnostics;
};

/**
 * @brief Complete structured race result produced upon completion or termination
 *        (Design Spec Section 12.1).
 */
struct RaceResult {
    SessionState terminal_state = SessionState::Completed;
    bool completed_full_race_distance = false;
    RaceMode race_mode = RaceMode::Preplanned;
    ModelStage model_stage = ModelStage::M0_DeterministicBaseline;
    FidelityStatus fidelity_status = FidelityStatus::UncalibratedIncomplete;
    std::string simulator_math_version = "1.0.0";

    // Event & data identity
    std::string event_id;
    std::string official_event_name;
    std::string track_id;
    int layout_version = 0;
    int surface_version = 0;
    int season_year = 0;
    int compound_catalogue_spec_version = 1;

    // Declared inputs
    VehicleConfig vehicle_config{};
    EnvironmentScenarioConfig environment_config{};
    ModelParameters model_parameters{};
    StrategyPlan initial_strategy{};

    // Summary metrics
    int configured_race_distance_laps = 0;
    int completed_race_laps = 0;
    double total_race_time_s = 0.0;
    double total_flying_lap_time_s = 0.0;
    double total_pit_stop_time_loss_s = 0.0;
    int total_pit_stops = 0;
    int planned_pit_stops = 0;
    int mandatory_health_stops = 0;

    double initial_fuel_kg = 0.0;
    double fuel_consumed_kg = 0.0;
    double final_fuel_remaining_kg = 0.0;

    // Detailed traces & logs
    std::vector<StintRuntimeRecord> stints;
    std::vector<LapTraceEntry> lap_trace;
    std::vector<PitStopRecord> pit_stops;
    std::vector<StrategyInterventionRecord> interventions;
    std::vector<SimulationEvent> event_log;
    std::vector<Diagnostic> diagnostics;
    std::string compatibility_and_fidelity_note;
};

} // namespace simulation
} // namespace f1_sim

#endif // F1_SIM_SIMULATION_ENGINE_TYPES_HPP
