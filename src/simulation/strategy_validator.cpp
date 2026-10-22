#include "f1_sim/simulation/strategy_validator.hpp"

#include <cmath>
#include <set>

namespace f1_sim {
namespace simulation {

CapabilityReadinessReport StrategyAndReadinessValidator::assessReadiness(
    const EventBundle* bundle,
    LoadStatus bundle_load_status,
    const std::vector<Diagnostic>& bundle_load_diagnostics,
    const VehicleConfig& vehicle,
    const EnvironmentScenarioConfig& env_config,
    const ModelParameters& params,
    const TyreInventoryManager& inventory,
    ModelStage stage,
    const data_access::DataAccessLayer* dal) {
    CapabilityReadinessReport report;
    report.evaluated_stage = stage;
    report.supported_schemas = {
        "TrackLayoutVersion (Schema 1A)",
        "TrackSurfaceProfile (Schema 1B)",
        "EventConfiguration (Schema 2)",
        "PhysicalCompoundCatalogue (Schema 4)",
        "EventRoleMapping (Schema 5)"
    };
    report.missing_repository_schemas = {
        "ScenarioEnvironmentSeries (Schema 3 - supplied via runtime EnvironmentScenarioConfig)",
        "VehicleSpecification (Schema 6 - supplied via runtime VehicleConfig with framework defaults)",
        "CalibratedModelParameters (Schema 7 - supplied via runtime ModelParameters / compound_overrides)"
    };

    report.diagnostics.insert(
        report.diagnostics.end(),
        bundle_load_diagnostics.begin(),
        bundle_load_diagnostics.end());

    if (bundle == nullptr || bundle_load_status != LoadStatus::Success) {
        report.bundle_load_valid = false;
        report.fidelity_status = FidelityStatus::InvalidOrBlocked;
        report.execution_permission = ExecutionPermission::BlockedInvalidBundle;
        return report;
    }
    report.bundle_load_valid = true;

    const std::string& event_id = bundle->event_config.event_id;
    bool input_valid = true;
    bool wet_blocked = false;
    bool missing_required_cal = false;
    bool uses_estimated_or_assumed = false;

    auto recordError = [&](DiagnosticCode code,
                           const std::string& field_path,
                           const std::string& msg,
                           const std::string& expected,
                           const std::string& actual) {
        Diagnostic d{Severity::Error, code, msg, event_id, "", field_path, expected, actual};
        report.diagnostics.push_back(d);
        if (dal != nullptr) {
            dal->reportEngineDiscrepancy(data_access::EngineDiscrepancyReport{
                Severity::Error, code, event_id, "", field_path, msg, expected, actual
            });
        }
    };

    // 1. Validate VehicleConfig inputs
    if (!std::isfinite(vehicle.driver_mass_kg) || vehicle.driver_mass_kg < 40.0 || vehicle.driver_mass_kg > 160.0) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/vehicle_config/driver_mass_kg",
            "Configured driver_mass_kg must be finite and within [40.0, 160.0] kg.",
            "[40.0, 160.0]",
            std::to_string(vehicle.driver_mass_kg));
    }
    if (!std::isfinite(vehicle.base_dry_mass_with_ref_driver_kg) ||
        vehicle.base_dry_mass_with_ref_driver_kg < 700.0 ||
        vehicle.base_dry_mass_with_ref_driver_kg > 950.0) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/vehicle_config/base_dry_mass_with_ref_driver_kg",
            "base_dry_mass_with_ref_driver_kg must be finite and within [700.0, 950.0] kg.",
            "[700.0, 950.0]",
            std::to_string(vehicle.base_dry_mass_with_ref_driver_kg));
    }
    if (!std::isfinite(vehicle.initial_fuel_load_kg) ||
        vehicle.initial_fuel_load_kg < 0.0 ||
        vehicle.initial_fuel_load_kg > 115.0) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/vehicle_config/initial_fuel_load_kg",
            "initial_fuel_load_kg must be finite and within [0.0, 115.0] kg.",
            "[0.0, 115.0]",
            std::to_string(vehicle.initial_fuel_load_kg));
    }
    if (vehicle.fuel_burn_rate_kg_per_lap.has_value() &&
        (!std::isfinite(*vehicle.fuel_burn_rate_kg_per_lap) || *vehicle.fuel_burn_rate_kg_per_lap < 0.0)) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/vehicle_config/fuel_burn_rate_kg_per_lap",
            "fuel_burn_rate_kg_per_lap must be finite and >= 0.0 kg/lap.",
            ">= 0.0",
            std::to_string(*vehicle.fuel_burn_rate_kg_per_lap));
    }
    if (vehicle.source_status == SourceStatus::Estimated ||
        vehicle.source_status == SourceStatus::Assumed) {
        uses_estimated_or_assumed = true;
    }

    // 2. Validate EnvironmentScenarioConfig (dry slick scope & domain guards)
    auto validateEnvEntry = [&](const LapEnvironment& env, const std::string& path_prefix) {
        if (!std::isfinite(env.air_temperature_c) || env.air_temperature_c < -10.0 || env.air_temperature_c > 60.0) {
            input_valid = false;
            recordError(
                DiagnosticCode::InvalidRangeOrEnum,
                path_prefix + "/air_temperature_c",
                "air_temperature_c must be finite and in [-10.0, 60.0] C.",
                "[-10.0, 60.0]",
                std::to_string(env.air_temperature_c));
        }
        if (env.track_temperature_c.has_value() &&
            (!std::isfinite(*env.track_temperature_c) || *env.track_temperature_c < -10.0 || *env.track_temperature_c > 80.0)) {
            input_valid = false;
            recordError(
                DiagnosticCode::InvalidRangeOrEnum,
                path_prefix + "/track_temperature_c",
                "track_temperature_c must be finite and in [-10.0, 80.0] C.",
                "[-10.0, 80.0]",
                std::to_string(*env.track_temperature_c));
        }
        if (!std::isfinite(env.relative_humidity_pct) || env.relative_humidity_pct < 0.0 || env.relative_humidity_pct > 100.0) {
            input_valid = false;
            recordError(
                DiagnosticCode::InvalidRangeOrEnum,
                path_prefix + "/relative_humidity_pct",
                "relative_humidity_pct must be finite and in [0.0, 100.0] %.",
                "[0.0, 100.0]",
                std::to_string(env.relative_humidity_pct));
        }
        if (!std::isfinite(env.surface_wetness) || env.surface_wetness < 0.0 || env.surface_wetness > 1.0) {
            input_valid = false;
            recordError(
                DiagnosticCode::InvalidRangeOrEnum,
                path_prefix + "/surface_wetness",
                "surface_wetness must be finite and in [0.0, 1.0].",
                "[0.0, 1.0]",
                std::to_string(env.surface_wetness));
        } else if (env.surface_wetness > 0.05 || env.precipitation_rate_mmph > 0.0) {
            wet_blocked = true;
            recordError(
                DiagnosticCode::InsufficientDataForCapability,
                path_prefix + "/surface_wetness",
                "Environment indicates wet/rain conditions (surface_wetness > 0.05 or precipitation > 0); initial engine scope supports dry slicks only (Stage M5 wet model is not implemented).",
                "surface_wetness <= 0.05 and precipitation_rate_mmph == 0.0",
                "wetness=" + std::to_string(env.surface_wetness) +
                    ", precip=" + std::to_string(env.precipitation_rate_mmph));
        }
        if (env.source_status == SourceStatus::Estimated ||
            env.source_status == SourceStatus::Assumed ||
            !env.track_temperature_c.has_value()) {
            uses_estimated_or_assumed = true;
        }
    };

    if (env_config.lap_series.empty()) {
        validateEnvEntry(env_config.constant_environment, "/environment_config/constant_environment");
    } else {
        for (std::size_t i = 0; i < env_config.lap_series.size(); ++i) {
            validateEnvEntry(
                env_config.lap_series[i],
                "/environment_config/lap_series/" + std::to_string(i));
        }
    }

    // 3. Validate ModelParameters (T_ref and P_ref)
    if (!params.ref_lap_time_s.has_value()) {
        missing_required_cal = true;
        report.parameter_gaps.push_back(SchemaAndParameterGap{
            "CalibratedModelParameters",
            "/model_parameters/ref_lap_time_s",
            SourceStatus::UnknownUnavailable,
            MissingPolicy::RejectSimulation,
            false,
            true,
            "Reference lap time T_ref(r, v) is required (MissingPolicy::RejectSimulation in Framework Section 4.7)."
        });
        recordError(
            DiagnosticCode::InsufficientDataForCapability,
            "/model_parameters/ref_lap_time_s",
            "Missing required parameter ref_lap_time_s (T_ref); simulation cannot substitute 0.0.",
            "finite double in [40.0, 180.0]",
            "null");
    } else if (!std::isfinite(*params.ref_lap_time_s) ||
               *params.ref_lap_time_s < 40.0 ||
               *params.ref_lap_time_s > 180.0) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/model_parameters/ref_lap_time_s",
            "ref_lap_time_s must be finite and within [40.0, 180.0] s.",
            "[40.0, 180.0]",
            std::to_string(*params.ref_lap_time_s));
    }

    const bool has_pit_total = params.pit_loss_total_s.has_value();
    const bool has_pit_components =
        params.main_straight_bypass_time_s.has_value() &&
        params.pit_accel_decel_loss_s.has_value();
    if (!has_pit_total && !has_pit_components) {
        missing_required_cal = true;
        report.parameter_gaps.push_back(SchemaAndParameterGap{
            "CalibratedModelParameters",
            "/model_parameters/pit_loss_total_s",
            SourceStatus::UnknownUnavailable,
            MissingPolicy::RejectSimulation,
            false,
            true,
            "Incremental pit-stop time loss P_ref(r) (or pit component parameters) is required (MissingPolicy::RejectSimulation)."
        });
        recordError(
            DiagnosticCode::InsufficientDataForCapability,
            "/model_parameters/pit_loss_total_s",
            "Missing required parameter pit_loss_total_s (P_ref); simulation cannot substitute 0.0.",
            "non-null pit_loss_total_s or pit component times",
            "null");
    } else if (has_pit_total &&
               (!std::isfinite(*params.pit_loss_total_s) || *params.pit_loss_total_s < 0.0 || *params.pit_loss_total_s > 120.0)) {
        input_valid = false;
        recordError(
            DiagnosticCode::InvalidRangeOrEnum,
            "/model_parameters/pit_loss_total_s",
            "pit_loss_total_s must be finite and within [0.0, 120.0] s.",
            "[0.0, 120.0]",
            std::to_string(*params.pit_loss_total_s));
    }

    if (params.parameter_source_status == SourceStatus::Estimated ||
        params.parameter_source_status == SourceStatus::Assumed) {
        uses_estimated_or_assumed = true;
    }

    // 4. Validate Nominated Physical Compounds (C1..C5) for this Event
    for (PhysicalCompoundId cid : bundle->event_config.nominated_compounds_set) {
        const ResolvedCompoundParams resolved =
            LapStintEvaluator::resolveCompoundParameters(*bundle, cid, params);
        const std::string comp_str = data_access::toString(cid);
        if (!resolved.has_required_baseline_calibration) {
            missing_required_cal = true;
            const PhysicalCompoundSpec* raw_spec = bundle->compound_catalogue.findCompound(cid);
            auto ov_it = params.compound_overrides.find(cid);

            auto checkCompField = [&](const char* fname, bool present) {
                if (!present) {
                    const std::string fpath = "/compounds/" + comp_str + "/" + fname;
                    report.parameter_gaps.push_back(SchemaAndParameterGap{
                        "PhysicalCompoundCatalogue",
                        fpath,
                        SourceStatus::UnknownUnavailable,
                        MissingPolicy::RejectSimulation,
                        false,
                        true,
                        "Nominated compound " + comp_str + " is missing required parameter " + fname + "."
                    });
                    recordError(
                        DiagnosticCode::InsufficientDataForCapability,
                        fpath,
                        "Nominated compound " + comp_str + " has uncalibrated (null) " + fname + ".",
                        "calibrated non-null double",
                        "null");
                }
            };

            const bool has_pace = (raw_spec && raw_spec->pace_offset_ref_s.has_value()) ||
                (ov_it != params.compound_overrides.end() && ov_it->second.pace_offset_ref_s.has_value());
            const bool has_wear = (raw_spec && raw_spec->base_wear_rate_per_lap.has_value()) ||
                (ov_it != params.compound_overrides.end() && ov_it->second.base_wear_rate_per_lap.has_value());
            const bool has_deg = (raw_spec && raw_spec->linear_deg_coeff_s.has_value()) ||
                (ov_it != params.compound_overrides.end() && ov_it->second.linear_deg_coeff_s.has_value());

            checkCompField("pace_offset_ref_s", has_pace);
            checkCompField("base_wear_rate_per_lap", has_wear);
            checkCompField("linear_deg_coeff_s", has_deg);
        } else if (resolved.source_status == SourceStatus::Estimated ||
                   resolved.source_status == SourceStatus::Assumed) {
            uses_estimated_or_assumed = true;
        }
    }

    // 5. Check inventory age-to-health derivation readiness
    for (const auto& s : inventory.sets()) {
        if (s.configured_age_laps > 0 &&
            s.health_status == TyreSetHealthStatus::MissingCalibrationForAgeDerivation) {
            missing_required_cal = true;
            recordError(
                DiagnosticCode::InsufficientDataForCapability,
                "/inventory/" + s.set_id + "/derived_starting_health",
                "Tyre set '" + s.set_id + "' has configured_age_laps > 0, but starting health cannot be derived without base_wear_rate_per_lap.",
                "derived starting health in [0, 1]",
                "nullopt");
        }
    }

    if (!params.allow_estimated_or_assumed_parameters && uses_estimated_or_assumed) {
        missing_required_cal = true;
        recordError(
            DiagnosticCode::InsufficientDataForCapability,
            "/model_parameters/allow_estimated_or_assumed_parameters",
            "Configuration contains ESTIMATED or ASSUMED parameters while allow_estimated_or_assumed_parameters is false.",
            "CALIBRATED parameters only",
            "ESTIMATED / ASSUMED");
    }

    report.user_input_valid = input_valid;
    report.mathematical_capability_sufficient = !missing_required_cal && !wet_blocked;

    if (!input_valid) {
        report.fidelity_status = FidelityStatus::InvalidOrBlocked;
        report.execution_permission = ExecutionPermission::BlockedInvalidInput;
    } else if (wet_blocked) {
        report.fidelity_status = FidelityStatus::InvalidOrBlocked;
        report.execution_permission = ExecutionPermission::BlockedUnsupportedWetConditions;
    } else if (missing_required_cal) {
        report.fidelity_status = FidelityStatus::UncalibratedIncomplete;
        report.execution_permission = ExecutionPermission::BlockedMissingCalibration;
    } else if (uses_estimated_or_assumed) {
        report.fidelity_status = FidelityStatus::EstimatedOrAssumedLowerFidelity;
        report.execution_permission = ExecutionPermission::AllowedLowerFidelity;
        report.diagnostics.push_back(Diagnostic{
            Severity::Warning,
            DiagnosticCode::AssumedOrLowConfidenceValue,
            "Simulation is ready to run in declared lower-fidelity mode using ESTIMATED/ASSUMED parameters.",
            event_id,
            "",
            "/model_parameters",
            "CALIBRATED",
            "ESTIMATED_OR_ASSUMED_LOWER_FIDELITY"
        });
    } else {
        report.fidelity_status = FidelityStatus::Calibrated;
        report.execution_permission = ExecutionPermission::AllowedCalibrated;
    }

    return report;
}

