#include "f1_sim/simulation/lap_evaluator.hpp"

#include <algorithm>
#include <cmath>

namespace f1_sim {
namespace simulation {

ResolvedCompoundParams LapStintEvaluator::resolveCompoundParameters(
    const EventBundle& bundle,
    PhysicalCompoundId compound_id,
    const ModelParameters& params) {
    ResolvedCompoundParams out;
    out.compound_id = compound_id;
    out.hardness_rank = data_access::canonicalHardnessRank(compound_id);

    const PhysicalCompoundSpec* spec = bundle.compound_catalogue.findCompound(compound_id);
    if (spec != nullptr) {
        out.hardness_rank = spec->hardness_rank;
    }

    bool has_pace = false;
    double pace_val = 0.0;
    bool has_wear = false;
    double wear_val = 0.0;
    bool has_deg = false;
    double deg_val = 0.0;

    out.cliff_wear_threshold = 1.0;
    out.cliff_amplitude_s = 0.0;
    out.cliff_exponent = 2.0;
    out.thermal_deg_sens_per_c = 0.01;
    out.ambient_air_deg_sens_per_c = 0.0;

    if (spec != nullptr) {
        if (spec->pace_offset_ref_s.has_value()) {
            has_pace = true;
            pace_val = *spec->pace_offset_ref_s;
        }
        if (spec->base_wear_rate_per_lap.has_value()) {
            has_wear = true;
            wear_val = *spec->base_wear_rate_per_lap;
        }
        if (spec->linear_deg_coeff_s.has_value()) {
            has_deg = true;
            deg_val = *spec->linear_deg_coeff_s;
        }
        if (spec->cliff_wear_threshold.has_value()) {
            out.cliff_wear_threshold = *spec->cliff_wear_threshold;
        }
        if (spec->cliff_amplitude_s.has_value()) {
            out.cliff_amplitude_s = *spec->cliff_amplitude_s;
        }
        if (spec->cliff_exponent.has_value()) {
            out.cliff_exponent = *spec->cliff_exponent;
        }
        if (spec->thermal_deg_sens_per_c.has_value()) {
            out.thermal_deg_sens_per_c = *spec->thermal_deg_sens_per_c;
        }

        if (spec->hasBaselineLinearCalibration()) {
            out.source_status = SourceStatus::Calibrated;
            auto it_prov = spec->field_provenance.find("calibrated_pace_and_wear_coefficients");
            if (it_prov != spec->field_provenance.end() &&
                it_prov->second.source_status != SourceStatus::UnknownUnavailable) {
                out.source_status = it_prov->second.source_status;
            }
        }
    }

    auto ov_it = params.compound_overrides.find(compound_id);
    if (ov_it != params.compound_overrides.end()) {
        const CompoundParameterOverride& ov = ov_it->second;
        if (ov.pace_offset_ref_s.has_value()) {
            has_pace = true;
            pace_val = *ov.pace_offset_ref_s;
        }
        if (ov.base_wear_rate_per_lap.has_value()) {
            has_wear = true;
            wear_val = *ov.base_wear_rate_per_lap;
        }
        if (ov.linear_deg_coeff_s.has_value()) {
            has_deg = true;
            deg_val = *ov.linear_deg_coeff_s;
        }
        if (ov.cliff_wear_threshold.has_value()) {
            out.cliff_wear_threshold = *ov.cliff_wear_threshold;
        }
        if (ov.cliff_amplitude_s.has_value()) {
            out.cliff_amplitude_s = *ov.cliff_amplitude_s;
        }
        if (ov.cliff_exponent.has_value()) {
            out.cliff_exponent = *ov.cliff_exponent;
        }
        if (ov.thermal_deg_sens_per_c.has_value()) {
            out.thermal_deg_sens_per_c = *ov.thermal_deg_sens_per_c;
        }
        if (ov.ambient_air_deg_sens_per_c.has_value()) {
            out.ambient_air_deg_sens_per_c = *ov.ambient_air_deg_sens_per_c;
        }
        out.source_status = ov.source_status;
    }

    out.has_required_baseline_calibration =
        has_pace && has_wear && has_deg &&
        std::isfinite(pace_val) && std::isfinite(wear_val) && std::isfinite(deg_val) &&
        (wear_val >= 0.0) && (deg_val >= 0.0);

    if (has_pace) out.pace_offset_ref_s = pace_val;
    if (has_wear) out.base_wear_rate_per_lap = wear_val;
    if (has_deg)  out.linear_deg_coeff_s = deg_val;

    return out;
}

LapEnvironment LapStintEvaluator::resolveLapEnvironment(
    const EnvironmentScenarioConfig& env_config,
    int race_lap) {
    if (env_config.lap_series.empty()) {
        LapEnvironment env = env_config.constant_environment;
        env.lap_index = race_lap;
        return env;
    }

    const LapEnvironment* best = &env_config.lap_series.front();
    for (const auto& entry : env_config.lap_series) {
        if (entry.lap_index == race_lap) {
            return entry;
        }
        if (entry.lap_index <= race_lap && entry.lap_index >= best->lap_index) {
            best = &entry;
        }
    }
    LapEnvironment resolved = *best;
    resolved.lap_index = race_lap;
    return resolved;
}

double LapStintEvaluator::inferTrackTemperatureC(
    const LapEnvironment& env,
    const EnvironmentScenarioConfig& env_config) noexcept {
    // Equation (8.2): T_trk_hat = T_air + ((1 - alpha_alb) * I_sol) / (h_rad,0 + h_wind * v_wind)
    const double denom = env_config.radiative_conductance_wm2c +
                         env_config.wind_convection_coeff_wsm3c * std::max(0.0, env.wind_speed_ms);
    if (denom <= 1e-6) {
        return env.air_temperature_c;
    }
    const double numer = (1.0 - std::clamp(env_config.asphalt_albedo, 0.0, 1.0)) *
                         std::max(0.0, env.solar_radiation_wm2);
    return env.air_temperature_c + (numer / denom);
}

std::array<double, 4> LapStintEvaluator::computeCornerAsymmetryMultipliers(
    const data_access::TrackLayoutVersion& layout,
    const VehicleConfig& vehicle,
    ModelStage stage) noexcept {
    if (stage != ModelStage::M3_NonlinearCliffAndFourCorner) {
        return {1.0, 1.0, 1.0, 1.0};
    }

    const int tot = std::max(1, layout.corners_total_count);
    const double right_minus_left_frac =
        static_cast<double>(layout.corners_right_count - layout.corners_left_count) /
        static_cast<double>(tot);

    const double lateral_asym = 0.15 * right_minus_left_frac;
    const double left_mult = 1.0 + lateral_asym;
    const double right_mult = 1.0 - lateral_asym;

    const double front_frac = std::clamp(vehicle.front_weight_dist_frac, 0.40, 0.60);
    const double front_mult = 1.0 + 0.25 * (front_frac - 0.46);
    const double rear_mult  = 1.0 - 0.25 * (front_frac - 0.46);

    std::array<double, 4> raw{
        left_mult * front_mult,  // FL
        right_mult * front_mult, // FR
        left_mult * rear_mult,   // RL
        right_mult * rear_mult   // RR
    };
    const double mean_raw = 0.25 * (raw[0] + raw[1] + raw[2] + raw[3]);
    if (mean_raw > 1e-9) {
        for (double& v : raw) {
            v /= mean_raw;
        }
    }
    return raw;
}

std::array<double, 4> LapStintEvaluator::computeStaticCircuitStress(
    const EventBundle& bundle,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) noexcept {
    if (stage == ModelStage::M0_DeterministicBaseline) {
        return {1.0, 1.0, 1.0, 1.0};
    }

    const auto lambdas = computeCornerAsymmetryMultipliers(bundle.track_layout, vehicle, stage);
    const double a_surf = bundle.track_surface.surface_abrasiveness_score;
    const double abr_factor = 1.0 + params.abrasiveness_sensitivity_omega * ((a_surf - 3.0) / 2.0);

    const double e_lat = bundle.track_layout.lateral_energy_index_norm;
    const double e_brk = bundle.track_layout.braking_energy_index_norm;
    const double e_trc = bundle.track_layout.traction_energy_index_norm;

    const double energy_factor = 0.50 * e_lat + 0.25 * e_brk + 0.25 * e_trc;
    const double base_stress = std::clamp(abr_factor * energy_factor, 0.40, 2.50);

    return {
        lambdas[0] * base_stress,
        lambdas[1] * base_stress,
        lambdas[2] * base_stress,
        lambdas[3] * base_stress
    };
}

std::array<double, 4> LapStintEvaluator::computeStaticBaselineDepletionRate(
    const EventBundle& bundle,
    const ResolvedCompoundParams& comp,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) noexcept {
    const double length_ratio = bundle.track_layout.lap_length_m / kReferenceLapLengthM;
    const auto s_str = computeStaticCircuitStress(bundle, vehicle, params, stage);
    const double zeta_v = vehicle.tyre_energy_load_mult;

    std::array<double, 4> d_base{};
    for (std::size_t i = 0; i < 4; ++i) {
        d_base[i] = comp.base_wear_rate_per_lap * length_ratio * s_str[i] * zeta_v;
    }
    return d_base;
}

std::optional<CornerHealthVector> LapStintEvaluator::deriveStartingHealthFromAge(
    int configured_age_laps,
    const EventBundle& bundle,
    const ResolvedCompoundParams& comp,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) noexcept {
    if (configured_age_laps < 0) {
        return std::nullopt;
    }
    if (configured_age_laps == 0) {
        return CornerHealthVector::uniform(1.0);
    }
    if (!comp.has_required_baseline_calibration) {
        return std::nullopt;
    }

    const auto d_base = computeStaticBaselineDepletionRate(bundle, comp, vehicle, params, stage);
    CornerHealthVector h_init;
    for (std::size_t i = 0; i < 4; ++i) {
        const double depleted = static_cast<double>(configured_age_laps) * d_base[i];
        h_init[i] = std::max(0.0, 1.0 - depleted);
    }
    return h_init;
}

double LapStintEvaluator::computeEnvironmentalMultiplier(
    const ResolvedCompoundParams& comp,
    const LapEnvironment& env,
    const EnvironmentScenarioConfig& env_config,
    const ModelParameters& params,
    ModelStage /*stage*/,
    double& out_effective_track_temp_c,
    bool& out_inferred_track_temp) noexcept {
    if (env.track_temperature_c.has_value()) {
        out_effective_track_temp_c = *env.track_temperature_c;
        out_inferred_track_temp = false;
    } else {
        out_effective_track_temp_c = inferTrackTemperatureC(env, env_config);
        out_inferred_track_temp = true;
    }

    const double dT_trk = out_effective_track_temp_c - kReferenceTrackTempC;
    const double dT_air = env.air_temperature_c - kReferenceAirTempC;
    const double dPhi   = (env.relative_humidity_pct - kReferenceHumidityPct) / 100.0;

    const double exponent_term =
        comp.thermal_deg_sens_per_c * dT_trk +
        comp.ambient_air_deg_sens_per_c * dT_air +
        params.humidity_wear_sens_psi * dPhi;

    double raw_mult = 1.0;
    if (env_config.model_form == EnvironmentModelForm::ExponentialLogLinear) {
        raw_mult = std::exp(exponent_term);
    } else {
        raw_mult = 1.0 + exponent_term;
    }
    return std::clamp(raw_mult, kEnvMultiplierMin, kEnvMultiplierMax);
}

double LapStintEvaluator::resolveFuelBurnRateKgPerLap(
    const EventBundle& bundle,
    const VehicleConfig& vehicle) noexcept {
    if (vehicle.fuel_burn_rate_kg_per_lap.has_value()) {
        return *vehicle.fuel_burn_rate_kg_per_lap;
    }
    const int race_laps = std::max(1, bundle.event_config.race_distance_laps);
    return vehicle.initial_fuel_load_kg / static_cast<double>(race_laps);
}

void LapStintEvaluator::computeLapFuelState(
    int race_lap,
    const EventBundle& bundle,
    const VehicleConfig& vehicle,
    double& out_mid_lap_fuel_kg,
    double& out_end_lap_fuel_kg) noexcept {
    const double mu_fuel = resolveFuelBurnRateKgPerLap(bundle, vehicle);
    const double k_double = static_cast<double>(race_lap);
    out_mid_lap_fuel_kg = std::max(0.0, vehicle.initial_fuel_load_kg - mu_fuel * (k_double - 0.5));
    out_end_lap_fuel_kg = std::max(0.0, vehicle.initial_fuel_load_kg - mu_fuel * k_double);
}

double LapStintEvaluator::computeUsageLoadMultiplier(
    double mid_lap_fuel_kg,
    const VehicleConfig& vehicle,
    const ModelParameters& /*params*/) noexcept {
    const double denom = vehicle.base_dry_mass_with_ref_driver_kg + VehicleConfig::kReferenceFuelMassKg;
    if (denom <= 1e-6 || std::fabs(vehicle.load_wear_sensitivity_kappa) <= 1e-12) {
        return 1.0;
    }
    const double effective_mass_delta =
        (mid_lap_fuel_kg + vehicle.driverMassDeltaKg()) - VehicleConfig::kReferenceFuelMassKg;
    const double mult = 1.0 + vehicle.load_wear_sensitivity_kappa * (effective_mass_delta / denom);
    return std::max(0.10, mult);
}

double LapStintEvaluator::computeEffectiveWear(
    const CornerHealthVector& health,
    const ModelParameters& params,
    ModelStage stage) noexcept {
    if (stage != ModelStage::M3_NonlinearCliffAndFourCorner) {
        return std::clamp(health.meanWear(), 0.0, 1.0);
    }
    const double xi = std::clamp(params.limiting_corner_weight_xi, 0.0, 1.0);
    const double w_bar = (1.0 - xi) * health.meanWear() + xi * health.maxCornerWear();
    return std::clamp(w_bar, 0.0, 1.0);
}

EngineResult<LapTimeBreakdown> LapStintEvaluator::evaluateFlyingLap(
    int race_lap,
    int stint_index,
    int stint_lap_number,
    const CornerHealthVector& health_start,
    const EventBundle& bundle,
    const ResolvedCompoundParams& comp,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) {
    if (!params.ref_lap_time_s.has_value() || !std::isfinite(*params.ref_lap_time_s)) {
        return EngineResult<LapTimeBreakdown>::failure(
            EngineStatus::InsufficientCapabilityOrCalibration,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Cannot evaluate flying lap time: ref_lap_time_s (T_ref) is missing or non-finite.",
                bundle.event_config.event_id,
                "",
                "/model_parameters/ref_lap_time_s",
                "finite double in [40.0, 180.0]",
                "null or non-finite"
            }});
    }

