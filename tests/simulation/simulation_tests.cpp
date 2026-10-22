#define F1_SIM_CLI_LIBRARY_ONLY
#include "main.cpp"
#include "f1_sim/simulation/race_session.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace f1_sim::data_access;
using namespace f1_sim::simulation;

namespace {

int g_tests_run = 0;
int g_tests_failed = 0;

#define EXPECT_TRUE(cond, msg)                                                        \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << "  [FAIL] " << __FUNCTION__ << ":" << __LINE__ << " - "      \
                      << (msg) << " (" #cond ")\n";                                   \
            return false;                                                             \
        }                                                                             \
    } while (0)

#define EXPECT_EQ(actual, expected, msg)                                              \
    do {                                                                              \
        auto _a = (actual);                                                           \
        auto _e = (expected);                                                         \
        if (!(_a == _e)) {                                                            \
            std::cerr << "  [FAIL] " << __FUNCTION__ << ":" << __LINE__ << " - "      \
                      << (msg) << "\n";                                               \
            return false;                                                             \
        }                                                                             \
    } while (0)

#define EXPECT_NEAR(actual, expected, tol, msg)                                       \
    do {                                                                              \
        double _a = static_cast<double>(actual);                                      \
        double _e = static_cast<double>(expected);                                    \
        if (std::fabs(_a - _e) > (tol)) {                                             \
            std::cerr << "  [FAIL] " << __FUNCTION__ << ":" << __LINE__ << " - "      \
                      << (msg) << " (actual=" << _a << ", expected=" << _e << ")\n";  \
            return false;                                                             \
        }                                                                             \
    } while (0)

/**
 * @brief Construct the exact synthetic EventBundle and ModelParameters from
 *        Mathematical Framework Section 12.1 (Synthetic Worked Example 1: 10-lap race).
 *
 * Role Mapping: H -> C2, M -> C3, S -> C4
 * L = 10 laps, L_lap = 5000.0 m, T_ref = 90.0 s, P_ref = 20.0 s
 * C2: dT_comp = +0.600 s, d_0 = 0.020 /lap, alpha = 5.000 s
 * C3: dT_comp =  0.000 s, d_0 = 0.040 /lap, alpha = 5.000 s
 * C4: dT_comp = -0.600 s, d_0 = 0.080 /lap, alpha = 5.000 s
 */
EventBundle makeSyntheticExample1Bundle(int race_laps = 10) {
    EventBundle b;
    b.event_config.schema_name = "EventConfiguration";
    b.event_config.schema_version = "1.0.0";
    b.event_config.event_id = "synth_event_alpha";
    b.event_config.official_event_name = "Synthetic Verification Grand Prix";
    b.event_config.round_number = 1;
    b.event_config.season_year = 2026;
    b.event_config.track_id = "synth_circuit_alpha";
    b.event_config.layout_version = 1;
    b.event_config.surface_version = 1;
    b.event_config.race_distance_laps = race_laps;
    b.event_config.scheduled_race_distance_km = 5.0 * static_cast<double>(race_laps);
    b.event_config.nominated_compounds_set = {
        PhysicalCompoundId::C2, PhysicalCompoundId::C3, PhysicalCompoundId::C4};
    b.event_config.is_hypothetical_override = false;
    b.event_config.min_distinct_dry_compounds = 2;
    b.event_config.min_pit_stops = 1;
    b.event_config.max_pit_stops = 4;
    b.event_config.min_stint_length_laps = 1;
    b.event_config.max_stint_length_laps = std::nullopt;

    b.role_mapping.schema_name = "EventRoleMapping";
    b.role_mapping.schema_version = "1.0.0";
    b.role_mapping.event_id = "synth_event_alpha";
    b.role_mapping.season_year = 2026;
    b.role_mapping.spec_version = 1;
    b.role_mapping.nominated_compounds_set = {
        PhysicalCompoundId::C2, PhysicalCompoundId::C3, PhysicalCompoundId::C4};
    b.role_mapping.mappings = {
        {"synth_event_alpha", TyreRole::Hard, PhysicalCompoundId::C2, "WHITE", 2},
        {"synth_event_alpha", TyreRole::Medium, PhysicalCompoundId::C3, "YELLOW", 3},
        {"synth_event_alpha", TyreRole::Soft, PhysicalCompoundId::C4, "RED", 4}
    };

    b.track_layout.schema_name = "TrackLayoutVersion";
    b.track_layout.schema_version = "1.0.0";
    b.track_layout.track_id = "synth_circuit_alpha";
    b.track_layout.layout_version = 1;
    b.track_layout.track_name = "Synthetic Circuit Alpha";
    b.track_layout.lap_length_m = 5000.0;
    b.track_layout.pit_lane_length_m = 400.0;
    b.track_layout.pit_speed_limit_kmh = 80.0;
    b.track_layout.corners_total_count = 16;
    b.track_layout.corners_left_count = 7;
    b.track_layout.corners_right_count = 9;
    b.track_layout.lateral_energy_index_norm = 1.0;
    b.track_layout.braking_energy_index_norm = 1.0;
    b.track_layout.traction_energy_index_norm = 1.0;

    b.track_surface.schema_name = "TrackSurfaceProfile";
    b.track_surface.schema_version = "1.0.0";
    b.track_surface.track_id = "synth_circuit_alpha";
    b.track_surface.layout_version = 1;
    b.track_surface.surface_version = 1;
    b.track_surface.surface_abrasiveness_score = 3.0;
    b.track_surface.surface_grip_baseline_score = 3.0;
    b.track_surface.track_evolution_amplitude_s = 0.0;
    b.track_surface.track_evolution_decay_laps = 20.0;

    b.compound_catalogue.schema_name = "PhysicalCompoundCatalogue";
    b.compound_catalogue.schema_version = "1.0.0";
    b.compound_catalogue.season_year = 2026;
    b.compound_catalogue.spec_version = 1;
    b.compound_catalogue.calibration_status = "SYNTHETIC_CALIBRATED";

    auto makeComp = [](PhysicalCompoundId id, int rank, double pace, double wear, double alpha, double kappa_trk) {
        PhysicalCompoundSpec c;
        c.season_year = 2026;
        c.spec_version = 1;
        c.compound_id = id;
        c.hardness_rank = rank;
        c.pace_offset_ref_s = pace;
        c.base_wear_rate_per_lap = wear;
        c.linear_deg_coeff_s = alpha;
        c.cliff_wear_threshold = 1.0;
        c.cliff_amplitude_s = 0.0;
        c.cliff_exponent = 2.0;
        c.thermal_deg_sens_per_c = kappa_trk;
        return c;
    };

    b.compound_catalogue.compounds = {
        makeComp(PhysicalCompoundId::C1, 1, +1.000, 0.015, 5.000, 0.008),
        makeComp(PhysicalCompoundId::C2, 2, +0.600, 0.020, 5.000, 0.010),
        makeComp(PhysicalCompoundId::C3, 3,  0.000, 0.040, 5.000, 0.020),
        makeComp(PhysicalCompoundId::C4, 4, -0.600, 0.080, 5.000, 0.040),
        makeComp(PhysicalCompoundId::C5, 5, -1.100, 0.100, 5.000, 0.045)
    };

    return b;
}

ModelParameters makeSyntheticExample1Params() {
    ModelParameters p;
    p.param_set_id = "synth_example_1_params";
    p.parameter_source_status = SourceStatus::Calibrated;
    p.ref_lap_time_s = 90.000;
    p.pit_loss_total_s = 20.000;
    p.reference_stationary_time_s = 2.40;
    p.configured_stationary_time_s = 2.40;
    p.fuel_lap_sens_s_per_kg = 0.000;
    p.lap1_standing_start_loss_s = 0.000;
    p.outlap_warmup_loss_s = 0.000;
    p.grip_scaling_psi = 1.000;
    p.abrasiveness_sensitivity_omega = 0.000;
    return p;
}

VehicleConfig makeNeutralVehicleConfig() {
    VehicleConfig v;
    v.source_status = SourceStatus::Calibrated;
    v.driver_mass_kg = 75.0;
    v.initial_fuel_load_kg = 100.0;
    v.load_wear_sensitivity_kappa = 0.0;
    return v;
}

// ============================================================================
// TEST 1: Strategy Validation (distance, roles, diversity, bounds, inventory)
// ============================================================================
bool testStrategyValidation(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    RaceSession session(dal);

    EventBundle bundle = makeSyntheticExample1Bundle(10);
    EXPECT_TRUE(session.loadFromEventBundle(bundle).ok(), "Load synthetic bundle");
    EXPECT_TRUE(session.setModelParameters(makeSyntheticExample1Params()).ok(), "Set params");
    EXPECT_TRUE(session.setVehicleConfig(makeNeutralVehicleConfig()).ok(), "Set vehicle");

    StrategyPlan valid_plan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}};
    EXPECT_TRUE(session.setStrategyPlan(valid_plan).ok(), "Set valid strategy");
    auto v_ok = session.validate();
    EXPECT_TRUE(v_ok.ok(), "Expected valid strategy to pass validate()");
    EXPECT_EQ(session.state(), SessionState::Validated, "Expected SessionState::Validated");

    StrategyPlan short_plan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 5}}};
    session.setStrategyPlan(short_plan);
    EXPECT_TRUE(!session.validate().ok(), "Strategy not summing to L must fail");

    StrategyPlan zero_stint{{{TyreRole::Soft, 0}, {TyreRole::Medium, 10}}};
    session.setStrategyPlan(zero_stint);
    EXPECT_TRUE(!session.validate().ok(), "Zero-length stint must fail");

    StrategyPlan neg_stint{{{TyreRole::Soft, -2}, {TyreRole::Medium, 12}}};
    session.setStrategyPlan(neg_stint);
    EXPECT_TRUE(!session.validate().ok(), "Negative-length stint must fail");

    StrategyPlan single_comp{{{TyreRole::Soft, 4}, {TyreRole::Soft, 6}}};
    session.setStrategyPlan(single_comp);
    EXPECT_TRUE(!session.validate().ok(), "Single-compound strategy must fail diversity rule");

    StrategyPlan excess_med{
        {{TyreRole::Medium, 2}, {TyreRole::Medium, 2}, {TyreRole::Medium, 3}, {TyreRole::Soft, 3}}};
    session.setStrategyPlan(excess_med);
    EXPECT_TRUE(!session.validate().ok(), "Requesting 3 Medium sets when only 2 exist must fail");

    StrategyPlan excess_soft{
        {{TyreRole::Soft, 2}, {TyreRole::Soft, 2}, {TyreRole::Soft, 2}, {TyreRole::Soft, 2}, {TyreRole::Medium, 2}}};
    session.setStrategyPlan(excess_soft);
    EXPECT_TRUE(!session.validate().ok(), "Requesting 4 Soft sets when only 3 exist must fail");

    StrategyPlan dup_set_id{
        {{TyreRole::Soft, 3, std::string("S1")},
         {TyreRole::Medium, 4, std::string("M1")},
         {TyreRole::Soft, 3, std::string("S1")}}};
    session.setStrategyPlan(dup_set_id);
    EXPECT_TRUE(!session.validate().ok(), "Assigning S1 to two stints in one race must fail");

    for (const auto& s : session.tyreInventory()) {
        EXPECT_TRUE(!s.currently_fitted && !s.consumed_in_current_run,
                    "Rejected strategy validation must not contaminate tyre inventory state");
    }

    return true;
}

