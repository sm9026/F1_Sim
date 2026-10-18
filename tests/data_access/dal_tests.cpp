#include "f1_sim/data_access/data_access_layer.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace f1_sim::data_access;

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

void writeTextFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
    ofs << content;
}

std::string readTextFile(const fs::path& path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

/**
 * @brief Helper that creates a self-contained temporary directory outside the repository
 *        and cleans it up on destruction. Never touches the real repository `data/`.
 */
class ScopedTempDataRoot {
public:
    explicit ScopedTempDataRoot(const std::string& label, const fs::path& real_data_root) {
        root_ = fs::temp_directory_path() / ("f1_sim_dal_test_" + label);
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "tracks", ec);
        fs::create_directories(root_ / "events", ec);
        fs::create_directories(root_ / "compounds", ec);

        fs::copy_file(
            real_data_root / "tracks/silverstone_gp_layout_v2011.json",
            root_ / "tracks/silverstone_gp_layout_v2011.json",
            fs::copy_options::overwrite_existing, ec);
        fs::copy_file(
            real_data_root / "tracks/silverstone_gp_surface_v2019.json",
            root_ / "tracks/silverstone_gp_surface_v2019.json",
            fs::copy_options::overwrite_existing, ec);
        fs::copy_file(
            real_data_root / "events/2026_british_gp_config.json",
            root_ / "events/2026_british_gp_config.json",
            fs::copy_options::overwrite_existing, ec);
        fs::copy_file(
            real_data_root / "events/2026_british_gp_role_mapping.json",
            root_ / "events/2026_british_gp_role_mapping.json",
            fs::copy_options::overwrite_existing, ec);
        fs::copy_file(
            real_data_root / "compounds/2026_pirelli_slick_catalogue_v1.json",
            root_ / "compounds/2026_pirelli_slick_catalogue_v1.json",
            fs::copy_options::overwrite_existing, ec);
    }

    ~ScopedTempDataRoot() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    const fs::path& path() const noexcept { return root_; }

private:
    fs::path root_;
};