EngineResult<std::vector<std::string>> StrategyAndReadinessValidator::validateInitialStrategy(
    const StrategyPlan& strategy,
    const EventBundle& bundle,
    const TyreInventoryManager& inventory) {
    std::vector<Diagnostic> errors;
    const std::string& event_id = bundle.event_config.event_id;
    const int race_laps = bundle.event_config.race_distance_laps;

    if (strategy.stints.empty()) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Strategy plan must contain at least one stint.",
            event_id,
            "",
            "/strategy/stints",
            "non-empty stint list",
            "0 stints"
        });
        return EngineResult<std::vector<std::string>>::failure(
            EngineStatus::ValidationFailed, std::move(errors));
    }

    const int num_stops = static_cast<int>(strategy.stints.size()) - 1;
    if (num_stops < bundle.event_config.min_pit_stops ||
        num_stops > bundle.event_config.max_pit_stops) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Strategy pit-stop count (" + std::to_string(num_stops) +
                ") violates event pit-stop bounds [" +
                std::to_string(bundle.event_config.min_pit_stops) + ", " +
                std::to_string(bundle.event_config.max_pit_stops) + "].",
            event_id,
            "",
            "/strategy/stints",
            "[" + std::to_string(bundle.event_config.min_pit_stops) + ", " +
                std::to_string(bundle.event_config.max_pit_stops) + "] stops",
            std::to_string(num_stops) + " stops"
        });
    }

    int sum_laps = 0;
    std::set<PhysicalCompoundId> distinct_compounds;

    for (std::size_t j = 0; j < strategy.stints.size(); ++j) {
        const StintSpec& st = strategy.stints[j];
        const std::string ptr = "/strategy/stints/" + std::to_string(j);

        auto cid_opt = bundle.role_mapping.compoundForRole(st.role);
        if (!cid_opt.has_value()) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::CrossReferenceFailure,
                std::string("Stint role ") + data_access::toString(st.role) +
                    " does not resolve through EventRoleMapping.",
                event_id,
                "",
                ptr + "/role",
                "Mapped role in {SOFT, MEDIUM, HARD}",
                data_access::toString(st.role)
            });
        } else {
            distinct_compounds.insert(*cid_opt);
        }

        if (st.target_laps < bundle.event_config.min_stint_length_laps) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Stint target_laps (" + std::to_string(st.target_laps) +
                    ") is less than min_stint_length_laps (" +
                    std::to_string(bundle.event_config.min_stint_length_laps) + ").",
                event_id,
                "",
                ptr + "/target_laps",
                ">= " + std::to_string(bundle.event_config.min_stint_length_laps),
                std::to_string(st.target_laps)
            });
        }

        if (bundle.event_config.max_stint_length_laps.has_value() &&
            st.target_laps > *bundle.event_config.max_stint_length_laps) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Stint target_laps (" + std::to_string(st.target_laps) +
                    ") exceeds max_stint_length_laps (" +
                    std::to_string(*bundle.event_config.max_stint_length_laps) + ").",
                event_id,
                "",
                ptr + "/target_laps",
                "<= " + std::to_string(*bundle.event_config.max_stint_length_laps),
                std::to_string(st.target_laps)
            });
        }

        if (st.target_laps > 0 && st.target_laps <= 10000) {
            sum_laps += st.target_laps;
        }
    }

    if (sum_laps != race_laps) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Sum of target stint lengths (" + std::to_string(sum_laps) +
                ") must equal configured race_distance_laps (" + std::to_string(race_laps) + ").",
            event_id,
            "",
            "/strategy/stints",
            std::to_string(race_laps) + " laps",
            std::to_string(sum_laps) + " laps"
        });
    }

    if (static_cast<int>(distinct_compounds.size()) <
        bundle.event_config.min_distinct_dry_compounds) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Strategy uses only " + std::to_string(distinct_compounds.size()) +
                " distinct dry compound(s), violating min_distinct_dry_compounds (" +
                std::to_string(bundle.event_config.min_distinct_dry_compounds) + ").",
            event_id,
            "",
            "/strategy/stints",
            ">= " + std::to_string(bundle.event_config.min_distinct_dry_compounds) + " distinct compounds",
            std::to_string(distinct_compounds.size())
        });
    }

    auto alloc_res = inventory.allocateSetsForStints(strategy.stints);
    if (!alloc_res.ok()) {
        errors.insert(errors.end(), alloc_res.diagnostics.begin(), alloc_res.diagnostics.end());
    }

    if (!errors.empty()) {
        return EngineResult<std::vector<std::string>>::failure(
            EngineStatus::ValidationFailed, std::move(errors));
    }

    return EngineResult<std::vector<std::string>>::success(std::move(*alloc_res.value));
}