// ============================================================================
// TEST 2: Inventory 3S/2M/2H, Age-to-Health Derivation, Deterministic Set
//         Allocation, Track-Change Reset, and Post-Run Reset
// ============================================================================
bool testInventoryAgeHealthAndReset(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    RaceSession session(dal);

    EventBundle bundle = makeSyntheticExample1Bundle(10);
    session.loadFromEventBundle(bundle);
    session.setModelParameters(makeSyntheticExample1Params());
    session.setVehicleConfig(makeNeutralVehicleConfig());

    const auto& inv = session.tyreInventory();
    EXPECT_EQ(inv.size(), std::size_t(7), "Expected 7 sets in inventory");
    int count_s = 0, count_m = 0, count_h = 0;
    for (const auto& s : inv) {
        if (s.role == TyreRole::Soft) ++count_s;
        if (s.role == TyreRole::Medium) ++count_m;
        if (s.role == TyreRole::Hard) ++count_h;
        EXPECT_EQ(s.configured_age_laps, 0, "Default configured age must be 0");
        EXPECT_TRUE(s.derived_starting_health.has_value(), "Default starting health must be present");
        EXPECT_NEAR(s.derived_starting_health->meanHealth(), 1.0, 1e-12, "Default health must be 100%");
    }
    EXPECT_EQ(count_s, 3, "Expected 3 Soft sets");
    EXPECT_EQ(count_m, 2, "Expected 2 Medium sets");
    EXPECT_EQ(count_h, 2, "Expected 2 Hard sets");

    EXPECT_TRUE(!session.setConfiguredTyreAge("S1", -1).ok(), "Negative age must be rejected");
    EXPECT_TRUE(!session.setConfiguredTyreAge("S99", 2).ok(), "Unknown set ID must be rejected");

    auto s1_res = session.setConfiguredTyreAge("S1", 3);
    EXPECT_TRUE(s1_res.ok(), "Set S1 age to 3");
    EXPECT_NEAR(s1_res.value->derived_starting_health->meanHealth(), 0.76, 1e-12,
                "S1 derived starting health at age 3 must be 0.76 (76%)");

    auto s2_res = session.setConfiguredTyreAge("S2", 1);
    EXPECT_TRUE(s2_res.ok(), "Set S2 age to 1");
    EXPECT_NEAR(s2_res.value->derived_starting_health->meanHealth(), 0.92, 1e-12,
                "S2 derived starting health at age 1 must be 0.92 (92%)");

    StrategyPlan plan{{{TyreRole::Soft, 3}, {TyreRole::Medium, 4}, {TyreRole::Soft, 3}}};
    session.setStrategyPlan(plan);
    EXPECT_TRUE(session.start().ok(), "Start session");

    EXPECT_EQ(session.setConfiguredTyreAge("S1", 0).status,
              EngineStatus::ConfigurationLockedWhileRunning,
              "Age editing must be rejected while RUNNING");
    EXPECT_TRUE(session.pause().ok(), "Pause session");
    EXPECT_EQ(session.setConfiguredTyreAge("S1", 0).status,
              EngineStatus::ConfigurationLockedWhileRunning,
              "Age editing must be rejected while PAUSED");
    EXPECT_TRUE(session.resume().ok(), "Resume session");

    auto finish_res = session.runUntilStopOrCompletion();
    EXPECT_TRUE(finish_res.ok(), "Run to completion");
    EXPECT_EQ(session.state(), SessionState::Completed, "Expected COMPLETED state");

    const RaceResult& result1 = *session.lastResult();
    EXPECT_EQ(result1.stints[0].assigned_set_id, std::string("S3"), "Stint 0 must get S3 (100% health)");
    EXPECT_EQ(result1.stints[1].assigned_set_id, std::string("M1"), "Stint 1 must get M1 (tie-break M1 < M2)");
    EXPECT_EQ(result1.stints[2].assigned_set_id, std::string("S2"), "Stint 2 must get S2 (92% > 76%)");
    EXPECT_EQ(result1.stints[2].set_configured_age_at_fit_laps, 1, "S2 starts stint at configured age 1");
    EXPECT_NEAR(result1.stints[2].set_starting_health_at_fit.meanHealth(), 0.92, 1e-12,
                "S2 starts stint at derived starting health 0.92");

    for (const auto& s : session.tyreInventory()) {
        EXPECT_TRUE(!s.currently_fitted && !s.consumed_in_current_run,
                    "Post-run inventory must reset fitted/consumed flags");
        EXPECT_EQ(s.runtime_age_laps, s.configured_age_laps,
                  "Post-run runtime_age_laps must reset to configured_age_laps");
        EXPECT_NEAR(s.runtime_health.meanHealth(), s.derived_starting_health->meanHealth(), 1e-12,
                    "Post-run runtime_health must reset to derived_starting_health");
    }

    EXPECT_TRUE(session.start().ok(), "Start second run with preserved configuration");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete second run");
    const RaceResult& result2 = *session.lastResult();
    EXPECT_NEAR(result1.total_race_time_s, result2.total_race_time_s, 1e-12,
                "Consecutive runs with preserved configuration must produce bitwise-identical race time");

    EventBundle other_event = makeSyntheticExample1Bundle(12);
    other_event.event_config.event_id = "synth_event_beta";
    other_event.event_config.track_id = "synth_circuit_beta";
    EXPECT_TRUE(session.loadFromEventBundle(other_event).ok(), "Switch to new event");
    for (const auto& s : session.tyreInventory()) {
        EXPECT_EQ(s.configured_age_laps, 0, "Event switch must reset configured_age_laps to 0");
        EXPECT_NEAR(s.derived_starting_health->meanHealth(), 1.0, 1e-12,
                    "Event switch must reset derived starting health to 100%");
    }

    return true;
}