bool hasDiagnosticWithCode(const std::vector<Diagnostic>& diags, DiagnosticCode code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// TEST 1: Load actual current 2026 British GP event bundle from real data_root
// ============================================================================
bool testLoadActualSilverstoneEventBundle(const fs::path& real_data_root) {
    auto collector = std::make_shared<InMemoryDiagnosticCollector>();
    DataAccessLayer dal(real_data_root, collector);

    Result<EventBundle> res = dal.loadEventBundle("2026_british_gp");
    EXPECT_TRUE(res.ok(), "Expected 2026_british_gp event bundle to load successfully");
    EXPECT_EQ(res.status, LoadStatus::Success, "Expected LoadStatus::Success");

    const EventBundle& bundle = *res.value;

    EXPECT_EQ(bundle.event_config.event_id, std::string("2026_british_gp"), "event_id mismatch");
    EXPECT_EQ(bundle.event_config.official_event_name,
              std::string("FORMULA 1 PIRELLI BRITISH GRAND PRIX 2026"),
              "official_event_name mismatch");
    EXPECT_EQ(bundle.event_config.round_number, 9, "round_number mismatch");
    EXPECT_EQ(bundle.event_config.season_year, 2026, "season_year mismatch");
    EXPECT_EQ(bundle.event_config.competition_format,
              CompetitionFormat::AlternativeFormatSprint,
              "competition_format mismatch");
    EXPECT_EQ(bundle.event_config.track_id, std::string("silverstone_gp"), "track_id mismatch");
    EXPECT_EQ(bundle.event_config.layout_version, 2011, "layout_version mismatch");
    EXPECT_EQ(bundle.event_config.surface_version, 2019, "surface_version mismatch");
    EXPECT_EQ(bundle.event_config.race_distance_laps, 52, "race_distance_laps mismatch");
    EXPECT_NEAR(bundle.event_config.scheduled_race_distance_km, 306.198, 1e-6, "scheduled_race_distance_km mismatch");
    EXPECT_EQ(bundle.event_config.min_distinct_dry_compounds, 2, "min_distinct_dry_compounds mismatch");
    EXPECT_EQ(bundle.event_config.available_sets_hard, 2, "available_sets_hard mismatch");
    EXPECT_EQ(bundle.event_config.available_sets_med, 4, "available_sets_med mismatch");
    EXPECT_EQ(bundle.event_config.available_sets_soft, 6, "available_sets_soft mismatch");

    EXPECT_EQ(bundle.track_layout.track_id, std::string("silverstone_gp"), "layout track_id mismatch");
    EXPECT_EQ(bundle.track_layout.layout_version, 2011, "layout_version mismatch");
    EXPECT_NEAR(bundle.track_layout.lap_length_m, 5891.0, 1e-6, "lap_length_m mismatch");
    EXPECT_EQ(bundle.track_layout.race_direction, RaceDirection::Clockwise, "race_direction mismatch");
    EXPECT_NEAR(bundle.track_layout.pit_lane_length_m, 457.0, 1e-6, "pit_lane_length_m mismatch");
    EXPECT_NEAR(bundle.track_layout.pit_speed_limit_kmh, 80.0, 1e-6, "pit_speed_limit_kmh mismatch");
    EXPECT_EQ(bundle.track_layout.corners_total_count, 18, "corners_total_count mismatch");
    EXPECT_EQ(bundle.track_layout.corners_left_count, 8, "corners_left_count mismatch");
    EXPECT_EQ(bundle.track_layout.corners_right_count, 10, "corners_right_count mismatch");
    EXPECT_NEAR(bundle.track_layout.lateral_energy_index_norm, 1.50, 1e-6, "lateral_energy_index_norm mismatch");
    EXPECT_NEAR(bundle.track_layout.braking_energy_index_norm, 0.75, 1e-6, "braking_energy_index_norm mismatch");
    EXPECT_NEAR(bundle.track_layout.traction_energy_index_norm, 1.00, 1e-6, "traction_energy_index_norm mismatch");

    EXPECT_EQ(bundle.track_surface.track_id, std::string("silverstone_gp"), "surface track_id mismatch");
    EXPECT_EQ(bundle.track_surface.layout_version, 2011, "surface layout_version mismatch");
    EXPECT_EQ(bundle.track_surface.surface_version, 2019, "surface_version mismatch");
    EXPECT_NEAR(bundle.track_surface.surface_abrasiveness_score, 2.0, 1e-6, "surface_abrasiveness_score mismatch");
    EXPECT_NEAR(bundle.track_surface.surface_grip_baseline_score, 4.0, 1e-6, "surface_grip_baseline_score mismatch");
    EXPECT_NEAR(bundle.track_surface.track_evolution_amplitude_s, 0.0, 1e-6, "track_evolution_amplitude_s mismatch");
    EXPECT_NEAR(bundle.track_surface.track_evolution_decay_laps, 20.0, 1e-6, "track_evolution_decay_laps mismatch");

    const PhysicalCompoundSpec* hard_spec = bundle.compoundForRole(TyreRole::Hard);
    const PhysicalCompoundSpec* med_spec = bundle.compoundForRole(TyreRole::Medium);
    const PhysicalCompoundSpec* soft_spec = bundle.compoundForRole(TyreRole::Soft);
    EXPECT_TRUE(hard_spec != nullptr, "Hard compound spec must resolve");
    EXPECT_TRUE(med_spec != nullptr, "Medium compound spec must resolve");
    EXPECT_TRUE(soft_spec != nullptr, "Soft compound spec must resolve");
    EXPECT_EQ(hard_spec->compound_id, PhysicalCompoundId::C1, "HARD must map to C1");
    EXPECT_EQ(med_spec->compound_id, PhysicalCompoundId::C2, "MEDIUM must map to C2");
    EXPECT_EQ(soft_spec->compound_id, PhysicalCompoundId::C3, "SOFT must map to C3");
    EXPECT_EQ(hard_spec->hardness_rank, 1, "C1 hardness_rank must be 1");
    EXPECT_EQ(med_spec->hardness_rank, 2, "C2 hardness_rank must be 2");
    EXPECT_EQ(soft_spec->hardness_rank, 3, "C3 hardness_rank must be 3");

    for (const auto& d : res.diagnostics) {
        EXPECT_TRUE(d.relative_path.empty() || d.relative_path[0] != '/',
                    "Diagnostic relative_path must be relative to data_root, not absolute");
    }

    return true;
}

// ============================================================================
// TEST 2: Individual dataset queries
// ============================================================================
bool testIndividualDatasetQueries(const fs::path& real_data_root) {
    DataAccessLayer dal(real_data_root);

    auto layout_res = dal.loadTrackLayout("silverstone_gp", 2011);
    EXPECT_TRUE(layout_res.ok(), "loadTrackLayout failed");
    EXPECT_EQ(layout_res.value->track_name, std::string("Silverstone Circuit"), "track_name mismatch");

    auto surface_res = dal.loadTrackSurface("silverstone_gp", 2019, 2011);
    EXPECT_TRUE(surface_res.ok(), "loadTrackSurface failed");
    EXPECT_EQ(surface_res.value->valid_from_date, std::string("2019-08-22"), "valid_from_date mismatch");

    auto cfg_res = dal.loadEventConfiguration("2026_british_gp");
    EXPECT_TRUE(cfg_res.ok(), "loadEventConfiguration failed");
    EXPECT_EQ(cfg_res.value->event_dates.race_date, std::string("2026-07-05"), "race_date mismatch");

    auto role_res = dal.loadEventRoleMapping("2026_british_gp");
    EXPECT_TRUE(role_res.ok(), "loadEventRoleMapping failed");
    EXPECT_EQ(role_res.value->mappings.size(), std::size_t(3), "mappings size must be 3");

    auto cat_res = dal.loadCompoundCatalogue(2026, 1);
    EXPECT_TRUE(cat_res.ok(), "loadCompoundCatalogue failed");
    EXPECT_EQ(cat_res.value->compounds.size(), std::size_t(5), "compounds size must be 5");

    auto c5_res = dal.loadCompoundSpec(2026, 1, PhysicalCompoundId::C5);
    EXPECT_TRUE(c5_res.ok(), "loadCompoundSpec(C5) failed");
    EXPECT_EQ(c5_res.value->hardness_rank, 5, "C5 hardness_rank must be 5");

    return true;
}

// ============================================================================
// TEST 3: Null/uncalibrated values remain unknown (std::nullopt), never 0.0
// ============================================================================
bool testNullAndUncalibratedValuesPreservedHonestly(const fs::path& real_data_root) {
    DataAccessLayer dal(real_data_root);
    Result<EventBundle> res = dal.loadEventBundle("2026_british_gp");
    EXPECT_TRUE(res.ok(), "Expected bundle load to succeed even when calibration parameters are null");
    EXPECT_TRUE(res.hasWarnings(), "Expected warnings for null/unknown and assumed fields");
    EXPECT_TRUE(hasDiagnosticWithCode(res.diagnostics, DiagnosticCode::LegitimateUnknownValue),
                "Expected LegitimateUnknownValue diagnostic code in warnings");

    const EventBundle& b = *res.value;

    EXPECT_TRUE(!b.track_layout.sector1_length_m.has_value(), "sector1_length_m must be nullopt");
    EXPECT_TRUE(!b.track_layout.sector2_length_m.has_value(), "sector2_length_m must be nullopt");
    EXPECT_TRUE(!b.track_layout.sector3_length_m.has_value(), "sector3_length_m must be nullopt");
    EXPECT_TRUE(!b.track_layout.hasCompleteSectorLengths(), "hasCompleteSectorLengths must be false");

    EXPECT_TRUE(!b.track_layout.frac_lap_straight.has_value(), "frac_lap_straight must be nullopt");
    EXPECT_TRUE(!b.track_layout.frac_lap_corner_slow.has_value(), "frac_lap_corner_slow must be nullopt");
    EXPECT_TRUE(!b.track_layout.frac_lap_corner_med.has_value(), "frac_lap_corner_med must be nullopt");
    EXPECT_TRUE(!b.track_layout.frac_lap_corner_high.has_value(), "frac_lap_corner_high must be nullopt");
    EXPECT_TRUE(!b.track_layout.hasCompleteSpeedBandFractions(), "hasCompleteSpeedBandFractions must be false");

    EXPECT_TRUE(!b.track_surface.bumpiness_index_score.has_value(), "bumpiness_index_score must be nullopt");
    EXPECT_NEAR(b.track_surface.track_evolution_amplitude_s, 0.0, 1e-12, "track_evolution_amplitude_s is explicit 0.0");

    EXPECT_TRUE(!b.event_config.max_stint_length_laps.has_value(), "max_stint_length_laps must be nullopt");

    const PhysicalCompoundSpec* c1 = b.compound_catalogue.findCompound(PhysicalCompoundId::C1);
    const PhysicalCompoundSpec* c2 = b.compound_catalogue.findCompound(PhysicalCompoundId::C2);
    const PhysicalCompoundSpec* c3 = b.compound_catalogue.findCompound(PhysicalCompoundId::C3);
    EXPECT_TRUE(c1 != nullptr && c2 != nullptr && c3 != nullptr, "C1..C3 must exist");

    EXPECT_TRUE(!c1->pace_offset_ref_s.has_value(), "C1 pace_offset_ref_s must remain nullopt");
    EXPECT_TRUE(!c2->pace_offset_ref_s.has_value(), "C2 pace_offset_ref_s must remain nullopt");
    EXPECT_TRUE(c3->pace_offset_ref_s.has_value(), "C3 pace_offset_ref_s is defined as 0.0 s");
    EXPECT_NEAR(*c3->pace_offset_ref_s, 0.0, 1e-12, "C3 pace_offset_ref_s must be 0.0");

    for (const auto& comp : b.compound_catalogue.compounds) {
        EXPECT_TRUE(!comp.base_wear_rate_per_lap.has_value(), "base_wear_rate_per_lap must be nullopt");
        EXPECT_TRUE(!comp.linear_deg_coeff_s.has_value(), "linear_deg_coeff_s must be nullopt");
        EXPECT_TRUE(!comp.opt_temp_window_min_c.has_value(), "opt_temp_window_min_c must be nullopt");
        EXPECT_TRUE(!comp.opt_temp_window_max_c.has_value(), "opt_temp_window_max_c must be nullopt");
        EXPECT_TRUE(!comp.hasBaselineLinearCalibration(), "hasBaselineLinearCalibration must be false");
    }

    auto prov_it = c1->field_provenance.find("calibrated_pace_and_wear_coefficients");
    EXPECT_TRUE(prov_it != c1->field_provenance.end(), "Expected provenance entry for calibrated coefficients");
    EXPECT_EQ(prov_it->second.source_status, SourceStatus::UnknownUnavailable, "Expected UNKNOWN_UNAVAILABLE status");
    EXPECT_EQ(prov_it->second.confidence_tier, ConfidenceTier::Provisional, "Expected PROVISIONAL tier");
    EXPECT_EQ(prov_it->second.missing_policy, MissingPolicy::RejectSimulation, "Expected REJECT_SIMULATION policy");

    EXPECT_TRUE(!b.areNominatedCompoundsCalibratedForBaselineM0(), "Nominal bundle is uncalibrated shell");
    auto suff = dal.checkBaselineSimulationSufficiency(b);
    EXPECT_TRUE(!suff.ok(), "Expected checkBaselineSimulationSufficiency to fail on uncalibrated shell");
    EXPECT_EQ(suff.status, LoadStatus::InsufficientCapabilityData, "Expected InsufficientCapabilityData status");
    EXPECT_TRUE(hasDiagnosticWithCode(suff.diagnostics, DiagnosticCode::InsufficientDataForCapability),
                "Expected InsufficientDataForCapability diagnostic code");
    EXPECT_EQ(suff.diagnostics.size(), std::size_t(8),
              "Expected 8 missing baseline fields across C1 (3), C2 (3), and C3 (2)");

    return true;
}

// ============================================================================
// TEST 4: Reject path traversal, out-of-root paths, symlinks, and invalid IDs
// ============================================================================
bool testPathTraversalAndRootConfinement(const fs::path& real_data_root) {
    DataAccessLayer dal(real_data_root);

    auto r1 = dal.loadEventBundle("../research/manifests/2026_silverstone_v1");
    EXPECT_TRUE(!r1.ok(), "Traversal identifier must fail");
    EXPECT_EQ(r1.status, LoadStatus::PathOutsideRoot, "Expected PathOutsideRoot");
    EXPECT_TRUE(hasDiagnosticWithCode(r1.diagnostics, DiagnosticCode::PathOutsideDataRoot),
                "Expected PathOutsideDataRoot code");

    auto r2 = dal.loadEventConfiguration("/etc/passwd");
    EXPECT_TRUE(!r2.ok(), "Absolute path identifier must fail");
    EXPECT_EQ(r2.status, LoadStatus::PathOutsideRoot, "Expected PathOutsideRoot");

    auto r3 = dal.loadTrackLayout("tracks/../../etc/passwd", 2011);
    EXPECT_TRUE(!r3.ok(), "Traversal track_id must fail");
    EXPECT_EQ(r3.status, LoadStatus::PathOutsideRoot, "Expected PathOutsideRoot");

    auto r4 = dal.resolver().verifyRelativePathUnderRoot(
        "research/manifests/2026_silverstone_v1.json", "manifest");
    EXPECT_TRUE(!r4.ok(), "Non-runtime subdirectory must be rejected");
    EXPECT_EQ(r4.status, LoadStatus::PathOutsideRoot, "Expected PathOutsideRoot");

    auto r5 = dal.loadEventBundle("");
    EXPECT_TRUE(!r5.ok(), "Empty identifier must fail");
    EXPECT_EQ(r5.status, LoadStatus::InvalidRequest, "Expected InvalidRequest");
    EXPECT_TRUE(hasDiagnosticWithCode(r5.diagnostics, DiagnosticCode::InvalidRequestIdentifier),
                "Expected InvalidRequestIdentifier code");

    auto r6 = dal.loadEventBundle("2026-British-GP!");
    EXPECT_TRUE(!r6.ok(), "Uppercase/punctuation identifier must fail");
    EXPECT_EQ(r6.status, LoadStatus::InvalidRequest, "Expected InvalidRequest");

    ScopedTempDataRoot temp_env("symlink_escape", real_data_root);
    fs::path outside_file = fs::temp_directory_path() / "f1_sim_outside_secret.json";
    writeTextFile(outside_file, "{\"schema_name\":\"EventConfiguration\",\"schema_version\":\"1.0.0\"}");

    std::error_code ec;
    fs::create_symlink(outside_file, temp_env.path() / "events" / "escaped_gp_config.json", ec);
    if (!ec) {
        DataAccessLayer temp_dal(temp_env.path());
        auto sym_res = temp_dal.loadEventConfiguration("escaped_gp");
        EXPECT_TRUE(!sym_res.ok(), "Symlink escaping data_root must be rejected");
        EXPECT_EQ(sym_res.status, LoadStatus::PathOutsideRoot, "Expected PathOutsideRoot for escaping symlink");
        EXPECT_TRUE(hasDiagnosticWithCode(sym_res.diagnostics, DiagnosticCode::PathOutsideDataRoot),
                    "Expected PathOutsideDataRoot code for escaping symlink");
    }
    fs::remove(outside_file, ec);

    return true;
}

// ============================================================================
// TEST 5: Missing files, malformed JSON, unsupported schema versions, and
//         structural schema validation failures (using temporary fixtures)
// ============================================================================
bool testMissingMalformedAndSchemaErrors(const fs::path& real_data_root) {
    ScopedTempDataRoot temp_env("schema_errors", real_data_root);
    DataAccessLayer dal(temp_env.path());

    auto missing_res = dal.loadEventBundle("2026_monaco_gp");
    EXPECT_TRUE(!missing_res.ok(), "Missing event bundle must fail");
    EXPECT_EQ(missing_res.status, LoadStatus::FileNotFound, "Expected FileNotFound");
    EXPECT_TRUE(hasDiagnosticWithCode(missing_res.diagnostics, DiagnosticCode::DatasetFileNotFound),
                "Expected DatasetFileNotFound diagnostic");

    writeTextFile(
        temp_env.path() / "events/broken_json_gp_config.json",
        "{\n  \"schema_name\": \"EventConfiguration\",\n  \"schema_version\": \"1.0.0\",\n  \"event_id\": \n}");
    auto malformed_res = dal.loadEventConfiguration("broken_json_gp");
    EXPECT_TRUE(!malformed_res.ok(), "Malformed JSON must fail");
    EXPECT_EQ(malformed_res.status, LoadStatus::MalformedJson, "Expected MalformedJson status");
    EXPECT_TRUE(hasDiagnosticWithCode(malformed_res.diagnostics, DiagnosticCode::MalformedJson),
                "Expected MalformedJson diagnostic");

    std::string cfg_text = readTextFile(temp_env.path() / "events/2026_british_gp_config.json");
    std::string v2_text = replaceAll(cfg_text, "\"schema_version\": \"1.0.0\"", "\"schema_version\": \"2.0.0\"");
    v2_text = replaceAll(v2_text, "\"event_id\": \"2026_british_gp\"", "\"event_id\": \"future_schema_gp\"");
    writeTextFile(temp_env.path() / "events/future_schema_gp_config.json", v2_text);

    auto ver_res = dal.loadEventConfiguration("future_schema_gp");
    EXPECT_TRUE(!ver_res.ok(), "Unsupported schema version must fail");
    EXPECT_EQ(ver_res.status, LoadStatus::UnsupportedSchemaVersion, "Expected UnsupportedSchemaVersion");
    EXPECT_TRUE(hasDiagnosticWithCode(ver_res.diagnostics, DiagnosticCode::UnsupportedSchemaVersion),
                "Expected UnsupportedSchemaVersion diagnostic");

    std::string layout_text = readTextFile(temp_env.path() / "tracks/silverstone_gp_layout_v2011.json");
    std::string bad_layout = replaceAll(layout_text, "\"layout_version\": 2011", "\"layout_version\": 9999");
    bad_layout = replaceAll(bad_layout, "\"lap_length_m\": 5891.0", "\"lap_length_m\": \"5891.0\"");
    writeTextFile(temp_env.path() / "tracks/silverstone_gp_layout_v9999.json", bad_layout);

    auto struct_res = dal.loadTrackLayout("silverstone_gp", 9999);
    EXPECT_TRUE(!struct_res.ok(), "Wrong JSON field type must fail");
    EXPECT_EQ(struct_res.status, LoadStatus::SchemaValidationFailed, "Expected SchemaValidationFailed");
    EXPECT_TRUE(hasDiagnosticWithCode(struct_res.diagnostics, DiagnosticCode::StructuralSchemaFailure),
                "Expected StructuralSchemaFailure diagnostic");

    return true;
}

// ============================================================================
// TEST 6: Semantic range checks and role-mapping bijection invariants
// ============================================================================
bool testSemanticAndRoleMappingInvariants(const fs::path& real_data_root) {
    ScopedTempDataRoot temp_env("semantic_invariants", real_data_root);
    DataAccessLayer dal(temp_env.path());

    std::string layout_text = readTextFile(temp_env.path() / "tracks/silverstone_gp_layout_v2011.json");
    std::string bad_corners = replaceAll(layout_text, "\"corners_total_count\": 18", "\"corners_total_count\": 19");
    bad_corners = replaceAll(bad_corners, "\"layout_version\": 2011", "\"layout_version\": 2012");
    writeTextFile(temp_env.path() / "tracks/silverstone_gp_layout_v2012.json", bad_corners);

    auto corner_res = dal.loadTrackLayout("silverstone_gp", 2012);
    EXPECT_TRUE(!corner_res.ok(), "Broken corner count sum must fail");
    EXPECT_EQ(corner_res.status, LoadStatus::SemanticValidationFailed, "Expected SemanticValidationFailed");
    EXPECT_TRUE(hasDiagnosticWithCode(corner_res.diagnostics, DiagnosticCode::InvalidRangeOrEnum),
                "Expected InvalidRangeOrEnum diagnostic");

    std::string map_text = readTextFile(temp_env.path() / "events/2026_british_gp_role_mapping.json");
    std::string dup_map = replaceAll(map_text, "\"event_id\": \"2026_british_gp\"", "\"event_id\": \"dup_role_gp\"");
    dup_map = replaceAll(dup_map, "\"compound_id\": \"C2\"", "\"compound_id\": \"C1\"");
    writeTextFile(temp_env.path() / "events/dup_role_gp_role_mapping.json", dup_map);

    auto dup_res = dal.loadEventRoleMapping("dup_role_gp");
    EXPECT_TRUE(!dup_res.ok(), "Duplicate compound assignment in role mapping must fail");
    EXPECT_EQ(dup_res.status, LoadStatus::SemanticValidationFailed, "Expected SemanticValidationFailed");

    std::string unnom_map = replaceAll(map_text, "\"event_id\": \"2026_british_gp\"", "\"event_id\": \"unnom_gp\"");
    unnom_map = replaceAll(unnom_map, "\"compound_id\": \"C3\"", "\"compound_id\": \"C4\"");
    unnom_map = replaceAll(unnom_map, "\"hardness_rank\": 3", "\"hardness_rank\": 4");
    writeTextFile(temp_env.path() / "events/unnom_gp_role_mapping.json", unnom_map);

    auto unnom_res = dal.loadEventRoleMapping("unnom_gp");
    EXPECT_TRUE(!unnom_res.ok(), "Un-nominated compound in role mapping must fail");
    EXPECT_EQ(unnom_res.status, LoadStatus::CrossReferenceFailed, "Expected CrossReferenceFailed");
    EXPECT_TRUE(hasDiagnosticWithCode(unnom_res.diagnostics, DiagnosticCode::CrossReferenceFailure),
                "Expected CrossReferenceFailure diagnostic");

    std::string inv_map = replaceAll(map_text, "\"event_id\": \"2026_british_gp\"", "\"event_id\": \"inverted_gp\"");
    inv_map = replaceAll(inv_map, "\"role_id\": \"HARD\"", "\"role_id\": \"TEMP_ROLE\"");
    inv_map = replaceAll(inv_map, "\"role_id\": \"SOFT\"", "\"role_id\": \"HARD\"");
    inv_map = replaceAll(inv_map, "\"role_id\": \"TEMP_ROLE\"", "\"role_id\": \"SOFT\"");
    writeTextFile(temp_env.path() / "events/inverted_gp_role_mapping.json", inv_map);

    auto inv_res = dal.loadEventRoleMapping("inverted_gp");
    EXPECT_TRUE(!inv_res.ok(), "Inverted hardness ordering must fail on official event");
    EXPECT_EQ(inv_res.status, LoadStatus::SemanticValidationFailed, "Expected SemanticValidationFailed");

    return true;
}

// ============================================================================
// TEST 7: Broken cross-dataset references when assembling an EventBundle
// ============================================================================
bool testBrokenCrossDatasetReferences(const fs::path& real_data_root) {
    ScopedTempDataRoot temp_env("broken_xrefs", real_data_root);
    DataAccessLayer dal(temp_env.path());

    std::string map_text = readTextFile(temp_env.path() / "events/2026_british_gp_role_mapping.json");
    std::string mismatch_map = replaceAll(map_text, "\"C3\"", "\"C4\"");
    mismatch_map = replaceAll(mismatch_map, "\"hardness_rank\": 3", "\"hardness_rank\": 4");
    writeTextFile(temp_env.path() / "events/2026_british_gp_role_mapping.json", mismatch_map);

    auto bundle_res = dal.loadEventBundle("2026_british_gp");
    EXPECT_TRUE(!bundle_res.ok(), "Mismatched nominated sets between config and role_mapping must fail");
    EXPECT_EQ(bundle_res.status, LoadStatus::CrossReferenceFailed, "Expected CrossReferenceFailed");
    EXPECT_TRUE(hasDiagnosticWithCode(bundle_res.diagnostics, DiagnosticCode::CrossReferenceFailure),
                "Expected CrossReferenceFailure diagnostic");

    writeTextFile(temp_env.path() / "events/2026_british_gp_role_mapping.json", map_text);
    std::string surf_text = readTextFile(temp_env.path() / "tracks/silverstone_gp_surface_v2019.json");
    std::string bad_surf = replaceAll(surf_text, "\"layout_version\": 2011", "\"layout_version\": 1999");
    writeTextFile(temp_env.path() / "tracks/silverstone_gp_surface_v2019.json", bad_surf);

    auto surf_fk_res = dal.loadEventBundle("2026_british_gp");
    EXPECT_TRUE(!surf_fk_res.ok(), "Surface layout_version FK mismatch must fail");
    EXPECT_EQ(surf_fk_res.status, LoadStatus::CrossReferenceFailed, "Expected CrossReferenceFailed");

    return true;
}

// ============================================================================
// TEST 8: Extensibility to another circuit/event (non-Silverstone) + calibrated
//         bundle sufficiency + immutable snapshot reuse across evaluations
// ============================================================================
bool testNonSilverstoneEventAndSnapshotReuse(const fs::path& real_data_root) {
    ScopedTempDataRoot temp_env("extensibility_and_reuse", real_data_root);

    std::string layout_text = readTextFile(temp_env.path() / "tracks/silverstone_gp_layout_v2011.json");
    layout_text = replaceAll(layout_text, "\"track_id\": \"silverstone_gp\"", "\"track_id\": \"spa_francorchamps\"");
    layout_text = replaceAll(layout_text, "\"layout_version\": 2011", "\"layout_version\": 2022");
    layout_text = replaceAll(layout_text, "\"track_name\": \"Silverstone Circuit\"", "\"track_name\": \"Circuit de Spa-Francorchamps\"");
    layout_text = replaceAll(layout_text, "\"lap_length_m\": 5891.0", "\"lap_length_m\": 7004.0");
    writeTextFile(temp_env.path() / "tracks/spa_francorchamps_layout_v2022.json", layout_text);

    std::string surf_text = readTextFile(temp_env.path() / "tracks/silverstone_gp_surface_v2019.json");
    surf_text = replaceAll(surf_text, "\"track_id\": \"silverstone_gp\"", "\"track_id\": \"spa_francorchamps\"");
    surf_text = replaceAll(surf_text, "\"layout_version\": 2011", "\"layout_version\": 2022");
    surf_text = replaceAll(surf_text, "\"surface_version\": 2019", "\"surface_version\": 2024");
    writeTextFile(temp_env.path() / "tracks/spa_francorchamps_surface_v2024.json", surf_text);

    std::string cfg_text = readTextFile(temp_env.path() / "events/2026_british_gp_config.json");
    cfg_text = replaceAll(cfg_text, "\"event_id\": \"2026_british_gp\"", "\"event_id\": \"2026_belgian_gp\"");
    cfg_text = replaceAll(cfg_text, "\"track_id\": \"silverstone_gp\"", "\"track_id\": \"spa_francorchamps\"");
    cfg_text = replaceAll(cfg_text, "\"layout_version\": 2011", "\"layout_version\": 2022");
    cfg_text = replaceAll(cfg_text, "\"surface_version\": 2019", "\"surface_version\": 2024");
    cfg_text = replaceAll(cfg_text, "\"race_distance_laps\": 52", "\"race_distance_laps\": 44");
    cfg_text = replaceAll(cfg_text, "\"scheduled_race_distance_km\": 306.198", "\"scheduled_race_distance_km\": 308.052");
    cfg_text = replaceAll(cfg_text, "\"C1\",\n    \"C2\",\n    \"C3\"", "\"C2\",\n    \"C4\",\n    \"C5\"");
    writeTextFile(temp_env.path() / "events/2026_belgian_gp_config.json", cfg_text);

    std::string map_text = readTextFile(temp_env.path() / "events/2026_british_gp_role_mapping.json");
    map_text = replaceAll(map_text, "\"2026_british_gp\"", "\"2026_belgian_gp\"");
    map_text = replaceAll(map_text, "\"C1\",\n    \"C2\",\n    \"C3\"", "\"C2\",\n    \"C4\",\n    \"C5\"");
    map_text = replaceAll(map_text, "\"compound_id\": \"C3\"", "\"compound_id\": \"C5\"");
    map_text = replaceAll(map_text, "\"hardness_rank\": 3", "\"hardness_rank\": 5");
    map_text = replaceAll(map_text, "\"compound_id\": \"C2\"", "\"compound_id\": \"C4\"");
    map_text = replaceAll(map_text, "\"hardness_rank\": 2", "\"hardness_rank\": 4");
    map_text = replaceAll(map_text, "\"compound_id\": \"C1\"", "\"compound_id\": \"C2\"");
    map_text = replaceAll(map_text, "\"hardness_rank\": 1", "\"hardness_rank\": 2");
    writeTextFile(temp_env.path() / "events/2026_belgian_gp_role_mapping.json", map_text);

    std::string cat_text = readTextFile(temp_env.path() / "compounds/2026_pirelli_slick_catalogue_v1.json");
    cat_text = replaceAll(
        cat_text,
        "\"compound_id\": \"C1\",\n      \"hardness_rank\": 1,\n      \"description\": \"Hardest 2026 P Zero dry slick compound; extreme high-load durability (deployed at Suzuka and Silverstone).\",\n      \"front_tyre_width_reduction_vs_2025_mm\": 25.0,\n      \"rear_tyre_width_reduction_vs_2025_mm\": 30.0,\n      \"front_outer_diameter_reduction_vs_2025_mm\": 15.0,\n      \"rear_outer_diameter_reduction_vs_2025_mm\": 10.0,\n      \"wheel_rim_diameter_in\": 18,\n      \"pace_offset_ref_s\": null,\n      \"base_wear_rate_per_lap\": null,\n      \"linear_deg_coeff_s\": null,",
        "\"compound_id\": \"C1\",\n      \"hardness_rank\": 1,\n      \"description\": \"Hardest\",\n      \"pace_offset_ref_s\": 0.90,\n      \"base_wear_rate_per_lap\": 0.015,\n      \"linear_deg_coeff_s\": 4.0,");
    cat_text = replaceAll(
        cat_text,
        "\"compound_id\": \"C2\",\n      \"hardness_rank\": 2,\n      \"description\": \"Second-hardest 2026 P Zero dry slick compound; versatile high-energy race compound.\",\n      \"front_tyre_width_reduction_vs_2025_mm\": 25.0,\n      \"rear_tyre_width_reduction_vs_2025_mm\": 30.0,\n      \"front_outer_diameter_reduction_vs_2025_mm\": 15.0,\n      \"rear_outer_diameter_reduction_vs_2025_mm\": 10.0,\n      \"wheel_rim_diameter_in\": 18,\n      \"pace_offset_ref_s\": null,\n      \"base_wear_rate_per_lap\": null,\n      \"linear_deg_coeff_s\": null,",
        "\"compound_id\": \"C2\",\n      \"hardness_rank\": 2,\n      \"description\": \"Second-hardest\",\n      \"pace_offset_ref_s\": 0.45,\n      \"base_wear_rate_per_lap\": 0.020,\n      \"linear_deg_coeff_s\": 4.5,");
    cat_text = replaceAll(
        cat_text,
        "\"compound_id\": \"C3\",\n      \"hardness_rank\": 3,\n      \"description\": \"Mid-range 2026 P Zero dry slick reference compound (c_ref); balanced grip and mechanical consistency.\",\n      \"front_tyre_width_reduction_vs_2025_mm\": 25.0,\n      \"rear_tyre_width_reduction_vs_2025_mm\": 30.0,\n      \"front_outer_diameter_reduction_vs_2025_mm\": 15.0,\n      \"rear_outer_diameter_reduction_vs_2025_mm\": 10.0,\n      \"wheel_rim_diameter_in\": 18,\n      \"pace_offset_ref_s\": 0.0,\n      \"base_wear_rate_per_lap\": null,\n      \"linear_deg_coeff_s\": null,",
        "\"compound_id\": \"C3\",\n      \"hardness_rank\": 3,\n      \"description\": \"Reference\",\n      \"pace_offset_ref_s\": 0.0,\n      \"base_wear_rate_per_lap\": 0.030,\n      \"linear_deg_coeff_s\": 5.0,");
    cat_text = replaceAll(
        cat_text,
        "\"compound_id\": \"C4\",\n      \"hardness_rank\": 4,\n      \"description\": \"Second-softest 2026 P Zero dry slick compound; rapid warm-up for medium/low-severity circuits.\",\n      \"front_tyre_width_reduction_vs_2025_mm\": 25.0,\n      \"rear_tyre_width_reduction_vs_2025_mm\": 30.0,\n      \"front_outer_diameter_reduction_vs_2025_mm\": 15.0,\n      \"rear_outer_diameter_reduction_vs_2025_mm\": 10.0,\n      \"wheel_rim_diameter_in\": 18,\n      \"pace_offset_ref_s\": null,\n      \"base_wear_rate_per_lap\": null,\n      \"linear_deg_coeff_s\": null,",
        "\"compound_id\": \"C4\",\n      \"hardness_rank\": 4,\n      \"description\": \"Second-softest\",\n      \"pace_offset_ref_s\": -0.50,\n      \"base_wear_rate_per_lap\": 0.045,\n      \"linear_deg_coeff_s\": 5.5,");
    cat_text = replaceAll(
        cat_text,
        "\"compound_id\": \"C5\",\n      \"hardness_rank\": 5,\n      \"description\": \"Softest 2026 P Zero dry slick compound; maximum mechanical grip for street and low-energy circuits.\",\n      \"front_tyre_width_reduction_vs_2025_mm\": 25.0,\n      \"rear_tyre_width_reduction_vs_2025_mm\": 30.0,\n      \"front_outer_diameter_reduction_vs_2025_mm\": 15.0,\n      \"rear_outer_diameter_reduction_vs_2025_mm\": 10.0,\n      \"wheel_rim_diameter_in\": 18,\n      \"pace_offset_ref_s\": null,\n      \"base_wear_rate_per_lap\": null,\n      \"linear_deg_coeff_s\": null,",
        "\"compound_id\": \"C5\",\n      \"hardness_rank\": 5,\n      \"description\": \"Softest\",\n      \"pace_offset_ref_s\": -0.95,\n      \"base_wear_rate_per_lap\": 0.065,\n      \"linear_deg_coeff_s\": 6.0,");
    writeTextFile(temp_env.path() / "compounds/2026_pirelli_slick_catalogue_v1.json", cat_text);

    DataAccessLayer dal(temp_env.path());
    Result<EventBundle> spa_res = dal.loadEventBundle("2026_belgian_gp");
    EXPECT_TRUE(spa_res.ok(), "Expected non-Silverstone 2026_belgian_gp bundle to load cleanly");

    const EventBundle snapshot = *spa_res.value;
    EXPECT_TRUE(snapshot.areNominatedCompoundsCalibratedForBaselineM0(),
                "Calibrated fixture bundle should satisfy M0 baseline sufficiency");
    auto suff = dal.checkBaselineSimulationSufficiency(snapshot);
    EXPECT_TRUE(suff.ok(), "Expected checkBaselineSimulationSufficiency to pass on calibrated fixture");

    std::error_code ec;
    fs::remove_all(temp_env.path(), ec);

    double accumulated_check = 0.0;
    for (int eval = 0; eval < 250; ++eval) {
        const PhysicalCompoundSpec* h = snapshot.compoundForRole(TyreRole::Hard);
        const PhysicalCompoundSpec* m = snapshot.compoundForRole(TyreRole::Medium);
        const PhysicalCompoundSpec* s = snapshot.compoundForRole(TyreRole::Soft);
        EXPECT_TRUE(h != nullptr && m != nullptr && s != nullptr, "Snapshot role lookup must remain valid");
        EXPECT_EQ(h->compound_id, PhysicalCompoundId::C2, "Spa HARD -> C2");
        EXPECT_EQ(m->compound_id, PhysicalCompoundId::C4, "Spa MEDIUM -> C4");
        EXPECT_EQ(s->compound_id, PhysicalCompoundId::C5, "Spa SOFT -> C5");
        accumulated_check += (*h->pace_offset_ref_s + *m->pace_offset_ref_s + *s->pace_offset_ref_s);
    }
    EXPECT_NEAR(accumulated_check, 250.0 * (0.45 - 0.50 - 0.95), 1e-9,
                "Snapshot values must remain deterministic across 250 strategy evaluations");

    return true;
}

// ============================================================================
// TEST 9: Engine-reported runtime discrepancies (two-way diagnostics without
//         auto-repair or source file mutation)
// ============================================================================
bool testEngineReportedDiscrepancyFeedback(const fs::path& real_data_root) {
    auto collector = std::make_shared<InMemoryDiagnosticCollector>();
    DataAccessLayer dal(real_data_root, collector);

    auto bundle_res = dal.loadEventBundle("2026_british_gp");
    EXPECT_TRUE(bundle_res.ok(), "Bundle load should succeed");
    const std::size_t initial_count = collector->size();
    EXPECT_TRUE(initial_count > 0, "Expected load warnings recorded in collector");

    EngineDiscrepancyReport report;
    report.severity = Severity::Warning;
    report.code = DiagnosticCode::EngineReportedDiscrepancy;
    report.dataset_id = "2026_british_gp";
    report.relative_path = "events/2026_british_gp_config.json";
    report.field_path = "/available_sets_soft";
    report.message = "Driver pre-race Sunday allocation has only 3 remaining Soft sets vs 6 weekend total.";
    report.expected = ">= 4 new Soft sets for 3-stop Soft strategy";
    report.actual = "3 used Soft sets";

    Diagnostic recorded = dal.reportEngineDiscrepancy(report);
    EXPECT_EQ(recorded.code, DiagnosticCode::EngineReportedDiscrepancy, "Discrepancy code mismatch");
    EXPECT_EQ(collector->size(), initial_count + 1, "Collector should append exactly 1 diagnostic");

    auto all_diags = collector->snapshot();
    EXPECT_EQ(all_diags.back().dataset_id, std::string("2026_british_gp"), "Recorded dataset_id mismatch");
    EXPECT_EQ(all_diags.back().field_path, std::string("/available_sets_soft"), "Recorded field_path mismatch");

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
    std::cout << "F1 Simulator Data Access Layer (DAL) Verification Suite\n";
    std::cout << "Configured data_root: " << data_root.generic_string() << "\n";
    std::cout << "====================================================================\n";

    runTest("Test01_LoadActualSilverstoneEventBundle", testLoadActualSilverstoneEventBundle, data_root);
    runTest("Test02_IndividualDatasetQueries", testIndividualDatasetQueries, data_root);
    runTest("Test03_NullAndUncalibratedValuesPreservedHonestly", testNullAndUncalibratedValuesPreservedHonestly, data_root);
    runTest("Test04_PathTraversalAndRootConfinement", testPathTraversalAndRootConfinement, data_root);
    runTest("Test05_MissingMalformedAndSchemaErrors", testMissingMalformedAndSchemaErrors, data_root);
    runTest("Test06_SemanticAndRoleMappingInvariants", testSemanticAndRoleMappingInvariants, data_root);
    runTest("Test07_BrokenCrossDatasetReferences", testBrokenCrossDatasetReferences, data_root);
    runTest("Test08_NonSilverstoneEventAndSnapshotReuse", testNonSilverstoneEventAndSnapshotReuse, data_root);
    runTest("Test09_EngineReportedDiscrepancyFeedback", testEngineReportedDiscrepancyFeedback, data_root);

    std::cout << "====================================================================\n";
    std::cout << "Tests run: " << g_tests_run << ", Failures: " << g_tests_failed << "\n";
    std::cout << "====================================================================\n";

    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}