    if (!comp.has_required_baseline_calibration) {
        return EngineResult<LapTimeBreakdown>::failure(
            EngineStatus::InsufficientCapabilityOrCalibration,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                std::string("Cannot evaluate flying lap time: compound ") +
                    data_access::toString(comp.compound_id) + " lacks required pace/degradation calibration.",
                bundle.event_config.event_id,
                "",
                std::string("/compounds/") + data_access::toString(comp.compound_id),
                "calibrated pace_offset_ref_s, base_wear_rate_per_lap, linear_deg_coeff_s",
                "null/uncalibrated"
            }});
    }

    LapTimeBreakdown out;
    out.ref_lap_time_s = *params.ref_lap_time_s;

    const double length_ratio = bundle.track_layout.lap_length_m / kReferenceLapLengthM;

    double mid_fuel_kg = 0.0;
    double end_fuel_kg = 0.0;
    computeLapFuelState(race_lap, bundle, vehicle, mid_fuel_kg, end_fuel_kg);
    const double gamma_fuel = params.scale_fuel_sens_by_lap_length
        ? (params.fuel_lap_sens_s_per_kg * length_ratio)
        : params.fuel_lap_sens_s_per_kg;
    const double mass_deviation_kg =
        (mid_fuel_kg + vehicle.driverMassDeltaKg()) - VehicleConfig::kReferenceFuelMassKg;
    out.fuel_offset_s = gamma_fuel * mass_deviation_kg;

    const double a_evol = bundle.track_surface.track_evolution_amplitude_s;
    const double tau_evol = std::max(1e-6, bundle.track_surface.track_evolution_decay_laps);
    if (a_evol > 0.0) {
        out.track_evolution_offset_s =
            a_evol * std::exp(-static_cast<double>(race_lap - 1) / tau_evol);
    }

    if (race_lap == 1) {
        out.standing_start_offset_s = params.lap1_standing_start_loss_s;
    }

    out.compound_offset_s =
        comp.pace_offset_ref_s * length_ratio * params.grip_scaling_psi;

    const double w_bar = computeEffectiveWear(health_start, params, stage);
    out.linear_tyre_deg_component_s = comp.linear_deg_coeff_s * length_ratio * w_bar;

    if (comp.cliff_amplitude_s > 0.0 && comp.cliff_wear_threshold < 1.0 &&
        w_bar > comp.cliff_wear_threshold) {
        const double denom = std::max(1e-9, 1.0 - comp.cliff_wear_threshold);
        const double normalized_excursion = (w_bar - comp.cliff_wear_threshold) / denom;
        out.cliff_tyre_deg_component_s =
            length_ratio * comp.cliff_amplitude_s * std::pow(normalized_excursion, comp.cliff_exponent);
    }
    out.tyre_degradation_loss_s =
        out.linear_tyre_deg_component_s + out.cliff_tyre_deg_component_s;

    if (stint_index >= 1 && stint_lap_number == 1) {
        out.outlap_warmup_loss_s = params.outlap_warmup_loss_s;
    }

    out.other_offset_s = 0.0;
    out.total_flying_lap_time_s =
        out.ref_lap_time_s +
        out.fuel_offset_s +
        out.track_evolution_offset_s +
        out.standing_start_offset_s +
        out.compound_offset_s +
        out.tyre_degradation_loss_s +
        out.outlap_warmup_loss_s +
        out.other_offset_s;

    if (!std::isfinite(out.total_flying_lap_time_s) || out.total_flying_lap_time_s <= 0.0) {
        return EngineResult<LapTimeBreakdown>::failure(
            EngineStatus::RuntimeConstraintFailure,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Computed flying lap time is non-finite or non-positive.",
                bundle.event_config.event_id,
                "",
                "/lap_evaluator/total_flying_lap_time_s",
                "> 0.0 s finite",
                std::to_string(out.total_flying_lap_time_s)
            }});
    }

    return EngineResult<LapTimeBreakdown>::success(out);
}