// ============================================================================
// TEST 3: Strict Health Thresholds (< 30% Aggregate, < 12% Individual),
//         End-of-Lap Evaluation Timing, and Post-Finish No-Stop Rule
// ============================================================================
bool testStrictHealthThresholdsAndStopTiming(const fs::path& real_data_root) {
    auto chk_exact_30 = LapStintEvaluator::evaluateMandatoryStopThresholds(
        CornerHealthVector::uniform(0.30));
    EXPECT_TRUE(!chk_exact_30.aggregate_breached, "Mean health == 0.30 must NOT breach aggregate threshold");
    EXPECT_TRUE(!chk_exact_30.individual_breached, "Health == 0.30 must NOT breach individual threshold");
    EXPECT_TRUE(!chk_exact_30.mandatory_stop_required, "Mean health == 0.30 must NOT trigger mandatory stop");

    auto chk_below_30 = LapStintEvaluator::evaluateMandatoryStopThresholds(
        CornerHealthVector::uniform(0.299999));
    EXPECT_TRUE(chk_below_30.aggregate_breached, "Mean health < 0.30 must breach aggregate threshold");
    EXPECT_TRUE(!chk_below_30.individual_breached, "0.299999 >= 0.12 so individual not breached");
    EXPECT_TRUE(chk_below_30.mandatory_stop_required, "Mean health < 0.30 must trigger mandatory stop");

    auto chk_exact_12 = LapStintEvaluator::evaluateMandatoryStopThresholds(
        CornerHealthVector{{0.12, 0.40, 0.40, 0.40}});
    EXPECT_TRUE(!chk_exact_12.aggregate_breached, "Mean 0.33 >= 0.30");
    EXPECT_TRUE(!chk_exact_12.individual_breached, "Min corner == 0.12 must NOT breach individual threshold");
    EXPECT_TRUE(!chk_exact_12.mandatory_stop_required, "Exact 0.12 must NOT trigger mandatory stop");

    auto chk_below_12 = LapStintEvaluator::evaluateMandatoryStopThresholds(
        CornerHealthVector{{0.40, 0.119999, 0.40, 0.40}});
    EXPECT_TRUE(!chk_below_12.aggregate_breached, "Mean 0.32999975 >= 0.30");
    EXPECT_TRUE(chk_below_12.individual_breached, "Corner < 0.12 must breach individual threshold");
    EXPECT_TRUE(chk_below_12.mandatory_stop_required, "Corner < 0.12 must trigger mandatory stop");
    EXPECT_EQ(chk_below_12.limiting_corner_index, std::size_t(1), "FR (index 1) is the limiting corner");

    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    RaceSession session(dal);
    EventBundle bundle = makeSyntheticExample1Bundle(6);
    ModelParameters params = makeSyntheticExample1Params();
    CompoundParameterOverride soft_ov;
    soft_ov.compound_id = PhysicalCompoundId::C4;
    soft_ov.pace_offset_ref_s = -0.600;
    soft_ov.base_wear_rate_per_lap = 0.35;
    soft_ov.linear_deg_coeff_s = 5.000;
    params.compound_overrides[PhysicalCompoundId::C4] = soft_ov;

    session.loadFromEventBundle(bundle);
    session.setModelParameters(params);
    session.setVehicleConfig(makeNeutralVehicleConfig());
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 2}}});
    EXPECT_TRUE(session.start().ok(), "Start exact-30% boundary test");

    auto lap1 = session.advanceLap();
    EXPECT_TRUE(lap1.ok(), "Advance lap 1");
    EXPECT_NEAR(lap1.value->health_end.meanHealth(), 0.65, 1e-12, "End of lap 1 health = 0.65");
    EXPECT_TRUE(!lap1.value->pit_stop_occurred_after_lap, "No pit stop after lap 1");

    auto lap2 = session.advanceLap();
    EXPECT_TRUE(lap2.ok(), "Advance lap 2");
    EXPECT_NEAR(lap2.value->health_end.meanHealth(), 0.30, 1e-12, "End of lap 2 health = 0.30 (exact 30%)");
    EXPECT_TRUE(!lap2.value->mandatory_check_after_lap.mandatory_stop_required,
                "Exact 30% at end of lap 2 must NOT trigger mandatory stop");
    EXPECT_TRUE(!lap2.value->pit_stop_occurred_after_lap, "No pit stop after lap 2");

    auto lap3 = session.advanceLap();
    EXPECT_TRUE(lap3.ok(), "Advance lap 3");
    EXPECT_TRUE(lap3.value->mandatory_check_after_lap.mandatory_stop_required,
                "Below 30% at end of lap 3 MUST trigger mandatory stop");
    EXPECT_TRUE(lap3.value->pit_stop_occurred_after_lap,
                "Mandatory stop must occur after lap 3 is completed and before lap 4");

    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Finish 6-lap race");
    EXPECT_EQ(session.lastResult()->total_pit_stops, 1, "Only 1 pit stop in race");

    CompoundParameterOverride med_ov;
    med_ov.compound_id = PhysicalCompoundId::C3;
    med_ov.pace_offset_ref_s = 0.0;
    med_ov.base_wear_rate_per_lap = 0.25;
    med_ov.linear_deg_coeff_s = 5.0;
    params.compound_overrides[PhysicalCompoundId::C3] = med_ov;
    session.setModelParameters(params);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 3}, {TyreRole::Medium, 3}}});
    EXPECT_TRUE(session.start().ok(), "Start final-lap threshold test");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete final-lap threshold test");

    const RaceResult& final_res = *session.lastResult();
    EXPECT_EQ(final_res.completed_race_laps, 6, "Completed all 6 laps");
    EXPECT_NEAR(final_res.lap_trace.back().health_end.meanHealth(), 0.25, 1e-12,
                "Final lap ended at 25% health (< 30%)");
    EXPECT_TRUE(!final_res.lap_trace.back().pit_stop_occurred_after_lap,
                "Threshold crossed after final race lap L must NOT add a post-finish pit stop");
    EXPECT_EQ(final_res.total_pit_stops, 1, "Total pit stops must remain 1 (no post-finish stop)");

    return true;
}

// ============================================================================
// TEST 4: Preplanned Mode Shortfall Cascade (10, 20, 30 Example & Coinciding Stop)
// ============================================================================
bool testPreplannedModeAndShortfallCascade(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    RaceSession session(dal);

    EventBundle bundle = makeSyntheticExample1Bundle(60);
    ModelParameters params = makeSyntheticExample1Params();

    CompoundParameterOverride c4_ov;
    c4_ov.compound_id = PhysicalCompoundId::C4; // Soft
    c4_ov.pace_offset_ref_s = -0.600;
    c4_ov.base_wear_rate_per_lap = 0.090;       // 8 laps -> 0.28 (< 0.30 on lap 8)
    c4_ov.linear_deg_coeff_s = 5.000;
    params.compound_overrides[PhysicalCompoundId::C4] = c4_ov;

    CompoundParameterOverride c3_ov;
    c3_ov.compound_id = PhysicalCompoundId::C3; // Medium
    c3_ov.pace_offset_ref_s = 0.000;
    c3_ov.base_wear_rate_per_lap = 0.042;       // 17 laps -> 0.286 (< 0.30 on stint lap 17)
    c3_ov.linear_deg_coeff_s = 5.000;
    params.compound_overrides[PhysicalCompoundId::C3] = c3_ov;

    CompoundParameterOverride c2_ov;
    c2_ov.compound_id = PhysicalCompoundId::C2; // Hard
    c2_ov.pace_offset_ref_s = +0.600;
    c2_ov.base_wear_rate_per_lap = 0.012;       // 52 laps -> 0.376 (>= 0.30, completes up to 52 laps)
    c2_ov.linear_deg_coeff_s = 5.000;
    params.compound_overrides[PhysicalCompoundId::C2] = c2_ov;

    session.loadFromEventBundle(bundle);
    session.setRaceMode(RaceMode::Preplanned);
    session.setModelParameters(params);
    session.setVehicleConfig(makeNeutralVehicleConfig());
    session.setStrategyPlan(StrategyPlan{
        {{TyreRole::Soft, 10}, {TyreRole::Medium, 20}, {TyreRole::Hard, 30}}});

    EXPECT_TRUE(session.start().ok(), "Start 60-lap preplanned cascade test");

    InteractiveDecision illegal_dec;
    EXPECT_TRUE(!session.applyDecision(illegal_dec).ok(),
                "applyDecision must be rejected in Preplanned mode");

    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Run 60-lap preplanned cascade to finish");
    EXPECT_EQ(session.state(), SessionState::Completed, "Race must complete at lap 60");

    const RaceResult& res = *session.lastResult();
    EXPECT_EQ(res.completed_race_laps, 60, "Must finish exactly at lap 60");
    EXPECT_EQ(res.stints.size(), std::size_t(3), "3 stints");

    EXPECT_EQ(res.stints[0].original_target_laps, 10, "Stint 1 original target = 10");
    EXPECT_EQ(res.stints[0].actual_laps_completed, 8, "Stint 1 actual laps = 8");
    EXPECT_EQ(res.stints[0].shortfall_laps, 2, "Stint 1 shortfall = 2");
    EXPECT_TRUE(res.stints[0].ended_by_mandatory_stop, "Stint 1 ended by mandatory stop");

    EXPECT_EQ(res.stints[1].original_target_laps, 20, "Stint 2 original target = 20");
    EXPECT_EQ(res.stints[1].effective_target_laps, 22, "Stint 2 cascaded target = 22");
    EXPECT_EQ(res.stints[1].actual_laps_completed, 17, "Stint 2 actual laps = 17");
    EXPECT_EQ(res.stints[1].shortfall_laps, 5, "Stint 2 shortfall against 22 = 5");
    EXPECT_TRUE(res.stints[1].ended_by_mandatory_stop, "Stint 2 ended by mandatory stop");

    EXPECT_EQ(res.stints[2].original_target_laps, 30, "Stint 3 original target = 30");
    EXPECT_EQ(res.stints[2].accumulated_shortfall_into_stint, 7, "Accumulated shortfall into Stint 3 = 2 + 5 = 7");
    EXPECT_EQ(res.stints[2].uncapped_cascaded_target_laps, 37, "Stint 3 uncapped cascaded target = 37");
    EXPECT_EQ(res.stints[2].effective_target_laps, 35, "Stint 3 effective target capped to remaining 35 laps");
    EXPECT_EQ(res.stints[2].actual_laps_completed, 35, "Stint 3 actual laps = 35 (8 + 17 + 35 = 60)");
    EXPECT_EQ(res.interventions.size(), std::size_t(2), "2 cascade records logged");

    session.setStrategyPlan(StrategyPlan{
        {{TyreRole::Soft, 8}, {TyreRole::Hard, 52}}});
    EXPECT_TRUE(session.start().ok(), "Start coinciding boundary test");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Finish coinciding boundary test");
    const RaceResult& res_coincide = *session.lastResult();
    EXPECT_EQ(res_coincide.total_pit_stops, 1, "Coinciding planned and mandatory stop must be 1 stop, not 2");
    EXPECT_EQ(res_coincide.pit_stops[0].completed_race_lap, 8, "Stop at lap 8");
    EXPECT_EQ(res_coincide.pit_stops[0].reason, PitStopReason::MandatoryAndPlannedBoundary,
              "Reason must be MandatoryAndPlannedBoundary");
    EXPECT_TRUE(res_coincide.pit_stops[0].coincided_with_planned_boundary,
                "coincided_with_planned_boundary flag must be true");

    return true;
}