EngineResult<std::vector<std::string>> StrategyAndReadinessValidator::validateInteractiveDecision(
    const InteractiveDecision& decision,
    const PendingDecisionPrompt& prompt,
    const EventBundle& bundle,
    const TyreInventoryManager& inventory,
    const std::vector<StintRuntimeRecord>& current_stint_records,
    int current_stint_index,
    int completed_laps) {
    const std::string& event_id = bundle.event_config.event_id;
    const int race_laps = bundle.event_config.race_distance_laps;
    const int remaining_race_laps = race_laps - completed_laps;
    std::vector<Diagnostic> errors;

    // 1. Mandatory stop precedence rule: cannot override a mandatory stop with an extension
    if (prompt.mandatory_stop_required &&
        decision.action_type == DecisionActionType::ExtendCurrentStint) {
        return EngineResult<std::vector<std::string>>::failure(
            EngineStatus::MandatoryStopCannotBeOverridden,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "A mandatory tyre-health stop is active; the current stint cannot be extended.",
                event_id,
                "",
                "/decision/action_type",
                "KEEP_ORIGINAL_PLAN or PIT_NOW_WITH_REVISED_PLAN",
                "EXTEND_CURRENT_STINT"
            }});
    }

    // 2. Construct the candidate future stints (after current_stint_index)
    std::vector<StintSpec> candidate_future_stints;
    if (decision.revised_remaining_stints.has_value()) {
        candidate_future_stints = *decision.revised_remaining_stints;
    } else {
        for (std::size_t j = static_cast<std::size_t>(current_stint_index + 1);
             j < current_stint_records.size(); ++j) {
            StintSpec sp;
            sp.role = current_stint_records[j].role;
            sp.target_laps = current_stint_records[j].effective_target_laps;
            sp.requested_set_id = current_stint_records[j].assigned_set_id;
            candidate_future_stints.push_back(sp);
        }
    }

    int extension_laps = 0;
    if (decision.action_type == DecisionActionType::ExtendCurrentStint) {
        if (decision.extension_laps <= 0) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "When extending the current stint, extension_laps must be a positive integer (> 0).",
                event_id,
                "",
                "/decision/extension_laps",
                "> 0",
                std::to_string(decision.extension_laps)
            });
        }
        extension_laps = decision.extension_laps;

        if (bundle.event_config.max_stint_length_laps.has_value()) {
            const int new_stint_total =
                current_stint_records[static_cast<std::size_t>(current_stint_index)].actual_laps_completed +
                extension_laps;
            if (new_stint_total > *bundle.event_config.max_stint_length_laps) {
                errors.push_back(Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InvalidRangeOrEnum,
                    "Extended current stint length (" + std::to_string(new_stint_total) +
                        ") exceeds max_stint_length_laps (" +
                        std::to_string(*bundle.event_config.max_stint_length_laps) + ").",
                    event_id,
                    "",
                    "/decision/extension_laps",
                    "<= " + std::to_string(*bundle.event_config.max_stint_length_laps),
                    std::to_string(new_stint_total)
                });
            }
        }
    } else {
        // Pitting now at completed_laps requires at least 1 future stint to cover remaining_race_laps
        if (candidate_future_stints.empty()) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Pitting at lap " + std::to_string(completed_laps) +
                    " requires at least one remaining stint to cover the remaining " +
                    std::to_string(remaining_race_laps) + " race laps.",
                event_id,
                "",
                "/decision/revised_remaining_stints",
                ">= 1 remaining stint",
                "0 remaining stints"
            });
        }
    }

    // 3. Verify total stop count and stint bounds on future stints
    const int total_stints = (current_stint_index + 1) + static_cast<int>(candidate_future_stints.size());
    const int total_stops = total_stints - 1;
    if (total_stops < bundle.event_config.min_pit_stops ||
        total_stops > bundle.event_config.max_pit_stops) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Revised plan total pit-stop count (" + std::to_string(total_stops) +
                ") violates event pit-stop bounds [" +
                std::to_string(bundle.event_config.min_pit_stops) + ", " +
                std::to_string(bundle.event_config.max_pit_stops) + "].",
            event_id,
            "",
            "/decision/revised_remaining_stints",
            "[" + std::to_string(bundle.event_config.min_pit_stops) + ", " +
                std::to_string(bundle.event_config.max_pit_stops) + "] stops",
            std::to_string(total_stops) + " stops"
        });
    }

    std::set<PhysicalCompoundId> distinct_compounds;
    for (int j = 0; j <= current_stint_index; ++j) {
        distinct_compounds.insert(current_stint_records[static_cast<std::size_t>(j)].physical_compound);
    }

    int sum_future_laps = 0;
    for (std::size_t i = 0; i < candidate_future_stints.size(); ++i) {
        const StintSpec& st = candidate_future_stints[i];
        const std::string ptr = "/decision/revised_remaining_stints/" + std::to_string(i);
        auto cid_opt = bundle.role_mapping.compoundForRole(st.role);
        if (!cid_opt.has_value()) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::CrossReferenceFailure,
                "Remaining stint role does not resolve through EventRoleMapping.",
                event_id,
                "",
                ptr + "/role",
                "SOFT|MEDIUM|HARD",
                data_access::toString(st.role)
            });
        } else {
            distinct_compounds.insert(*cid_opt);
        }

        if (st.target_laps < bundle.event_config.min_stint_length_laps) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Remaining stint target_laps (" + std::to_string(st.target_laps) +
                    ") is less than min_stint_length_laps (" +
                    std::to_string(bundle.event_config.min_stint_length_laps) + ").",
                event_id,
                "",
                ptr + "/target_laps",
                ">= " + std::to_string(bundle.event_config.min_stint_length_laps),
                std::to_string(st.target_laps)
            });
        }
        if (bundle.event_config.max_stint_length_laps.has_value() &&
            st.target_laps > *bundle.event_config.max_stint_length_laps) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Remaining stint target_laps exceeds max_stint_length_laps.",
                event_id,
                "",
                ptr + "/target_laps",
                "<= " + std::to_string(*bundle.event_config.max_stint_length_laps),
                std::to_string(st.target_laps)
            });
        }
        if (st.target_laps > 0 && st.target_laps <= 10000) {
            sum_future_laps += st.target_laps;
        }
    }

    // 4. Verify exact remaining race distance coverage (no automatic subtraction of extension!)
    const int planned_remaining_total = extension_laps + sum_future_laps;
    if (planned_remaining_total != remaining_race_laps) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Remaining planned laps (extension " + std::to_string(extension_laps) +
                " + future stints " + std::to_string(sum_future_laps) + " = " +
                std::to_string(planned_remaining_total) +
                ") must equal remaining race distance (" + std::to_string(remaining_race_laps) +
                " laps). The engine does not automatically subtract an extension or cascade a shortfall in Interactive mode.",
            event_id,
            "",
            "/decision/revised_remaining_stints",
            std::to_string(remaining_race_laps) + " remaining laps",
            std::to_string(planned_remaining_total) + " laps"
        });
    }

    // 5. Verify compound diversity across completed + active + future stints
    if (static_cast<int>(distinct_compounds.size()) <
        bundle.event_config.min_distinct_dry_compounds) {
        errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::InvalidRangeOrEnum,
            "Revised race strategy uses only " + std::to_string(distinct_compounds.size()) +
                " distinct dry compound(s), violating min_distinct_dry_compounds (" +
                std::to_string(bundle.event_config.min_distinct_dry_compounds) + ").",
            event_id,
            "",
            "/decision/revised_remaining_stints",
            ">= " + std::to_string(bundle.event_config.min_distinct_dry_compounds) + " distinct compounds",
            std::to_string(distinct_compounds.size())
        });
    }

    // 6. Verify physical tyre set availability for all future stints without reusing consumed sets
    std::vector<std::string> already_used_sets;
    for (int j = 0; j <= current_stint_index; ++j) {
        already_used_sets.push_back(current_stint_records[static_cast<std::size_t>(j)].assigned_set_id);
    }

    auto alloc_res = inventory.allocateSetsForStints(
        candidate_future_stints, already_used_sets, current_stint_index + 1);
    if (!alloc_res.ok()) {
        errors.insert(errors.end(), alloc_res.diagnostics.begin(), alloc_res.diagnostics.end());
    }

    if (!errors.empty()) {
        return EngineResult<std::vector<std::string>>::failure(
            EngineStatus::ValidationFailed, std::move(errors));
    }

    return EngineResult<std::vector<std::string>>::success(std::move(*alloc_res.value));
}

} // namespace simulation
} // namespace f1_sim