EngineResult<TyreStepBreakdown> LapStintEvaluator::stepTyreHealth(
    int race_lap,
    const CornerHealthVector& health_start,
    const EventBundle& bundle,
    const ResolvedCompoundParams& comp,
    const VehicleConfig& vehicle,
    const EnvironmentScenarioConfig& env_config,
    const ModelParameters& params,
    ModelStage stage) {
    if (!comp.has_required_baseline_calibration) {
        return EngineResult<TyreStepBreakdown>::failure(
            EngineStatus::InsufficientCapabilityOrCalibration,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Cannot step tyre health: base_wear_rate_per_lap is uncalibrated.",
                bundle.event_config.event_id,
                "",
                std::string("/compounds/") + data_access::toString(comp.compound_id) + "/base_wear_rate_per_lap",
                "finite double >= 0.0",
                "null"
            }});
    }

    TyreStepBreakdown out;
    out.base_wear_rate_d0 = comp.base_wear_rate_per_lap;
    out.length_scale_ratio = bundle.track_layout.lap_length_m / kReferenceLapLengthM;
    out.corner_stress_indices = computeStaticCircuitStress(bundle, vehicle, params, stage);
    out.static_depletion_rates = computeStaticBaselineDepletionRate(bundle, comp, vehicle, params, stage);

    const LapEnvironment lap_env = resolveLapEnvironment(env_config, race_lap);
    out.environmental_multiplier_f_env = computeEnvironmentalMultiplier(
        comp, lap_env, env_config, params, stage,
        out.effective_track_temp_c, out.track_temp_inferred_from_radiation);

    double mid_fuel_kg = 0.0;
    double end_fuel_kg = 0.0;
    computeLapFuelState(race_lap, bundle, vehicle, mid_fuel_kg, end_fuel_kg);
    out.usage_load_multiplier_f_usage = computeUsageLoadMultiplier(mid_fuel_kg, vehicle, params);

    out.health_start = health_start;
    out.effective_wear_w_bar_start = computeEffectiveWear(health_start, params, stage);

    for (std::size_t i = 0; i < 4; ++i) {
        const double delta =
            out.static_depletion_rates[i] *
            out.environmental_multiplier_f_env *
            out.usage_load_multiplier_f_usage;
        out.health_depletion_delta[i] = std::max(0.0, delta);
        out.health_end[i] = std::clamp(health_start[i] - out.health_depletion_delta[i], 0.0, 1.0);
    }

    return EngineResult<TyreStepBreakdown>::success(out);
}