// ============================================================================
// TEST 5: Interactive Mode Decision Points, Additive Extensions, No Auto-Subtraction,
//         Mandatory-Stop Precedence, and No Silent Cascade After Forced Stop
// ============================================================================
bool testInteractiveModeDecisionsAndMandatoryPrecedence(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    RaceSession session(dal);

    EventBundle bundle = makeSyntheticExample1Bundle(10);
    session.loadFromEventBundle(bundle);
    session.setRaceMode(RaceMode::Interactive);
    session.setModelParameters(makeSyntheticExample1Params());
    session.setVehicleConfig(makeNeutralVehicleConfig());

    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    EXPECT_TRUE(session.start().ok(), "Start Interactive race");

    auto step1 = session.runUntilStopOrCompletion();
    EXPECT_TRUE(step1.ok(), "Run to first decision point");
    EXPECT_EQ(step1.status, EngineStatus::DecisionRequired, "Expected DecisionRequired at lap 4");
    EXPECT_EQ(session.state(), SessionState::DecisionRequired, "State must be DecisionRequired");
    EXPECT_TRUE(session.pendingDecision().has_value(), "PendingDecisionPrompt must be populated");
    EXPECT_EQ(session.pendingDecision()->completed_race_lap, 4, "Decision at lap 4");
    EXPECT_EQ(session.pendingDecision()->remaining_race_laps, 6, "6 laps remaining");
    EXPECT_EQ(session.pendingDecision()->reason, DecisionReason::PlannedStintBoundary, "Planned boundary");
    EXPECT_TRUE(!session.pendingDecision()->mandatory_stop_required, "Not a mandatory stop");

    EXPECT_TRUE(!session.resume().ok(), "Cannot resume() without applying a decision first");

    InteractiveDecision unadjusted_ext;
    unadjusted_ext.action_type = DecisionActionType::ExtendCurrentStint;
    unadjusted_ext.extension_laps = 2;
    auto bad_ext = session.applyDecision(unadjusted_ext);
    EXPECT_TRUE(!bad_ext.ok(),
                "Extending current stint without adjusting remaining stints must be rejected");
    EXPECT_EQ(session.state(), SessionState::DecisionRequired,
              "Session must remain at DecisionRequired after rejected decision");

    InteractiveDecision valid_ext;
    valid_ext.action_type = DecisionActionType::ExtendCurrentStint;
    valid_ext.extension_laps = 2;
    valid_ext.revised_remaining_stints = std::vector<StintSpec>{{TyreRole::Hard, 4}};
    valid_ext.user_note = "Extend Soft by +2 laps and switch final stint to Hard (4 laps)";

    auto ext_res = session.applyDecision(valid_ext);
    EXPECT_TRUE(ext_res.ok(), "Valid extension + remaining stint reallocation must succeed");
    EXPECT_EQ(session.state(), SessionState::Paused,
              "applyDecision must transition to Paused awaiting explicit resume()");
    EXPECT_EQ(session.snapshot().stint_records[0].effective_target_laps, 6,
              "Extended Stint 1 target must be 4 + 2 = 6 laps");
    EXPECT_EQ(session.snapshot().completed_laps, 4,
              "applyDecision must not silently advance laps");

    EXPECT_TRUE(session.resume().ok(), "Resume after extension decision");
    auto step2 = session.runUntilStopOrCompletion();
    EXPECT_TRUE(step2.ok(), "Run to revised boundary at lap 6");
    EXPECT_EQ(step2.status, EngineStatus::DecisionRequired, "DecisionRequired at lap 6");
    EXPECT_EQ(session.pendingDecision()->completed_race_lap, 6, "Paused at lap 6");
    EXPECT_EQ(session.pendingDecision()->remaining_race_laps, 4, "4 laps remaining");

    InteractiveDecision keep_dec;
    keep_dec.action_type = DecisionActionType::KeepOriginalPlan;
    EXPECT_TRUE(session.applyDecision(keep_dec).ok(), "Apply KeepOriginalPlan at lap 6");
    EXPECT_TRUE(session.resume().ok(), "Resume for final stint");
    auto step3 = session.runUntilStopOrCompletion();
    EXPECT_TRUE(step3.ok(), "Run to finish at lap 10");
    EXPECT_EQ(session.state(), SessionState::Completed, "Completed Interactive race");

    const RaceResult& res = *session.lastResult();
    EXPECT_EQ(res.stints[0].actual_laps_completed, 6, "Stint 1 completed 6 laps");
    EXPECT_EQ(res.stints[1].actual_laps_completed, 4, "Stint 2 completed 4 laps");
    EXPECT_EQ(res.stints[1].role, TyreRole::Hard, "Stint 2 used Hard role");
    EXPECT_EQ(res.interventions.size(), std::size_t(2), "2 interactive interventions logged");

    ModelParameters high_wear_params = makeSyntheticExample1Params();
    CompoundParameterOverride c4_fast;
    c4_fast.compound_id = PhysicalCompoundId::C4;
    c4_fast.pace_offset_ref_s = -0.600;
    c4_fast.base_wear_rate_per_lap = 0.250;
    c4_fast.linear_deg_coeff_s = 5.000;
    high_wear_params.compound_overrides[PhysicalCompoundId::C4] = c4_fast;

    session.setModelParameters(high_wear_params);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 5}, {TyreRole::Medium, 5}}});
    EXPECT_TRUE(session.start().ok(), "Start Interactive mandatory-stop test");

    auto mand_pause = session.runUntilStopOrCompletion();
    EXPECT_TRUE(mand_pause.ok(), "Run until mandatory stop at lap 3");
    EXPECT_EQ(mand_pause.status, EngineStatus::DecisionRequired, "DecisionRequired at lap 3");
    EXPECT_EQ(session.pendingDecision()->completed_race_lap, 3, "Stopped at lap 3");
    EXPECT_EQ(session.pendingDecision()->remaining_race_laps, 7, "7 laps remaining");
    EXPECT_TRUE(session.pendingDecision()->mandatory_stop_required, "mandatory_stop_required must be true");
    EXPECT_TRUE(!session.pendingDecision()->extension_permitted, "extension_permitted must be false");

    EXPECT_EQ(session.pendingDecision()->current_stint_plan[1].effective_target_laps, 5,
              "Interactive mode must NOT silently cascade the 2-lap shortfall onto Stint 2");

    InteractiveDecision try_override;
    try_override.action_type = DecisionActionType::ExtendCurrentStint;
    try_override.extension_laps = 2;
    try_override.revised_remaining_stints = std::vector<StintSpec>{{TyreRole::Medium, 5}};
    auto override_res = session.applyDecision(try_override);
    EXPECT_TRUE(!override_res.ok(), "Mandatory stop cannot be overridden by ExtendCurrentStint");
    EXPECT_EQ(override_res.status, EngineStatus::MandatoryStopCannotBeOverridden,
              "Expected MandatoryStopCannotBeOverridden status");

    InteractiveDecision try_unreplanned;
    try_unreplanned.action_type = DecisionActionType::KeepOriginalPlan;
    EXPECT_TRUE(!session.applyDecision(try_unreplanned).ok(),
                "Un-replanned 5-lap stint cannot cover 7 remaining laps");

    InteractiveDecision replan_dec;
    replan_dec.action_type = DecisionActionType::PitNowWithRevisedPlan;
    replan_dec.revised_remaining_stints = std::vector<StintSpec>{{TyreRole::Medium, 7}};
    EXPECT_TRUE(session.applyDecision(replan_dec).ok(), "Re-planning remaining 7 laps must succeed");
    EXPECT_TRUE(session.resume().ok(), "Resume after mandatory stop re-plan");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete race after mandatory stop");
    EXPECT_EQ(session.state(), SessionState::Completed, "Completed race");
    EXPECT_EQ(session.lastResult()->mandatory_health_stops, 1, "Recorded 1 mandatory health stop");

    return true;
}

// ============================================================================
// TEST 6: Pause/Resume, Snapshot/Restore Determinism, and Cancellation Cleanup
// ============================================================================
bool testLifecyclePauseResumeAndSnapshotDeterminism(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);

    EventBundle bundle = makeSyntheticExample1Bundle(10);
    ModelParameters params = makeSyntheticExample1Params();
    params.fuel_lap_sens_s_per_kg = 0.032;
    params.lap1_standing_start_loss_s = 3.50;
    params.outlap_warmup_loss_s = 0.50;

    RaceSession uninterrupted(dal);
    uninterrupted.loadFromEventBundle(bundle);
    uninterrupted.setModelParameters(params);
    uninterrupted.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    EXPECT_TRUE(uninterrupted.start().ok(), "Start uninterrupted session");
    EXPECT_TRUE(uninterrupted.runUntilStopOrCompletion().ok(), "Complete uninterrupted session");
    const RaceResult baseline_res = *uninterrupted.lastResult();

    RaceSession paused_session(dal);
    paused_session.loadFromEventBundle(bundle);
    paused_session.setModelParameters(params);
    paused_session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    EXPECT_TRUE(paused_session.start().ok(), "Start paused session");

    EXPECT_TRUE(paused_session.advanceLap().ok(), "Lap 1");
    EXPECT_TRUE(paused_session.advanceLap().ok(), "Lap 2");
    EXPECT_TRUE(paused_session.pause().ok(), "Pause after lap 2");
    EXPECT_EQ(paused_session.state(), SessionState::Paused, "State is Paused");

    EXPECT_TRUE(!paused_session.pause().ok(), "Duplicate pause() must be rejected");
    EXPECT_TRUE(!paused_session.advanceLap().ok(), "advanceLap() while Paused must be rejected");

    const SessionSnapshot snap1 = paused_session.snapshot();
    const SessionSnapshot snap2 = paused_session.snapshot();
    EXPECT_EQ(snap1.completed_laps, snap2.completed_laps, "Snapshots identical");
    EXPECT_NEAR(snap1.elapsed_race_time_s, snap2.elapsed_race_time_s, 1e-15, "Snapshot elapsed time identical");

    RaceSession restored_session(dal);
    restored_session.loadFromEventBundle(bundle);
    EXPECT_TRUE(restored_session.restoreSnapshot(snap1).ok(), "Restore snapshot at lap 2");
    EXPECT_EQ(restored_session.state(), SessionState::Paused, "Restored session is Paused");
    EXPECT_TRUE(restored_session.resume().ok(), "Resume restored session");
    EXPECT_TRUE(!restored_session.resume().ok(), "Duplicate resume() while Running must be rejected");

    EXPECT_TRUE(restored_session.advanceLap().ok(), "Lap 3");
    EXPECT_TRUE(restored_session.advanceLap().ok(), "Lap 4 (with pit stop)");
    EXPECT_TRUE(restored_session.pause().ok(), "Pause after lap 4 pit stop");
    EXPECT_TRUE(restored_session.resume().ok(), "Resume after lap 4 pause");
    EXPECT_TRUE(restored_session.runUntilStopOrCompletion().ok(), "Run restored session to completion");

    const RaceResult restored_res = *restored_session.lastResult();
    EXPECT_NEAR(baseline_res.total_race_time_s, restored_res.total_race_time_s, 1e-12,
                "Pause/snapshot/restore/resume must yield identical total race time");
    EXPECT_NEAR(baseline_res.total_flying_lap_time_s, restored_res.total_flying_lap_time_s, 1e-12,
                "Flying lap time sum must match identically");
    EXPECT_NEAR(baseline_res.total_pit_stop_time_loss_s, restored_res.total_pit_stop_time_loss_s, 1e-12,
                "Pit stop loss sum must match identically");
    EXPECT_EQ(baseline_res.lap_trace.size(), restored_res.lap_trace.size(),
              "Lap trace count must match without duplicating laps");

    EXPECT_TRUE(!restored_session.resume().ok(), "Completed session must reject resume()");
    EXPECT_TRUE(!restored_session.advanceLap().ok(), "Completed session must reject advanceLap()");

    RaceSession cancel_session(dal);
    cancel_session.loadFromEventBundle(bundle);
    cancel_session.setModelParameters(params);
    cancel_session.setConfiguredTyreAge("S1", 2);
    cancel_session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    EXPECT_TRUE(cancel_session.start().ok(), "Start session to cancel");
    EXPECT_TRUE(cancel_session.advanceLap().ok(), "Advance 1 lap before cancelling");

    auto cancel_res = cancel_session.cancel();
    EXPECT_TRUE(cancel_res.ok(), "Cancel active session");
    EXPECT_EQ(cancel_session.state(), SessionState::Cancelled, "State is Cancelled");
    EXPECT_EQ(cancel_res.value->completed_race_laps, 1, "Partial trace of 1 lap preserved");

    const TyreSetState* s1 = nullptr;
    for (const auto& s : cancel_session.tyreInventory()) {
        EXPECT_TRUE(!s.currently_fitted && !s.consumed_in_current_run,
                    "Cancelled session must reset fitted/consumed flags");
        if (s.set_id == "S1") s1 = &s;
    }
    EXPECT_TRUE(s1 != nullptr && s1->configured_age_laps == 2,
                "Configured age on S1 must be preserved after cancel()");

    return true;
}

