#ifndef F1_SIM_SIMULATION_LAP_EVALUATOR_HPP
#define F1_SIM_SIMULATION_LAP_EVALUATOR_HPP

#include "f1_sim/simulation/engine_types.hpp"

namespace f1_sim {
namespace simulation {

/**
 * @brief Effective resolved compound parameters for a single physical compound C1..C5,
 *        combining the DAL's PhysicalCompoundSpec with any explicit CompoundParameterOverride
 *        and framework-authorised defaults.
 */
struct ResolvedCompoundParams {
    PhysicalCompoundId compound_id = PhysicalCompoundId::C3;
    int hardness_rank = 3;
    SourceStatus source_status = SourceStatus::UnknownUnavailable;
    bool has_required_baseline_calibration = false;

    double pace_offset_ref_s = 0.0;        // dT_comp,0(c)
    double base_wear_rate_per_lap = 0.0;   // d_{0,c}
    double linear_deg_coeff_s = 0.0;       // alpha_c
    double cliff_wear_threshold = 1.0;     // w_crit,c (default 1.0 = linear only)
    double cliff_amplitude_s = 0.0;        // beta_c (default 0.0 s)
    double cliff_exponent = 2.0;           // eta_c (default 2.0)
    double thermal_deg_sens_per_c = 0.01;  // kappa_trk,c (default 0.01 C^-1)
    double ambient_air_deg_sens_per_c = 0.0;// kappa_air,c (default 0.0 C^-1)
};

/**
 * @brief Output of a stateless stint rollout evaluation (shared by the Simulation Engine
 *        and the future Stint-Boundary DAG Strategy Optimiser, Framework Section 10.4).
 */
struct StintRolloutEvaluation {
    bool feasible = false;
    int start_lap_completed = 0; // l_0 in {0 .. L-1}
    int end_lap_completed = 0;   // l_1 in {1 .. L}
    TyreRole role = TyreRole::Soft;
    PhysicalCompoundId compound_id = PhysicalCompoundId::C3;
    double cumulative_flying_time_s = 0.0;
    CornerHealthVector initial_health = CornerHealthVector::uniform(1.0);
    CornerHealthVector final_health = CornerHealthVector::uniform(1.0);
    bool breached_mandatory_stop_threshold = false;
    bool breached_structural_floor_h_min = false;
    int first_mandatory_breach_lap = 0;
    std::vector<LapTraceEntry> lap_traces;
};

/**
 * @brief Stateless, deterministic single-source mathematical evaluator implementing
 *        the equations of `docs/f1_simulator_mathematical_framework.md`.
 *
 * Shared across:
 *  1. Preplanned Race mode
 *  2. Interactive Race mode
 *  3. Inventory age-to-health derivation
 *  4. Future Stint-Boundary DAG Strategy Optimiser (`evaluateStintRollout`)
 */
class LapStintEvaluator {
public:
    static constexpr double kReferenceLapLengthM = 5000.0;
    static constexpr double kReferenceTrackTempC = 35.0;
    static constexpr double kReferenceAirTempC = 25.0;
    static constexpr double kReferenceHumidityPct = 50.0;
    static constexpr double kEnvMultiplierMin = 0.50;
    static constexpr double kEnvMultiplierMax = 2.50;

    static ResolvedCompoundParams resolveCompoundParameters(
        const EventBundle& bundle,
        PhysicalCompoundId compound_id,
        const ModelParameters& params);

    static LapEnvironment resolveLapEnvironment(
        const EnvironmentScenarioConfig& env_config,
        int race_lap);

    static double inferTrackTemperatureC(
        const LapEnvironment& env,
        const EnvironmentScenarioConfig& env_config) noexcept;

    static std::array<double, 4> computeCornerAsymmetryMultipliers(
        const data_access::TrackLayoutVersion& layout,
        const VehicleConfig& vehicle,
        ModelStage stage) noexcept;

    static std::array<double, 4> computeStaticCircuitStress(
        const EventBundle& bundle,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage) noexcept;

    static std::array<double, 4> computeStaticBaselineDepletionRate(
        const EventBundle& bundle,
        const ResolvedCompoundParams& comp,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage) noexcept;

    static std::optional<CornerHealthVector> deriveStartingHealthFromAge(
        int configured_age_laps,
        const EventBundle& bundle,
        const ResolvedCompoundParams& comp,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage) noexcept;

    static double computeEnvironmentalMultiplier(
        const ResolvedCompoundParams& comp,
        const LapEnvironment& env,
        const EnvironmentScenarioConfig& env_config,
        const ModelParameters& params,
        ModelStage stage,
        double& out_effective_track_temp_c,
        bool& out_inferred_track_temp) noexcept;

    static double resolveFuelBurnRateKgPerLap(
        const EventBundle& bundle,
        const VehicleConfig& vehicle) noexcept;

    static void computeLapFuelState(
        int race_lap,
        const EventBundle& bundle,
        const VehicleConfig& vehicle,
        double& out_mid_lap_fuel_kg,
        double& out_end_lap_fuel_kg) noexcept;

    static double computeUsageLoadMultiplier(
        double mid_lap_fuel_kg,
        const VehicleConfig& vehicle,
        const ModelParameters& params) noexcept;

    static double computeEffectiveWear(
        const CornerHealthVector& health,
        const ModelParameters& params,
        ModelStage stage) noexcept;

    static EngineResult<LapTimeBreakdown> evaluateFlyingLap(
        int race_lap,
        int stint_index,
        int stint_lap_number,
        const CornerHealthVector& health_start,
        const EventBundle& bundle,
        const ResolvedCompoundParams& comp,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage);

    static EngineResult<TyreStepBreakdown> stepTyreHealth(
        int race_lap,
        const CornerHealthVector& health_start,
        const EventBundle& bundle,
        const ResolvedCompoundParams& comp,
        const VehicleConfig& vehicle,
        const EnvironmentScenarioConfig& env_config,
        const ModelParameters& params,
        ModelStage stage);

    static MandatoryStopCheckResult evaluateMandatoryStopThresholds(
        const CornerHealthVector& health_after_lap) noexcept;

    static EngineResult<PitStopRecord> evaluatePitStopLoss(
        int stop_sequence_number,
        int completed_race_lap,
        const EventBundle& bundle,
        const ModelParameters& params);

    static EngineResult<StintRolloutEvaluation> evaluateStintRollout(
        int start_lap_completed,
        int end_lap_completed,
        TyreRole role,
        int initial_tyre_age_laps,
        const CornerHealthVector& initial_health,
        const EventBundle& bundle,
        const VehicleConfig& vehicle,
        const EnvironmentScenarioConfig& env_config,
        const ModelParameters& params,
        ModelStage stage);
};

} // namespace simulation
} // namespace f1_sim

#endif // F1_SIM_SIMULATION_LAP_EVALUATOR_HPP