MandatoryStopCheckResult LapStintEvaluator::evaluateMandatoryStopThresholds(
    const CornerHealthVector& health_after_lap) noexcept {
    MandatoryStopCheckResult res;
    res.observed_mean_health = health_after_lap.meanHealth();
    res.observed_min_corner_health = health_after_lap[0];
    res.limiting_corner_index = 0;

    for (std::size_t i = 1; i < 4; ++i) {
        if (health_after_lap[i] < res.observed_min_corner_health) {
            res.observed_min_corner_health = health_after_lap[i];
            res.limiting_corner_index = i;
        }
    }

    res.aggregate_breached =
        (res.observed_mean_health < MandatoryStopThresholds::kAggregateHealthThreshold);
    res.individual_breached =
        (res.observed_min_corner_health < MandatoryStopThresholds::kIndividualHealthThreshold);
    res.mandatory_stop_required = (res.aggregate_breached || res.individual_breached);
    return res;
}

EngineResult<PitStopRecord> LapStintEvaluator::evaluatePitStopLoss(
    int stop_sequence_number,
    int completed_race_lap,
    const EventBundle& bundle,
    const ModelParameters& params) {
    PitStopRecord rec;
    rec.stop_sequence_number = stop_sequence_number;
    rec.completed_race_lap = completed_race_lap;
    rec.stationary_service_time_s = params.configured_stationary_time_s;

    if (params.pit_loss_total_s.has_value()) {
        const double p_ref = *params.pit_loss_total_s;
        if (!std::isfinite(p_ref) || p_ref < 0.0) {
            return EngineResult<PitStopRecord>::failure(
                EngineStatus::ValidationFailed,
                {Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InvalidRangeOrEnum,
                    "Configured pit_loss_total_s must be finite and >= 0.0 s.",
                    bundle.event_config.event_id,
                    "",
                    "/model_parameters/pit_loss_total_s",
                    ">= 0.0 s",
                    std::to_string(p_ref)
                }});
        }
        rec.pit_accel_decel_loss_s = params.pit_accel_decel_loss_s.value_or(0.0);
        rec.pit_lane_traversal_net_loss_s =
            std::max(0.0, p_ref - params.reference_stationary_time_s - rec.pit_accel_decel_loss_s);
        rec.total_stop_time_loss_s =
            p_ref + (rec.stationary_service_time_s - params.reference_stationary_time_s);
        return EngineResult<PitStopRecord>::success(rec);
    }

    if (params.main_straight_bypass_time_s.has_value() &&
        params.pit_accel_decel_loss_s.has_value()) {
        const double v_pit_ms = bundle.track_layout.pit_speed_limit_kmh / 3.6;
        if (v_pit_ms <= 1e-6) {
            return EngineResult<PitStopRecord>::failure(
                EngineStatus::ValidationFailed,
                {Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InvalidRangeOrEnum,
                    "Track pit_speed_limit_kmh must be positive.",
                    bundle.event_config.event_id,
                    "",
                    "/track_layout/pit_speed_limit_kmh",
                    "> 0",
                    std::to_string(bundle.track_layout.pit_speed_limit_kmh)
                }});
        }
        const double raw_pit_lane_time = bundle.track_layout.pit_lane_length_m / v_pit_ms;
        rec.pit_lane_traversal_net_loss_s =
            raw_pit_lane_time - *params.main_straight_bypass_time_s;
        rec.pit_accel_decel_loss_s = *params.pit_accel_decel_loss_s;
        rec.total_stop_time_loss_s =
            rec.pit_lane_traversal_net_loss_s +
            rec.stationary_service_time_s +
            rec.pit_accel_decel_loss_s;
        return EngineResult<PitStopRecord>::success(rec);
    }

    return EngineResult<PitStopRecord>::failure(
        EngineStatus::InsufficientCapabilityOrCalibration,
        {Diagnostic{
            Severity::Error,
            DiagnosticCode::InsufficientDataForCapability,
            "Cannot evaluate pit-stop time loss: neither pit_loss_total_s nor component pit parameters are provided.",
            bundle.event_config.event_id,
            "",
            "/model_parameters/pit_loss_total_s",
            "non-null pit_loss_total_s or (main_straight_bypass_time_s + pit_accel_decel_loss_s)",
            "null"
        }});
}