// ============================================================================
// TEST 7: DAL Integration, Uncalibrated Bundle Blocking, Wet-Condition Guard,
//         and Fidelity Status Classification
// ============================================================================
bool testDALIntegrationAndCapabilityGating(const fs::path& real_data_root) {
    auto sink = std::make_shared<InMemoryDiagnosticCollector>();
    auto dal = std::make_shared<DataAccessLayer>(real_data_root, sink);
    RaceSession session(dal);

    auto readiness_res = session.selectEvent("2026_british_gp");
    EXPECT_TRUE(readiness_res.ok(), "selectEvent(2026_british_gp) must load bundle cleanly via DAL");

    const CapabilityReadinessReport& rep = *readiness_res.value;
    EXPECT_TRUE(rep.bundle_load_valid, "DAL bundle load must be valid");
    EXPECT_TRUE(!rep.mathematical_capability_sufficient,
                "Raw 2026_british_gp has uncalibrated (null) compound fields and no T_ref/P_ref yet");
    EXPECT_EQ(rep.fidelity_status, FidelityStatus::UncalibratedIncomplete,
              "Expected UncalibratedIncomplete fidelity status");
    EXPECT_EQ(rep.execution_permission, ExecutionPermission::BlockedMissingCalibration,
              "Expected BlockedMissingCalibration permission");
    EXPECT_TRUE(!rep.canExecute(), "Cannot execute uncalibrated shell without parameters");
    EXPECT_TRUE(rep.parameter_gaps.size() >= 10,
                "Expected field-by-field gap report for T_ref, P_ref, and 8 null C1/C2/C3 fields");

    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 18}, {TyreRole::Hard, 34}}});
    auto blocked_start = session.start();
    EXPECT_TRUE(!blocked_start.ok(), "start() must be blocked when calibration parameters are null");
    EXPECT_EQ(blocked_start.status, EngineStatus::InsufficientCapabilityOrCalibration,
              "Expected InsufficientCapabilityOrCalibration");

    ModelParameters est_params;
    est_params.param_set_id = "silverstone_estimated_test";
    est_params.parameter_source_status = SourceStatus::Estimated;
    est_params.ref_lap_time_s = 88.500;
    est_params.pit_loss_total_s = 21.000;

    auto addEstComp = [&](PhysicalCompoundId cid, double pace, double wear, double alpha) {
        CompoundParameterOverride ov;
        ov.compound_id = cid;
        ov.source_status = SourceStatus::Estimated;
        ov.pace_offset_ref_s = pace;
        ov.base_wear_rate_per_lap = wear;
        ov.linear_deg_coeff_s = alpha;
        est_params.compound_overrides[cid] = ov;
    };
    addEstComp(PhysicalCompoundId::C1, +0.70, 0.012, 4.0);
    addEstComp(PhysicalCompoundId::C2, +0.35, 0.018, 4.5);
    addEstComp(PhysicalCompoundId::C3,  0.00, 0.025, 5.0);

    session.setModelParameters(est_params);
    auto est_readiness = session.assessReadiness();
    EXPECT_TRUE(est_readiness.canExecute(), "Estimated parameters should permit lower-fidelity execution");
    EXPECT_EQ(est_readiness.fidelity_status, FidelityStatus::EstimatedOrAssumedLowerFidelity,
              "Must classify as EstimatedOrAssumedLowerFidelity, never Calibrated");
    EXPECT_EQ(est_readiness.execution_permission, ExecutionPermission::AllowedLowerFidelity,
              "Expected AllowedLowerFidelity");

    EXPECT_TRUE(session.start().ok(), "Start 52-lap Silverstone simulation");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete 52-lap Silverstone simulation");
    EXPECT_EQ(session.lastResult()->completed_race_laps, 52, "Completed all 52 laps at Silverstone");
    EXPECT_EQ(session.lastResult()->fidelity_status, FidelityStatus::EstimatedOrAssumedLowerFidelity,
              "Result metadata must record EstimatedOrAssumedLowerFidelity");

    EnvironmentScenarioConfig wet_env;
    wet_env.constant_environment.surface_wetness = 0.25;
    wet_env.constant_environment.precipitation_rate_mmph = 4.0;
    session.setEnvironmentConfig(wet_env);

    auto wet_val = session.validate();
    EXPECT_TRUE(!wet_val.ok(), "Wet environment must block dry-only simulation");
    EXPECT_EQ(wet_val.status, EngineStatus::UnsupportedWetConditions,
              "Expected UnsupportedWetConditions status");

    return true;
}

// ============================================================================
// TEST 8: Mathematical Conformance Suite (Framework Section 12.1, 12.2, 12.3,
//         Driver Mass, Radiative Track Temp Eq. 8.2, M3 Asymmetry, DAG Rollout)
// ============================================================================
bool testMathematicalConformanceAndSyntheticExamples(const fs::path& real_data_root) {
    auto dal = std::make_shared<DataAccessLayer>(real_data_root);
    EventBundle bundle = makeSyntheticExample1Bundle(10);
    ModelParameters params = makeSyntheticExample1Params();
    VehicleConfig vehicle = makeNeutralVehicleConfig();

    RaceSession session(dal);
    session.loadFromEventBundle(bundle);
    session.setModelParameters(params);
    session.setVehicleConfig(vehicle);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    EXPECT_TRUE(session.start().ok(), "Start Example 1A ((S,4),(M,6))");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete Example 1A");

    const RaceResult& ex1 = *session.lastResult();
    EXPECT_NEAR(ex1.stints[0].cumulative_flying_time_s, 360.000, 1e-9, "Example 1A Stint 1 cost = 360.000 s");
    EXPECT_NEAR(ex1.stints[1].cumulative_flying_time_s, 543.000, 1e-9, "Example 1A Stint 2 cost = 543.000 s");
    EXPECT_NEAR(ex1.total_pit_stop_time_loss_s, 20.000, 1e-9, "Example 1A Pit loss = 20.000 s");
    EXPECT_NEAR(ex1.total_race_time_s, 923.000, 1e-9, "Example 1A Total race time = 923.000 s");

    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 5}, {TyreRole::Medium, 5}}});
    EXPECT_TRUE(session.start().ok(), "Start Example 1A ((S,5),(M,5))");
    EXPECT_TRUE(session.runUntilStopOrCompletion().ok(), "Complete Example 1A ((S,5),(M,5))");
    EXPECT_NEAR(session.lastResult()->total_race_time_s, 923.000, 1e-9,
                "Example 1A ((S,5),(M,5)) must tie at 923.000 s");

    EnvironmentScenarioConfig hot_env;
    hot_env.constant_environment.track_temperature_c = 50.0;
    hot_env.constant_environment.air_temperature_c = 25.0;
    hot_env.constant_environment.relative_humidity_pct = 50.0;
    hot_env.model_form = EnvironmentModelForm::LinearisedFirstOrder;

    ModelParameters ex2_params = params;
    CompoundParameterOverride c4_cliff;
    c4_cliff.compound_id = PhysicalCompoundId::C4;
    c4_cliff.source_status = SourceStatus::Calibrated;
    c4_cliff.pace_offset_ref_s = -0.600;
    c4_cliff.base_wear_rate_per_lap = 0.080;
    c4_cliff.linear_deg_coeff_s = 5.000;
    c4_cliff.cliff_wear_threshold = 0.250;
    c4_cliff.cliff_amplitude_s = 4.500;
    c4_cliff.cliff_exponent = 2.000;
    c4_cliff.thermal_deg_sens_per_c = 0.040;
    ex2_params.compound_overrides[PhysicalCompoundId::C4] = c4_cliff;

    session.setModelStage(ModelStage::M2_DynamicEnvironmentDry);
    session.setEnvironmentConfig(hot_env);
    session.setModelParameters(ex2_params);

    auto evalStrategyTime = [&](int s_laps, int m_laps) -> double {
        session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, s_laps}, {TyreRole::Medium, m_laps}}});
        session.start();
        session.runUntilStopOrCompletion();
        return session.lastResult()->total_race_time_s;
    };

    EXPECT_NEAR(evalStrategyTime(4, 6), 925.483936, 1e-6, "Example 2 ((S,4),(M,6)) = 925.483936 s");
    EXPECT_NEAR(evalStrategyTime(3, 7), 925.580288, 1e-6, "Example 2 ((S,3),(M,7)) = 925.580288 s");
    EXPECT_NEAR(evalStrategyTime(5, 5), 926.693088, 1e-6, "Example 2 ((S,5),(M,5)) = 926.693088 s");

    EventBundle zero_deg_bundle = makeSyntheticExample1Bundle(10);
    zero_deg_bundle.event_config.min_distinct_dry_compounds = 1;
    zero_deg_bundle.event_config.min_pit_stops = 0;
    for (auto& c : zero_deg_bundle.compound_catalogue.compounds) {
        c.pace_offset_ref_s = 0.0;
        c.base_wear_rate_per_lap = 0.0;
        c.linear_deg_coeff_s = 1.0;
    }
    ModelParameters zero_deg_params = makeSyntheticExample1Params();
    zero_deg_params.outlap_warmup_loss_s = 0.50;

    session.loadFromEventBundle(zero_deg_bundle);
    session.setEnvironmentConfig(EnvironmentScenarioConfig{});
    session.setModelParameters(zero_deg_params);

    session.setStrategyPlan(StrategyPlan{{{TyreRole::Medium, 10}}});
    session.start();
    session.runUntilStopOrCompletion();
    const double t_0stop = session.lastResult()->total_race_time_s;
    EXPECT_NEAR(t_0stop, 900.000, 1e-9, "0-stop zero-deg race time = 900.000 s");

    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    session.start();
    session.runUntilStopOrCompletion();
    const double t_1stop = session.lastResult()->total_race_time_s;
    EXPECT_NEAR(t_1stop - t_0stop, 20.500, 1e-9,
                "TEST_07: T_race(J=2) - T_race(J=1) must equal P_1 + dT_out (20.500 s) with no double counting");

    EventBundle event_a = makeSyntheticExample1Bundle(10);
    EventBundle event_b = makeSyntheticExample1Bundle(10);
    event_b.event_config.nominated_compounds_set = {
        PhysicalCompoundId::C3, PhysicalCompoundId::C4, PhysicalCompoundId::C5};
    event_b.role_mapping.nominated_compounds_set = {
        PhysicalCompoundId::C3, PhysicalCompoundId::C4, PhysicalCompoundId::C5};
    event_b.role_mapping.mappings = {
        {"synth_event_alpha", TyreRole::Hard, PhysicalCompoundId::C3, "WHITE", 3},
        {"synth_event_alpha", TyreRole::Medium, PhysicalCompoundId::C4, "YELLOW", 4},
        {"synth_event_alpha", TyreRole::Soft, PhysicalCompoundId::C5, "RED", 5}
    };

    session.loadFromEventBundle(event_a);
    session.setModelParameters(params);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    session.start();
    session.runUntilStopOrCompletion();
    const double time_event_a = session.lastResult()->total_race_time_s;

    session.loadFromEventBundle(event_b);
    session.setModelParameters(params);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Medium, 4}, {TyreRole::Hard, 6}}});
    session.start();
    session.runUntilStopOrCompletion();
    const double time_event_b = session.lastResult()->total_race_time_s;

    EXPECT_NEAR(time_event_a, time_event_b, 1e-12,
                "TEST_04: Role labels must resolve through EventRoleMapping without hard-coded C-numbers");

    EnvironmentScenarioConfig hum_low;
    hum_low.constant_environment.relative_humidity_pct = 10.0;
    EnvironmentScenarioConfig hum_high;
    hum_high.constant_environment.relative_humidity_pct = 95.0;

    session.loadFromEventBundle(event_a);
    session.setStrategyPlan(StrategyPlan{{{TyreRole::Soft, 4}, {TyreRole::Medium, 6}}});
    session.setEnvironmentConfig(hum_low);
    session.start();
    session.runUntilStopOrCompletion();
    const double t_hum_10 = session.lastResult()->total_race_time_s;

    session.setEnvironmentConfig(hum_high);
    session.start();
    session.runUntilStopOrCompletion();
    const double t_hum_95 = session.lastResult()->total_race_time_s;
    EXPECT_NEAR(t_hum_10, t_hum_95, 1e-12, "TEST_06: Humidity neutrality (psi_hum == 0)");

    const ResolvedCompoundParams c4_res =
        LapStintEvaluator::resolveCompoundParameters(event_a, PhysicalCompoundId::C4, ex2_params);
    auto evalAtWear = [&](double w) {
        return LapStintEvaluator::evaluateFlyingLap(
            2, 0, 2, CornerHealthVector::uniform(1.0 - w),
            event_a, c4_res, vehicle, ex2_params, ModelStage::M3_NonlinearCliffAndFourCorner)
            .value->tyre_degradation_loss_s;
    };
    const double eps = 1e-7;
    const double w_crit = 0.250;
    const double val_minus = evalAtWear(w_crit - eps);
    const double val_plus  = evalAtWear(w_crit + eps);
    const double deriv_minus = ( evalAtWear(w_crit) - evalAtWear(w_crit - eps) ) / eps;
    const double deriv_plus  = ( evalAtWear(w_crit + eps) - evalAtWear(w_crit) ) / eps;
    EXPECT_NEAR(val_minus, val_plus, 1e-5, "TEST_10: Value continuity at w_crit");
    EXPECT_NEAR(deriv_minus, deriv_plus, 1e-4, "TEST_10: First-derivative C1 continuity at w_crit");

    ModelParameters fuel_sens_params = params;
    fuel_sens_params.fuel_lap_sens_s_per_kg = 0.032;
    session.setEnvironmentConfig(EnvironmentScenarioConfig{});
    session.setModelParameters(fuel_sens_params);
    session.setDriverMassKg(75.0);
    session.start();
    session.runUntilStopOrCompletion();
    const double time_75kg = session.lastResult()->total_race_time_s;

    session.setDriverMassKg(85.0);
    session.start();
    session.runUntilStopOrCompletion();
    const double time_85kg = session.lastResult()->total_race_time_s;
    EXPECT_NEAR(time_85kg - time_75kg, 3.200, 1e-9,
                "Heavier driver (+10 kg) must add 10 * 0.032 * 10 = 3.200 s via framework mass sensitivity");

    session.setModelStage(ModelStage::M3_NonlinearCliffAndFourCorner);
    session.start();
    session.runUntilStopOrCompletion();
    const CornerHealthVector& m3_health = session.lastResult()->lap_trace[0].health_end;
    EXPECT_TRUE(m3_health[0] < m3_health[1],
                "In Stage M3 on clockwise circuit (N_R > N_L), outside FL corner must wear more than inside FR corner");

    auto rollout_s4 = LapStintEvaluator::evaluateStintRollout(
        0, 4, TyreRole::Soft, 0, CornerHealthVector::uniform(1.0),
        event_a, vehicle, EnvironmentScenarioConfig{}, params, ModelStage::M0_DeterministicBaseline);
    EXPECT_TRUE(rollout_s4.ok() && rollout_s4.value->feasible, "Stint rollout (0..4, Soft) must be feasible");
    EXPECT_NEAR(rollout_s4.value->cumulative_flying_time_s, 360.000, 1e-9,
                "Stateless evaluateStintRollout must match session Stint 1 cost (360.000 s)");

    return true;
}

// ============================================================================
// INTERACTIVE CLI VERIFICATION SUITES (Tests 09 - 13)
// ============================================================================
using namespace f1_sim::cli;