EngineResult<StintRolloutEvaluation> LapStintEvaluator::evaluateStintRollout(
    int start_lap_completed,
    int end_lap_completed,
    TyreRole role,
    int initial_tyre_age_laps,
    const CornerHealthVector& initial_health,
    const EventBundle& bundle,
    const VehicleConfig& vehicle,
    const EnvironmentScenarioConfig& env_config,
    const ModelParameters& params,
    ModelStage stage) {
    const int race_laps = bundle.event_config.race_distance_laps;
    if (start_lap_completed < 0 || end_lap_completed <= start_lap_completed ||
        end_lap_completed > race_laps) {
        return EngineResult<StintRolloutEvaluation>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Invalid stint rollout lap bounds [start_lap_completed, end_lap_completed].",
                bundle.event_config.event_id,
                "",
                "/stint_rollout/bounds",
                "0 <= start < end <= L",
                std::to_string(start_lap_completed) + ".." + std::to_string(end_lap_completed)
            }});
    }

    auto comp_id_opt = bundle.role_mapping.compoundForRole(role);
    if (!comp_id_opt.has_value()) {
        return EngineResult<StintRolloutEvaluation>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::CrossReferenceFailure,
                "Role is not mapped in EventRoleMapping.",
                bundle.event_config.event_id,
                "",
                "/role_mapping",
                data_access::toString(role),
                "unmapped"
            }});
    }

    const ResolvedCompoundParams comp = resolveCompoundParameters(bundle, *comp_id_opt, params);
    StintRolloutEvaluation eval;
    eval.start_lap_completed = start_lap_completed;
    eval.end_lap_completed = end_lap_completed;
    eval.role = role;
    eval.compound_id = *comp_id_opt;
    eval.initial_health = initial_health;

    CornerHealthVector h = initial_health;
    int age = initial_tyre_age_laps;
    const int stint_index = (start_lap_completed == 0) ? 0 : 1;

    for (int k = start_lap_completed + 1; k <= end_lap_completed; ++k) {
        const int m = k - start_lap_completed;
        auto lap_res = evaluateFlyingLap(k, stint_index, m, h, bundle, comp, vehicle, params, stage);
        if (!lap_res.ok()) {
            return EngineResult<StintRolloutEvaluation>::failure(lap_res.status, std::move(lap_res.diagnostics));
        }
        auto step_res = stepTyreHealth(k, h, bundle, comp, vehicle, env_config, params, stage);
        if (!step_res.ok()) {
            return EngineResult<StintRolloutEvaluation>::failure(step_res.status, std::move(step_res.diagnostics));
        }

        eval.cumulative_flying_time_s += lap_res.value->total_flying_lap_time_s;
        h = step_res.value->health_end;

        const auto mand_check = evaluateMandatoryStopThresholds(h);
        if (mand_check.mandatory_stop_required && !eval.breached_mandatory_stop_threshold) {
            eval.breached_mandatory_stop_threshold = true;
            eval.first_mandatory_breach_lap = k;
        }
        if (h.minCornerHealth() < params.structural_health_floor_h_min) {
            eval.breached_structural_floor_h_min = true;
        }

        LapTraceEntry trace;
        trace.race_lap = k;
        trace.stint_index = stint_index;
        trace.stint_lap_number = m;
        trace.role = role;
        trace.physical_compound = *comp_id_opt;
        trace.tyre_age_start_laps = age;
        trace.tyre_age_end_laps = age + 1;
        trace.health_start = step_res.value->health_start;
        trace.health_end = h;
        computeLapFuelState(k, bundle, vehicle, trace.fuel_mid_lap_kg, trace.fuel_end_lap_kg);
        trace.effective_vehicle_mass_mid_lap_kg = vehicle.effectiveDryMassKg() + trace.fuel_mid_lap_kg;
        trace.lap_breakdown = *lap_res.value;
        trace.tyre_breakdown = *step_res.value;
        trace.cumulative_elapsed_time_s = eval.cumulative_flying_time_s;
        trace.mandatory_check_after_lap = mand_check;
        eval.lap_traces.push_back(std::move(trace));

        ++age;
    }

    eval.final_health = h;
    const int stint_len = end_lap_completed - start_lap_completed;
    const bool within_min = (stint_len >= bundle.event_config.min_stint_length_laps);
    const bool within_max = (!bundle.event_config.max_stint_length_laps.has_value() ||
                             stint_len <= *bundle.event_config.max_stint_length_laps);
    eval.feasible = within_min && within_max &&
                    !eval.breached_mandatory_stop_threshold &&
                    !eval.breached_structural_floor_h_min;

    return EngineResult<StintRolloutEvaluation>::success(std::move(eval));
}

} // namespace simulation
} // namespace f1_sim