bool containsSubstr(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool testCliFlagsHelpCheckOnlySmokeTestAndDefaultMenu(const fs::path& data_root) {
    {
        CliOptions opts;
        opts.data_root = data_root;
        opts.explicit_data_root = true;

        std::istringstream in("7\n");
        std::ostringstream out;
        std::ostringstream err;

        CliApp app(opts, in, out, err);
        const int rc = app.run("f1_sim");
        EXPECT_EQ(rc, kExitOk, "Launching without flags and choosing 7 (Exit) must return 0");
        const std::string output = out.str();
        EXPECT_TRUE(containsSubstr(output, "Main Menu:"), "Must open Interactive Main Menu");
        EXPECT_TRUE(containsSubstr(output, "1. Select event / track"), "Must show Option 1");
        EXPECT_TRUE(containsSubstr(output, "2. Configure tyre inventory"), "Must show Option 2");
        EXPECT_TRUE(containsSubstr(output, "3. Build or edit race strategy"), "Must show Option 3");
        EXPECT_TRUE(containsSubstr(output, "4. Configure race settings"), "Must show Option 4");
        EXPECT_TRUE(containsSubstr(output, "5. Inspect data readiness and model assumptions"), "Must show Option 5");
        EXPECT_TRUE(containsSubstr(output, "6. Start simulation"), "Must show Option 6");
        EXPECT_TRUE(containsSubstr(output, "7. Exit"), "Must show Option 7");
        EXPECT_TRUE(!app.session().lastResult().has_value(),
                    "Default launch without --smoke-test must NOT auto-execute a race");
    }

    {
        const char* argv[] = {"f1_sim", "--help"};
        CliOptions opts;
        std::string err_msg;
        EXPECT_TRUE(parseCliArgs(2, const_cast<char**>(argv), opts, err_msg), "Parse --help");
        EXPECT_TRUE(opts.show_help, "show_help must be true");

        std::istringstream in("");
        std::ostringstream out;
        std::ostringstream err;
        CliApp app(opts, in, out, err);
        EXPECT_EQ(app.run("f1_sim"), kExitOk, "--help must return 0");
        EXPECT_TRUE(containsSubstr(out.str(), "--smoke-test"), "--help must document --smoke-test");
        EXPECT_TRUE(containsSubstr(out.str(), "--check-only"), "--help must document --check-only");
    }

    {
        CliOptions opts;
        opts.data_root = data_root;
        opts.explicit_data_root = true;
        opts.check_only = true;

        std::istringstream in("");
        std::ostringstream out;
        std::ostringstream err;
        CliApp app(opts, in, out, err);
        EXPECT_EQ(app.run("f1_sim"), kExitOk, "--check-only must return 0");
        EXPECT_TRUE(containsSubstr(out.str(), "[--check-only] Dataset and readiness inspection complete."),
                    "--check-only must complete inspection without running a race");
    }

    {
        CliOptions opts;
        opts.data_root = data_root;
        opts.explicit_data_root = true;
        opts.smoke_test = true;

        std::istringstream in("");
        std::ostringstream out;
        std::ostringstream err;
        CliApp app(opts, in, out, err);
        EXPECT_EQ(app.run("f1_sim"), kExitOk, "--smoke-test must return 0");
        EXPECT_TRUE(app.session().lastResult().has_value(), "--smoke-test must produce a RaceResult");
        const RaceResult& res = *app.session().lastResult();
        EXPECT_EQ(res.completed_race_laps, 52, "Smoke test must complete 52 laps");
        EXPECT_EQ(res.stints.size(), std::size_t(2), "Smoke test must have 2 stints");
        EXPECT_EQ(res.stints[0].actual_laps_completed, 18, "Stint 1 = 18 laps Soft");
        EXPECT_EQ(res.stints[1].actual_laps_completed, 34, "Stint 2 = 34 laps Hard");
        EXPECT_NEAR(res.total_race_time_s, 4724.174, 1e-3, "Smoke test total race time must be 4724.174 s");
        EXPECT_TRUE(containsSubstr(out.str(), "VERIFIED (all 7 sets restored to pre-race configured state)"),
                    "Smoke test must verify post-run inventory reset");
    }

    {
        CliOptions opts;
        opts.data_root = data_root;
        opts.explicit_data_root = true;
        opts.smoke_test = true;
        opts.strict_calibrated = true;

        std::istringstream in("");
        std::ostringstream out;
        std::ostringstream err;
        CliApp app(opts, in, out, err);
        EXPECT_EQ(app.run("f1_sim"), kExitValidationError,
                  "--strict-calibrated must block uncalibrated execution with exit code 3");
    }

    return true;
}

bool testEventSelectionAndTyreInventoryWorkflow(const fs::path& data_root) {
    CliOptions opts;
    opts.data_root = data_root;
    opts.explicit_data_root = true;

    std::ostringstream script;
    script << "2\n"      // Main Menu: 2. Configure tyre inventory
           << "2\n"      // Inventory: 2. Edit age
           << "S1\n"     // Set S1
           << "5\n"      // Age 5 laps
           << "4\n"      // Return to main menu
           << "1\n"      // Main Menu: 1. Select event / track
           << "1\n"      // Select event #1 (2026_british_gp)
           << "n\n"      // Decline confirmation -> S1 must remain at age 5
           << "1\n"      // Main Menu: 1. Select event / track
           << "2026_british_gp\n" // Select by event_id
           << "y\n"      // Confirm event selection -> resets all ages to 0
           << "2\n"      // Main Menu: 2. Configure tyre inventory
           << "2\n"      // Edit age
           << "S99\n"    // Invalid set ID -> rejected by engine
           << "M1\n"     // Valid set M1
           << "-3\n"     // Invalid negative age -> rejected
           << "abc\n"    // Non-numeric age -> rejected
           << "4\n"      // Valid age 4 laps
           << "4\n"      // Return to main menu
           << "4\n"      // Main Menu: 4. Configure race settings
           << "4\n"      // Toggle strict calibration gate ON
           << "6\n"      // Return to main menu
           << "2\n"      // Main Menu: 2. Configure tyre inventory (M1 age=4 with uncalibrated raw dataset -> UNAVAILABLE)
           << "3\n"      // Reset all 7 sets to age 0
           << "4\n"      // Return to main menu
           << "7\n";     // Exit

    std::istringstream in(script.str());
    std::ostringstream out;
    std::ostringstream err;

    CliApp app(opts, in, out, err);
    const int rc = app.run("f1_sim");
    EXPECT_EQ(rc, kExitOk, "Interactive event & inventory workflow must exit cleanly with 0");

    const std::string output = out.str();
    EXPECT_TRUE(containsSubstr(output, "1 valid event found (only one event is available in the current dataset)"),
                "Criterion 5: Must clearly report that only 1 valid event is available");
    EXPECT_TRUE(containsSubstr(output, "Event switch cancelled. Retaining existing event and tyre-set ages."),
                "Declining event switch confirmation must retain ages");
    EXPECT_TRUE(containsSubstr(output, "Successfully selected event '2026_british_gp'. All 7 physical tyre sets have been reset to age 0 laps."),
                "Criterion 4 & 16: Confirming event selection loads event and resets tyre ages to 0");
    EXPECT_TRUE(containsSubstr(output, "Unknown tyre set ID 'S99'"),
                "Invalid set ID must be rejected with engine diagnostic");
    EXPECT_TRUE(containsSubstr(output, "UNAVAILABLE (uncalibrated wear rate)"),
                "Criterion 6: Unavailable health must be displayed as UNAVAILABLE, never as 0%");
    EXPECT_TRUE(containsSubstr(output, "All seven physical tyre sets (S1..S3, M1..M2, H1..H2) reset to age 0 laps"),
                "Criterion 6: Resetting all 7 sets to age 0 must succeed");

    for (const auto& s : app.session().tyreInventory()) {
        EXPECT_EQ(s.configured_age_laps, 0, "All sets must end at age 0 after reset");
    }

    return true;
}

bool testStrategyBuilderValidationAndPreplannedRepeatedRuns(const fs::path& data_root) {
    CliOptions opts;
    opts.data_root = data_root;
    opts.explicit_data_root = true;

    std::ostringstream script;
    script << "2\n"      // Main Menu: 2. Configure tyre inventory
           << "2\n"      // Edit age
           << "S2\n"     // Set S2
           << "3\n"      // Age 3 laps
           << "4\n"      // Return to main menu
           << "3\n"      // Main Menu: 3. Build or edit race strategy
           << "1\n"      // Invalid Strategy #1: total laps 15 + 20 = 35 != 52
           << "2\n"
           << "S\nS1\n15\n"
           << "H\nH1\n20\n"
           << "1\n"      // Invalid Strategy #2: duplicate set S1
           << "2\n"
           << "S\nS1\n20\n"
           << "S\nS1\n32\n"
           << "1\n"      // Invalid Strategy #3: incompatible set-role (Soft with H1)
           << "2\n"
           << "S\nH1\n20\n"
           << "H\nH2\n32\n"
           << "1\n"      // Valid 3-stint strategy: (Soft, S2, 15), (Medium, M1, 20), (Hard, H1, 17)
           << "3\n"
           << "S\nS2\n15\n"
           << "M\nM1\n20\n"
           << "H\nH1\n17\n"
           << "y\n"      // Confirm and commit valid strategy
           << "7\n"      // Return to main menu
           << "6\n"      // Main Menu: 6. Start simulation (#1)
           << "y\n"      // Confirm lower-fidelity execution
           << "1\n"      // Automatic progression
           << "1\n"      // Post-Race Menu: 1. Run another simulation with same event, inventory ages, and strategy
           << "y\n"      // Confirm lower-fidelity execution
           << "1\n"      // Automatic progression
           << "6\n";     // Post-Race Menu: 6. Exit

    std::istringstream in(script.str());
    std::ostringstream out;
    std::ostringstream err;

    CliApp app(opts, in, out, err);
    const int rc = app.run("f1_sim");
    EXPECT_EQ(rc, kExitOk, "Strategy builder and repeated preplanned runs must return 0");

    const std::string output = out.str();
    EXPECT_TRUE(containsSubstr(output, "Sum of target stint lengths (35) must equal configured race_distance_laps (52)"),
                "Criterion 9: Must reject strategy whose stint lengths do not sum to 52");
    EXPECT_TRUE(containsSubstr(output, "Physical tyre set 'S1' is assigned to multiple stints in the same race"),
                "Criterion 9: Must reject duplicate physical tyre set assignment");
    EXPECT_TRUE(containsSubstr(output, "Requested tyre set 'H1' has role HARD, which does not match stint role SOFT"),
                "Criterion 9: Must reject incompatible set-role assignment");
    EXPECT_TRUE(containsSubstr(output, "[STRATEGY VALID] All stint lengths, role mappings, and physical set allocations are valid."),
                "Criterion 8: Valid 3-stint strategy must pass engine validation");

    EXPECT_TRUE(app.session().lastResult().has_value(), "Race result must be present");
    const RaceResult& res = *app.session().lastResult();
    EXPECT_EQ(res.completed_race_laps, 52, "Criterion 10: Preplanned 3-stint race must complete all 52 laps");
    EXPECT_EQ(res.stints.size(), std::size_t(3), "Must have 3 stints");
    EXPECT_EQ(res.stints[0].assigned_set_id, std::string("S2"), "Stint 1 used S2");
    EXPECT_EQ(res.stints[0].set_configured_age_at_fit_laps, 3, "S2 started at configured age 3 laps across repeated runs");
    EXPECT_EQ(res.stints[1].assigned_set_id, std::string("M1"), "Stint 2 used M1");
    EXPECT_EQ(res.stints[2].assigned_set_id, std::string("H1"), "Stint 3 used H1");

    for (const auto& s : app.session().tyreInventory()) {
        EXPECT_TRUE(!s.currently_fitted && !s.consumed_in_current_run,
                    "Criterion 14: Post-run fitted and consumed flags must be reset");
        EXPECT_EQ(s.runtime_age_laps, s.configured_age_laps,
                  "Criterion 14: Post-run runtime_age_laps must equal configured_age_laps");
        if (s.set_id == "S2") {
            EXPECT_EQ(s.configured_age_laps, 3, "Criterion 14 & 15: S2 configured age (3) must persist across runs");
        }
    }

    return true;
}

bool testInteractiveModeDecisionsAndMandatoryStopHandling(const fs::path& data_root) {
    CliOptions opts;
    opts.data_root = data_root;
    opts.explicit_data_root = true;
    opts.race_mode = RaceMode::Interactive;

    std::ostringstream script;
    script << "2\n"      // Main Menu: 2. Configure tyre inventory
           << "2\n"      // Edit age
           << "S1\n"     // Set S1
           << "15\n"     // Age 15 laps (starts at ~52.8% health; breaches <30% after 7 race laps!)
           << "4\n"      // Return to main menu
           << "3\n"      // Main Menu: 3. Strategy Builder
           << "1\n"      // Build from scratch
           << "2\n"      // 2 stints
           << "S\nS1\n20\n" // Stint 1: Soft (S1, age 15), target 20 laps (triggers mandatory stop at Lap 7!)
           << "H\nH1\n32\n" // Stint 2: Hard (H1), target 32 laps
           << "y\n"      // Commit strategy
           << "7\n"      // Return to main menu
           << "6\n"      // Main Menu: 6. Start simulation
           << "y\n"      // Confirm lower-fidelity execution
           << "1\n"      // Automatic progression to first decision point (Mandatory Stop at Lap 7, 45 laps remaining)
           << "2\n"      // Attempt Option 2 (Extend current stint) -> MUST be rejected (MandatoryStopCannotBeOverridden)!
           << "1\n"      // Attempt Option 1 (KeepOriginalPlan: 32L != 45L) -> MUST be rejected (no silent cascade)!
           << "3\n"      // Option 3: PitNowWithRevisedPlan
           << "1\nH\nH1\n30\n" // Invalid remaining lap sum (30 != 45) -> rejected without corrupting state!
           << "3\n"      // Option 3: PitNowWithRevisedPlan (valid revision for 45 remaining laps)
           << "2\n"      // 2 remaining stints
           << "M\nM1\n18\n" // Stint 2: Medium (M1), 18 laps (Lap 8..25)
           << "H\nH1\n27\n" // Stint 3: Hard (H1), 27 laps (Lap 26..52; 18 + 27 = 45 laps)
           << "2\n"      // Option 2: ExtendCurrentStint at Lap 25
           << "2\n"      // +2 additional laps
           << "n\n"      // Do NOT revise remaining stints -> rejected (2 + 27 = 29 != 27, no auto-subtraction)!
           << "2\n"      // Option 2: ExtendCurrentStint
           << "2\n"      // +2 additional laps (leaving 25 laps for Stint 3)
           << "y\n"      // Revise remaining stints
           << "1\n"      // 1 remaining stint
           << "H\nH1\n25\n" // Stint 3: Hard (H1), 25 laps (2 + 25 = 27 remaining laps)
           << "1\n"      // Option 1 at Lap 27: KeepOriginalPlan (pit onto H1 for final 25 laps)
           << "6\n";     // Exit

    std::istringstream in(script.str());
    std::ostringstream out;
    std::ostringstream err;

    CliApp app(opts, in, out, err);
    const int rc = app.run("f1_sim");
    EXPECT_EQ(rc, kExitOk, "Interactive decision workflow must complete and return 0");

    const std::string output = out.str();
    EXPECT_TRUE(containsSubstr(output, "INTERACTIVE RACE DECISION REQUIRED — END OF LAP 7"),
                "Criterion 11 & 13: Must pause at Lap 7 for Mandatory Health Stop decision");
    EXPECT_TRUE(containsSubstr(output, "[MANDATORY STOP CANNOT BE OVERRIDDEN]"),
                "Criterion 13: Must reject extending stint when mandatory health stop is active");
    EXPECT_TRUE(containsSubstr(output, "The engine does not automatically subtract an extension or cascade a shortfall in Interactive mode."),
                "Criterion 12 & 13: Must reject un-replanned shortfall and unadjusted extension without auto-cascading or auto-subtracting");

    EXPECT_TRUE(app.session().lastResult().has_value(), "Race must finish with a valid RaceResult");
    const RaceResult& res = *app.session().lastResult();
    EXPECT_EQ(res.completed_race_laps, 52, "Interactive race must complete all 52 laps");
    EXPECT_EQ(res.stints.size(), std::size_t(3), "Completed 3 stints after interactive re-plan");
    EXPECT_EQ(res.stints[0].actual_laps_completed, 7, "Stint 1 ran 7 laps before mandatory health stop");
    EXPECT_TRUE(res.stints[0].ended_by_mandatory_stop, "Stint 1 marked ended_by_mandatory_stop");
    EXPECT_EQ(res.stints[1].actual_laps_completed, 20, "Stint 2 ran 18 + 2 extended = 20 laps on M1");
    EXPECT_EQ(res.stints[2].actual_laps_completed, 25, "Stint 3 ran 25 laps on H1 (7 + 20 + 25 = 52)");
    EXPECT_EQ(res.interventions.size(), std::size_t(3), "3 committed interactive decisions logged");

    return true;
}

bool testStepByStepPauseResumeAndCancel(const fs::path& data_root) {
    CliOptions opts;
    opts.data_root = data_root;
    opts.explicit_data_root = true;

    std::ostringstream script;
    script << "3\n"      // Main Menu: 3. Strategy Builder
           << "1\n"      // Build from scratch
           << "2\n"      // 2 stints
           << "S\nAUTO\n18\n"
           << "H\nAUTO\n34\n"
           << "y\n"      // Commit strategy
           << "7\n"      // Return to main menu
           << "6\n"      // Main Menu: 6. Start simulation
           << "y\n"      // Confirm lower-fidelity execution
           << "2\n"      // Select 2: Step-by-step / Pauseable lap control
           << "2\n"      // Advance 1 lap (Lap 1)
           << "4\n"      // Pause simulation after Lap 1
           << "5\n"      // Inspect live telemetry snapshot while PAUSED
           << "4\n"      // Resume simulation
           << "3\n"      // Advance N laps
           << "3\n"      // Advance 3 laps (Laps 2, 3, 4)
           << "6\n"      // Cancel active race after Lap 4
           << "6\n";     // Post-simulation menu: 6. Exit

    std::istringstream in(script.str());
    std::ostringstream out;
    std::ostringstream err;

    CliApp app(opts, in, out, err);
    const int rc = app.run("f1_sim");
    EXPECT_EQ(rc, kExitOk, "Step-by-step pause/resume/cancel workflow must return 0");

    const std::string output = out.str();
    EXPECT_TRUE(containsSubstr(output, "[PAUSED] Simulation paused at safe lap boundary after Lap 1."),
                "Must support pausing at safe lap boundary");
    EXPECT_TRUE(containsSubstr(output, "--- Live Race Snapshot (State: PAUSED) ---"),
                "Must support inspecting live telemetry snapshot while paused");
    EXPECT_TRUE(containsSubstr(output, "[RESUMED] Simulation resumed from Lap 1."),
                "Must support resuming paused simulation");
    EXPECT_TRUE(containsSubstr(output, "[CANCELLED] Active race cancelled after Lap 4."),
                "Must support cancelling active simulation and resetting runtime tyre state");

    EXPECT_TRUE(app.session().lastResult().has_value(), "Cancelled race must record partial RaceResult");
    EXPECT_EQ(app.session().lastResult()->terminal_state, SessionState::Cancelled, "Terminal state is Cancelled");
    EXPECT_EQ(app.session().lastResult()->completed_race_laps, 4, "Completed 4 laps before cancellation");

    for (const auto& s : app.session().tyreInventory()) {
        EXPECT_TRUE(!s.currently_fitted && !s.consumed_in_current_run,
                    "Cancelled simulation must restore all 7 tyre sets to unconsumed state");
    }

    return true;
}

void runTest(const char* name, bool (*fn)(const fs::path&), const fs::path& data_root) {
    ++g_tests_run;
    std::cout << "[ RUN      ] " << name << "\n";
    if (fn(data_root)) {
        std::cout << "[       OK ] " << name << "\n";
    } else {
        ++g_tests_failed;
        std::cout << "[  FAILED  ] " << name << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    fs::path data_root = "data";
    if (argc >= 2) {
        data_root = argv[1];
    }

    std::cout << "====================================================================\n";
    std::cout << "F1 Simulator — Race Simulation Engine Verification Suite\n";
    std::cout << "Configured data_root: " << data_root.generic_string() << "\n";
    std::cout << "====================================================================\n";

    runTest("Test01_StrategyValidation", testStrategyValidation, data_root);
    runTest("Test02_InventoryAgeHealthAndReset", testInventoryAgeHealthAndReset, data_root);
    runTest("Test03_StrictHealthThresholdsAndStopTiming", testStrictHealthThresholdsAndStopTiming, data_root);
    runTest("Test04_PreplannedModeAndShortfallCascade", testPreplannedModeAndShortfallCascade, data_root);
    runTest("Test05_InteractiveModeDecisionsAndMandatoryPrecedence", testInteractiveModeDecisionsAndMandatoryPrecedence, data_root);
    runTest("Test06_LifecyclePauseResumeAndSnapshotDeterminism", testLifecyclePauseResumeAndSnapshotDeterminism, data_root);
    runTest("Test07_DALIntegrationAndCapabilityGating", testDALIntegrationAndCapabilityGating, data_root);
    runTest("Test08_MathematicalConformanceAndSyntheticExamples", testMathematicalConformanceAndSyntheticExamples, data_root);
    runTest("Test09_CliFlagsHelpCheckOnlySmokeTestAndDefaultMenu", testCliFlagsHelpCheckOnlySmokeTestAndDefaultMenu, data_root);
    runTest("Test10_CliEventSelectionInventoryAgeHealthAndReset", testEventSelectionAndTyreInventoryWorkflow, data_root);
    runTest("Test11_CliStrategyBuilderAndPreplannedExecution", testStrategyBuilderValidationAndPreplannedRepeatedRuns, data_root);
    runTest("Test12_CliInteractiveModeDecisionsAndMandatoryStopNonOverride", testInteractiveModeDecisionsAndMandatoryStopHandling, data_root);
    runTest("Test13_CliStepByStepPauseResumeAndCancel", testStepByStepPauseResumeAndCancel, data_root);

    std::cout << "====================================================================\n";
    std::cout << "Tests run: " << g_tests_run << ", Failures: " << g_tests_failed << "\n";
    std::cout << "====================================================================\n";

    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}