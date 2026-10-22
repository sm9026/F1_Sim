#include "f1_sim/data_access/data_access_layer.hpp"
#include "f1_sim/simulation/race_session.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace f1_sim::data_access;
using namespace f1_sim::simulation;

namespace {

// Process exit codes (Section 3.8)
constexpr int kExitOk              = 0;
constexpr int kExitCliUsageError   = 1;
constexpr int kExitDataLoadError   = 2;
constexpr int kExitValidationError = 3;
constexpr int kExitSimulationError = 4;

struct CliOptions {
    bool show_help = false;
    bool check_only = false;
    bool strict_calibrated = false;
    bool verbose_laps = false;
    bool explicit_data_root = false;
    fs::path data_root;
    std::string event_id = "2026_british_gp";
    RaceMode race_mode = RaceMode::Preplanned;
    ModelStage model_stage = ModelStage::M1_TrackAwareCalibratedDry;
    double driver_mass_kg = 75.0;
};

void printUsage(const char* prog_name) {
    const std::string bin = (prog_name != nullptr && prog_name[0] != '\0') ? prog_name : "f1_sim";
    std::cout
        << "F1_Sim — Formula 1 Race Strategy Simulator CLI\n"
        << "==============================================\n\n"
        << "Usage:\n"
        << "  " << bin << " [options]\n\n"
        << "Core Options:\n"
        << "  --help, -h                  Display this help message and exit (code 0).\n"
        << "  --data-root <path>          Path to local read-only runtime dataset directory\n"
        << "                              (defaults to './data' or '../data' relative to CWD/binary).\n"
        << "  --event <event_id>          Event identifier to load via DAL (default: 2026_british_gp).\n"
        << "  --mode <preplanned|interactive>\n"
        << "                              Simulation operating mode (default: preplanned).\n"
        << "  --stage <m0|m1|m2|m3>       Mathematical framework model stage (default: m1).\n"
        << "  --driver-mass <kg>          Driver mass in kg within [40.0, 160.0] (default: 75.0).\n"
        << "  --check-only                Load dataset via DAL, report raw calibration readiness\n"
        << "                              and parameter gaps, and exit without running a race.\n"
        << "  --strict-calibrated         Require strictly CALIBRATED parameters (blocks execution\n"
        << "                              with exit code 3 if compound catalogue is uncalibrated).\n"
        << "  --verbose-laps              Print all lap trace entries instead of milestone laps.\n\n"
        << "Default Smoke-Test Scenario:\n"
        << "  1. Resolves the local 'data/' directory and loads '2026_british_gp' (Silverstone, 52 laps)\n"
        << "     exclusively through f1_sim::data_access::DataAccessLayer.\n"
        << "  2. Audits the raw on-disk dataset readiness via checkBaselineSimulationSufficiency()\n"
        << "     and RaceSession::assessReadiness(), reporting all uncalibrated (null) fields honestly.\n"
        << "  3. Configures a declared lower-fidelity (ESTIMATED) parameter supplement for C1/C2/C3\n"
        << "     and executes a deterministic 52-lap 1-stop race ((SOFT, 18 laps), (HARD, 34 laps))\n"
        << "     through f1_sim::simulation::RaceSession.\n\n"
        << "Exit Codes:\n"
        << "  0  Success (simulation completed or --help / --check-only succeeded)\n"
        << "  1  Invalid command-line arguments\n"
        << "  2  Data Access Layer (DAL) dataset resolution, loading, or schema validation failure\n"
        << "  3  Simulation readiness, calibration gate, or strategy validation failure\n"
        << "  4  Runtime simulation execution failure\n";
}

bool parseCliArgs(int argc, char** argv, CliOptions& out_opts, std::string& out_error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out_opts.show_help = true;
            return true;
        } else if (arg == "--check-only") {
            out_opts.check_only = true;
        } else if (arg == "--strict-calibrated") {
            out_opts.strict_calibrated = true;
        } else if (arg == "--verbose-laps") {
            out_opts.verbose_laps = true;
        } else if (arg == "--data-root") {
            if (i + 1 >= argc) {
                out_error = "Missing path argument after '--data-root'.";
                return false;
            }
            out_opts.data_root = argv[++i];
            out_opts.explicit_data_root = true;
        } else if (arg.rfind("--data-root=", 0) == 0) {
            out_opts.data_root = arg.substr(std::string("--data-root=").size());
            out_opts.explicit_data_root = true;
        } else if (arg == "--event") {
            if (i + 1 >= argc) {
                out_error = "Missing event_id argument after '--event'.";
                return false;
            }
            out_opts.event_id = argv[++i];
        } else if (arg.rfind("--event=", 0) == 0) {
            out_opts.event_id = arg.substr(std::string("--event=").size());
        } else if (arg == "--mode") {
            if (i + 1 >= argc) {
                out_error = "Missing mode argument after '--mode' (expected 'preplanned' or 'interactive').";
                return false;
            }
            const std::string val = argv[++i];
            if (val == "preplanned" || val == "PREPLANNED") {
                out_opts.race_mode = RaceMode::Preplanned;
            } else if (val == "interactive" || val == "INTERACTIVE") {
                out_opts.race_mode = RaceMode::Interactive;
            } else {
                out_error = "Invalid '--mode' value '" + val + "' (expected 'preplanned' or 'interactive').";
                return false;
            }
        } else if (arg == "--stage") {
            if (i + 1 >= argc) {
                out_error = "Missing stage argument after '--stage' (expected m0, m1, m2, or m3).";
                return false;
            }
            const std::string val = argv[++i];
            if (val == "m0" || val == "M0") {
                out_opts.model_stage = ModelStage::M0_DeterministicBaseline;
            } else if (val == "m1" || val == "M1") {
                out_opts.model_stage = ModelStage::M1_TrackAwareCalibratedDry;
            } else if (val == "m2" || val == "M2") {
                out_opts.model_stage = ModelStage::M2_DynamicEnvironmentDry;
            } else if (val == "m3" || val == "M3") {
                out_opts.model_stage = ModelStage::M3_NonlinearCliffAndFourCorner;
            } else {
                out_error = "Invalid '--stage' value '" + val + "' (expected m0, m1, m2, or m3).";
                return false;
            }
        } else if (arg == "--driver-mass") {
            if (i + 1 >= argc) {
                out_error = "Missing numeric value (kg) after '--driver-mass'.";
                return false;
            }
            const std::string val = argv[++i];
            char* end_ptr = nullptr;
            const double parsed = std::strtod(val.c_str(), &end_ptr);
            if (end_ptr == val.c_str() || *end_ptr != '\0' || !std::isfinite(parsed)) {
                out_error = "Invalid numeric value '" + val + "' for '--driver-mass'.";
                return false;
            }
            out_opts.driver_mass_kg = parsed;
        } else {
            out_error = "Unknown command-line argument '" + arg + "'. Run with '--help' for usage.";
            return false;
        }
    }
    return true;
}

bool isLikelyDataRoot(const fs::path& candidate) noexcept {
    std::error_code ec;
    return fs::exists(candidate, ec) && !ec &&
           fs::is_directory(candidate, ec) && !ec &&
           fs::is_directory(candidate / "events", ec) && !ec &&
           fs::is_directory(candidate / "tracks", ec) && !ec &&
           fs::is_directory(candidate / "compounds", ec) && !ec;
}

/**
 * @brief Locate the repository's local `data/` directory when `--data-root` is not
 *        explicitly provided on the command line.
 *
 * If `--data-root` was explicitly provided by the user, returns it verbatim so that
 * invalid explicit paths fail deterministically without falling back to another folder.
 */
fs::path resolveDefaultDataRoot(const char* argv0) {
    std::vector<fs::path> candidates;
    candidates.emplace_back("data");
    candidates.emplace_back(fs::path("..") / "data");
    candidates.emplace_back(fs::path("..") / ".." / "data");

    if (argv0 != nullptr && argv0[0] != '\0') {
        std::error_code ec;
        fs::path exe_path = fs::weakly_canonical(fs::path(argv0), ec);
        if (!ec && !exe_path.empty()) {
            fs::path exe_dir = exe_path.parent_path();
            if (!exe_dir.empty()) {
                candidates.push_back(exe_dir / "data");
                candidates.push_back(exe_dir.parent_path() / "data");
                candidates.push_back(exe_dir.parent_path().parent_path() / "data");
            }
        }
    }

    for (const auto& cand : candidates) {
        if (isLikelyDataRoot(cand)) {
            return cand;
        }
    }
    return fs::path("data");
}

std::string formatRaceDuration(double total_seconds) {
    if (!std::isfinite(total_seconds) || total_seconds < 0.0) {
        return "N/A";
    }
    const int hours = static_cast<int>(total_seconds / 3600.0);
    const double rem_after_hours = total_seconds - static_cast<double>(hours) * 3600.0;
    const int minutes = static_cast<int>(rem_after_hours / 60.0);
    const double seconds = rem_after_hours - static_cast<double>(minutes) * 60.0;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << ":" << std::setw(2) << std::setfill('0') << minutes << ":";
    } else {
        oss << minutes << ":";
    }
    oss << std::setw(6) << std::setfill('0') << std::fixed << std::setprecision(3) << seconds;
    return oss.str();
}

void printDiagnosticsList(
    std::ostream& os,
    const std::vector<Diagnostic>& diags,
    bool errors_only = false,
    const std::string& indent = "  ") {
    for (const auto& d : diags) {
        if (errors_only && d.severity != Severity::Error) {
            continue;
        }
        os << indent << "[" << toString(d.severity) << " | " << toString(d.code) << "] ";
        if (!d.relative_path.empty()) {
            os << d.relative_path << " ";
        }
        if (!d.field_path.empty()) {
            os << "(" << d.field_path << ") ";
        }
        os << d.message;
        if (!d.expected.empty() || !d.actual.empty()) {
            os << " [expected: " << d.expected << ", actual: " << d.actual << "]";
        }
        os << "\n";
    }
}

/**
 * @brief Construct declared lower-fidelity (ESTIMATED) model parameters for the
 *        2026 British Grand Prix smoke test.
 *
 * As documented in `data/compounds/2026_pirelli_slick_catalogue_v1.json` and
 * `docs/architecture/program_blueprint.md` Section 14, the on-disk 2026 compound
 * catalogue is an uncalibrated structural shell (`null` wear/degradation coefficients).
 * Rather than mutating `data/` or pretending estimated values are empirically calibrated,
 * these parameters are explicitly tagged with `SourceStatus::Estimated`, which
 * `StrategyAndReadinessValidator` classifies as `FidelityStatus::EstimatedOrAssumedLowerFidelity`.
 */
ModelParameters makeDeclaredLowerFidelitySmokeTestParams(bool allow_estimated) {
    ModelParameters params;
    params.param_set_id = "silverstone_2026_smoke_test_estimated_v1";
    params.parameter_source_status = SourceStatus::Estimated;
    params.ref_lap_time_s = 88.500;       // T_ref(r, v) in seconds
    params.pit_loss_total_s = 21.000;     // P_ref(r) net incremental pit-stop loss in seconds
    params.reference_stationary_time_s = 2.40;
    params.configured_stationary_time_s = 2.40;
    params.fuel_lap_sens_s_per_kg = 0.032;
    params.lap1_standing_start_loss_s = 3.50;
    params.outlap_warmup_loss_s = 0.50;
    params.grip_scaling_psi = 1.00;
    params.abrasiveness_sensitivity_omega = 0.20;
    params.allow_estimated_or_assumed_parameters = allow_estimated;

    auto addEstimatedCompound = [&](PhysicalCompoundId cid, double pace_s, double wear_rate, double alpha_s) {
        CompoundParameterOverride ov;
        ov.compound_id = cid;
        ov.source_status = SourceStatus::Estimated;
        ov.pace_offset_ref_s = pace_s;
        ov.base_wear_rate_per_lap = wear_rate;
        ov.linear_deg_coeff_s = alpha_s;
        params.compound_overrides[cid] = ov;
    };

    // Silverstone 2026 nominated triplet: HARD -> C1, MEDIUM -> C2, SOFT -> C3
    addEstimatedCompound(PhysicalCompoundId::C1, +0.700, 0.012, 4.000);
    addEstimatedCompound(PhysicalCompoundId::C2, +0.350, 0.018, 4.500);
    addEstimatedCompound(PhysicalCompoundId::C3,  0.000, 0.025, 5.000);
    // Also populate C4 and C5 in case a custom event nominates softer compounds
    addEstimatedCompound(PhysicalCompoundId::C4, -0.450, 0.034, 5.500);
    addEstimatedCompound(PhysicalCompoundId::C5, -0.850, 0.045, 6.000);

    return params;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions opts;
    std::string cli_error;
    if (!parseCliArgs(argc, argv, opts, cli_error)) {
        std::cerr << "[CLI ERROR] " << cli_error << "\n";
        return kExitCliUsageError;
    }

    if (opts.show_help) {
        printUsage(argc > 0 ? argv[0] : "f1_sim");
        return kExitOk;
    }

    const fs::path effective_data_root = opts.explicit_data_root
        ? opts.data_root
        : resolveDefaultDataRoot(argc > 0 ? argv[0] : nullptr);

    std::cout << "================================================================================\n";
    std::cout << "F1_Sim — Formula 1 Strategy Simulator CLI Integration Milestone\n";
    std::cout << "================================================================================\n";
    std::cout << "Configured Data Root : " << effective_data_root.generic_string()
              << (opts.explicit_data_root ? " (explicit --data-root)" : " (auto-discovered)") << "\n";
    std::cout << "Target Event ID      : " << opts.event_id << "\n";
    std::cout << "Operating Mode       : " << toString(opts.race_mode) << "\n";
    std::cout << "Model Stage          : " << toString(opts.model_stage) << "\n";
    std::cout << "Driver Mass          : " << std::fixed << std::setprecision(1)
              << opts.driver_mass_kg << " kg\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout.flush();

    // 1. Initialise DAL and verify data_root confinement
    auto diag_collector = std::make_shared<InMemoryDiagnosticCollector>();
    auto dal = std::make_shared<DataAccessLayer>(effective_data_root, diag_collector);

    if (!dal->resolver().isRootValid()) {
        std::cerr << "[DAL ERROR] Invalid local data_root directory: "
                  << effective_data_root.generic_string() << "\n";
        if (dal->resolver().rootError().has_value()) {
            printDiagnosticsList(std::cerr, {*dal->resolver().rootError()});
        }
        return kExitDataLoadError;
    }

    // 2. Load and cross-validate the EventBundle via RaceSession -> DataAccessLayer
    RaceSession session(dal);
    auto select_res = session.selectEvent(opts.event_id);
    if (!select_res.ok() || session.eventBundle() == nullptr) {
        std::cerr << "[DAL ERROR] Failed to load event bundle '" << opts.event_id
                  << "' (EngineStatus: " << toString(select_res.status) << "):\n";
        printDiagnosticsList(std::cerr, select_res.diagnostics, true);
        return kExitDataLoadError;
    }

    const EventBundle& bundle = *session.eventBundle();
    std::cout << "[1/4] DAL EventBundle Loaded & Cross-Validated Successfully\n";
    std::cout << "  Event Name         : " << bundle.event_config.official_event_name
              << " (Round " << bundle.event_config.round_number << ", "
              << bundle.event_config.season_year << ")\n";
    std::cout << "  Competition Format : " << toString(bundle.event_config.competition_format) << "\n";
    std::cout << "  Circuit Layout     : " << bundle.track_layout.track_name
              << " (" << bundle.track_layout.track_id
              << ", layout v" << bundle.track_layout.layout_version
              << ", surface v" << bundle.track_surface.surface_version << ")\n";
    std::cout << "  Lap & Race Distance: " << std::fixed << std::setprecision(1)
              << bundle.track_layout.lap_length_m << " m/lap x "
              << bundle.event_config.race_distance_laps << " laps = "
              << std::setprecision(3) << bundle.event_config.scheduled_race_distance_km << " km\n";
    std::cout << "  Pit Lane Geometry  : " << std::setprecision(1)
              << bundle.track_layout.pit_lane_length_m << " m @ "
              << bundle.track_layout.pit_speed_limit_kmh << " km/h\n";
    std::cout << "  Surface Profile    : abrasiveness=" << bundle.track_surface.surface_abrasiveness_score
              << "/5.0, grip=" << bundle.track_surface.surface_grip_baseline_score
              << "/5.0, evolution_amp=" << bundle.track_surface.track_evolution_amplitude_s << " s\n";
    std::cout << "  Role Mapping (m_e) : ";
    for (std::size_t i = 0; i < bundle.role_mapping.mappings.size(); ++i) {
        const auto& m = bundle.role_mapping.mappings[i];
        std::cout << toString(m.role_id) << " -> " << toString(m.compound_id)
                  << " (" << m.sidewall_colour << ", rank " << m.hardness_rank << ")"
                  << (i + 1 < bundle.role_mapping.mappings.size() ? ", " : "\n");
    }

    // 3. Inspect raw on-disk calibration sufficiency (distinguishing load validity from run readiness)
    const CapabilityReadinessReport& raw_readiness = *select_res.value;
    const Result<void> dal_sufficiency = dal->checkBaselineSimulationSufficiency(bundle);
    std::cout << "\n[2/4] Raw Dataset Calibration & Readiness Audit\n";
    std::cout << "  Bundle Load Valid  : " << (raw_readiness.bundle_load_valid ? "YES" : "NO") << "\n";
    std::cout << "  Catalogue Status   : " << bundle.compound_catalogue.calibration_status << "\n";
    std::cout << "  Raw DAL Sufficiency: " << toString(dal_sufficiency.status)
              << " (" << dal_sufficiency.diagnostics.size() << " uncalibrated compound fields)\n";
    std::cout << "  Raw Engine Fidelity: " << toString(raw_readiness.fidelity_status) << "\n";
    std::cout << "  Raw Execution Gate : " << toString(raw_readiness.execution_permission)
              << " (" << raw_readiness.parameter_gaps.size() << " parameter gaps in raw bundle)\n";

    if (!raw_readiness.parameter_gaps.empty()) {
        std::cout << "  Explicit Parameter Gaps Detected in Raw On-Disk Bundle:\n";
        for (const auto& gap : raw_readiness.parameter_gaps) {
            std::cout << "    - " << gap.field_path << " [" << toString(gap.status)
                      << " -> " << toString(gap.missing_policy) << "]: " << gap.detail << "\n";
        }
    }
    std::cout.flush();

    if (opts.check_only) {
        std::cout << "\n[--check-only] Dataset and readiness inspection complete.\n";
        return kExitOk;
    }

    // 4. Configure simulation inputs via RaceSession public interfaces
    auto mode_res = session.setRaceMode(opts.race_mode);
    auto stage_res = session.setModelStage(opts.model_stage);
    auto mass_res = session.setDriverMassKg(opts.driver_mass_kg);
    if (!mode_res.ok() || !stage_res.ok() || !mass_res.ok()) {
        std::cerr << "[VALIDATION ERROR] Failed to configure session parameters:\n";
        printDiagnosticsList(std::cerr, mode_res.diagnostics, true);
        printDiagnosticsList(std::cerr, stage_res.diagnostics, true);
        printDiagnosticsList(std::cerr, mass_res.diagnostics, true);
        return kExitValidationError;
    }

    if (opts.strict_calibrated) {
        ModelParameters strict_params;
        strict_params.allow_estimated_or_assumed_parameters = false;
        session.setModelParameters(strict_params);
    } else {
        ModelParameters est_params = makeDeclaredLowerFidelitySmokeTestParams(true);
        session.setModelParameters(est_params);
    }

    // Build a deterministic 2-stint (1-stop) strategy summing to race_distance_laps
    const int total_laps = bundle.event_config.race_distance_laps;
    const int stint1_laps = std::max(bundle.event_config.min_stint_length_laps, total_laps * 18 / 52);
    const int stint2_laps = std::max(bundle.event_config.min_stint_length_laps, total_laps - stint1_laps);

    StrategyPlan strategy;
    strategy.stints = {
        StintSpec{TyreRole::Soft, stint1_laps, std::nullopt},
        StintSpec{TyreRole::Hard, stint2_laps, std::nullopt}
    };
    session.setStrategyPlan(strategy);

    std::cout << "\n[3/4] Strategy & Capability Validation\n";
    std::cout << "  Planned Strategy   : Stint 1 = SOFT (" << stint1_laps
              << " laps), Stint 2 = HARD (" << stint2_laps << " laps)\n";
    std::cout.flush();

    auto val_res = session.validate();
    if (!val_res.ok()) {
        std::cerr << "[VALIDATION BLOCKED] Engine refused execution (EngineStatus: "
                  << toString(val_res.status) << "):\n";
        printDiagnosticsList(std::cerr, val_res.diagnostics, true);
        return kExitValidationError;
    }

    std::cout << "  Validation Status  : " << toString(session.state()) << "\n";
    std::cout << "  Active Fidelity    : " << toString(val_res.value->fidelity_status) << "\n";
    std::cout << "  Execution Gate     : " << toString(val_res.value->execution_permission) << "\n";
    std::cout << "  Pre-Race Inventory : ";
    for (std::size_t i = 0; i < session.tyreInventory().size(); ++i) {
        const auto& s = session.tyreInventory()[i];
        std::cout << s.set_id << "(age=" << s.configured_age_laps << ", health="
                  << std::fixed << std::setprecision(0)
                  << (s.derived_starting_health.has_value() ? s.derived_starting_health->meanHealthPct() : 0.0)
                  << "%)" << (i + 1 < session.tyreInventory().size() ? ", " : "\n");
    }
    std::cout.flush();

    // 5. Execute the race simulation
    std::cout << "\n[4/4] Running Deterministic Race Simulation...\n";
    std::cout.flush();
    auto start_res = session.start();
    if (!start_res.ok()) {
        std::cerr << "[SIMULATION ERROR] Failed to start session ("
                  << toString(start_res.status) << "):\n";
        printDiagnosticsList(std::cerr, start_res.diagnostics, true);
        return kExitSimulationError;
    }

    while (session.state() == SessionState::Running) {
        auto run_res = session.runUntilStopOrCompletion();
        if (!run_res.ok()) {
            std::cerr << "[SIMULATION ERROR] Simulation failed during execution ("
                      << toString(run_res.status) << "):\n";
            printDiagnosticsList(std::cerr, run_res.diagnostics, true);
            return kExitSimulationError;
        }

        if (session.state() == SessionState::DecisionRequired &&
            session.pendingDecision().has_value()) {
            const PendingDecisionPrompt& prompt = *session.pendingDecision();
            std::cout << "  [INTERACTIVE PROMPT @ Lap " << prompt.completed_race_lap
                      << "] Reason: " << toString(prompt.reason)
                      << " | Active Set: " << prompt.current_tyre_set_id
                      << " (mean health: " << std::fixed << std::setprecision(1)
                      << prompt.current_tyre_health.meanHealthPct() << "%)"
                      << " | Applying KeepOriginalPlan & resuming...\n";
            std::cout.flush();

            InteractiveDecision decision;
            decision.action_type = DecisionActionType::KeepOriginalPlan;
            decision.user_note = "CLI smoke-test automatic confirmation of planned pit stop.";
            auto dec_res = session.applyDecision(decision);
            if (!dec_res.ok()) {
                std::cerr << "[SIMULATION ERROR] Failed to apply interactive decision ("
                          << toString(dec_res.status) << "):\n";
                printDiagnosticsList(std::cerr, dec_res.diagnostics, true);
                return kExitSimulationError;
            }
            auto res_res = session.resume();
            if (!res_res.ok()) {
                std::cerr << "[SIMULATION ERROR] Failed to resume session ("
                          << toString(res_res.status) << "):\n";
                printDiagnosticsList(std::cerr, res_res.diagnostics, true);
                return kExitSimulationError;
            }
        }
    }

    if (session.state() != SessionState::Completed || !session.lastResult().has_value()) {
        std::cerr << "[SIMULATION ERROR] Session terminated in non-completed state: "
                  << toString(session.state()) << "\n";
        return kExitSimulationError;
    }

    const RaceResult& result = *session.lastResult();

    // 6. Display structured RaceResult summary
    std::cout << "\n================================================================================\n";
    std::cout << "SIMULATION RESULT SUMMARY — " << result.official_event_name << "\n";
    std::cout << "================================================================================\n";
    std::cout << "Terminal State       : " << toString(result.terminal_state)
              << " (full distance: " << (result.completed_full_race_distance ? "YES" : "NO") << ")\n";
    std::cout << "Completed Laps       : " << result.completed_race_laps << " / "
              << result.configured_race_distance_laps << "\n";
    std::cout << "Operating Mode       : " << toString(result.race_mode) << "\n";
    std::cout << "Model Stage          : " << toString(result.model_stage) << "\n";
    std::cout << "Fidelity Status      : " << toString(result.fidelity_status) << "\n";
    std::cout << "Fidelity Note        : " << result.compatibility_and_fidelity_note << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total Race Time      : " << result.total_race_time_s << " s ("
              << formatRaceDuration(result.total_race_time_s) << ")\n";
    std::cout << "Total Flying Time    : " << result.total_flying_lap_time_s << " s\n";
    std::cout << "Total Pit Time Loss  : " << result.total_pit_stop_time_loss_s << " s ("
              << result.total_pit_stops << " stop(s): "
              << result.planned_pit_stops << " planned, "
              << result.mandatory_health_stops << " mandatory)\n";
    std::cout << "Fuel Summary         : initial=" << std::setprecision(2) << result.initial_fuel_kg
              << " kg, consumed=" << result.fuel_consumed_kg
              << " kg, remaining=" << result.final_fuel_remaining_kg << " kg\n";

    std::cout << "\n--- Stint Summary ---\n";
    std::cout << "Stint | Role   | Compound | Set | Start Lap | Target | Actual | Shortfall | Start Health | End Health | Flying Time (s)\n";
    std::cout << "------+--------+----------+-----+-----------+--------+--------+-----------+--------------+------------+----------------\n";
    for (const auto& st : result.stints) {
        std::cout << std::setw(5) << (st.stint_index + 1) << " | "
                  << std::left << std::setw(6) << toString(st.role) << " | "
                  << std::setw(8) << toString(st.physical_compound) << " | "
                  << std::setw(3) << st.assigned_set_id << std::right << " | "
                  << std::setw(9) << st.start_race_lap << " | "
                  << std::setw(6) << st.effective_target_laps << " | "
                  << std::setw(6) << st.actual_laps_completed << " | "
                  << std::setw(9) << st.shortfall_laps << " | "
                  << std::setw(11) << std::fixed << std::setprecision(1)
                  << st.set_starting_health_at_fit.meanHealthPct() << "% | "
                  << std::setw(9) << st.final_health.meanHealthPct() << "% | "
                  << std::setw(15) << std::setprecision(3) << st.cumulative_flying_time_s << "\n";
    }

    std::cout << "\n--- Pit-Stop Summary ---\n";
    if (result.pit_stops.empty()) {
        std::cout << "  (No pit stops executed)\n";
    } else {
        for (const auto& ps : result.pit_stops) {
            std::cout << "  Stop #" << ps.stop_sequence_number
                      << " after Lap " << ps.completed_race_lap
                      << " [" << toString(ps.reason) << "]: "
                      << ps.set_removed_id << " (" << toString(ps.role_removed) << "/"
                      << toString(ps.compound_removed) << ", end health "
                      << std::fixed << std::setprecision(1)
                      << ps.removed_set_final_health.meanHealthPct() << "%) -> "
                      << ps.set_fitted_id << " (" << toString(ps.role_fitted) << "/"
                      << toString(ps.compound_fitted) << ", start health "
                      << ps.fitted_set_starting_health.meanHealthPct() << "%), "
                      << "total loss = " << std::setprecision(3) << ps.total_stop_time_loss_s
                      << " s (pit lane net: " << ps.pit_lane_traversal_net_loss_s
                      << " s, stationary: " << ps.stationary_service_time_s << " s)\n";
        }
    }

    std::cout << "\n--- Sample Lap-by-Lap Trace "
              << (opts.verbose_laps ? "(All Laps)" : "(Key Milestone Laps; pass --verbose-laps for all)")
              << " ---\n";
    std::cout << "Lap | Stint | Set | Role/Comp | Age (start->end) | Health (start->end) | Fuel Mid (kg) | Flying Lap (s) | Pit Loss (s) | Elapsed (s)\n";
    std::cout << "----+-------+-----+-----------+------------------+---------------------+---------------+----------------+--------------+------------\n";
    for (std::size_t idx = 0; idx < result.lap_trace.size(); ++idx) {
        const auto& lt = result.lap_trace[idx];
        const bool is_milestone =
            opts.verbose_laps ||
            lt.race_lap == 1 ||
            lt.race_lap == 2 ||
            lt.stint_lap_number == 1 ||
            lt.pit_stop_occurred_after_lap ||
            lt.race_lap == result.configured_race_distance_laps ||
            (lt.race_lap % 10 == 0);
        if (!is_milestone) {
            continue;
        }
        const std::string role_comp =
            std::string(toString(lt.role)) + "/" + toString(lt.physical_compound);
        std::cout << std::setw(3) << lt.race_lap << " | "
                  << std::setw(5) << (lt.stint_index + 1) << " | "
                  << std::left << std::setw(3) << lt.tyre_set_id << " | "
                  << std::setw(9) << role_comp << std::right << " | "
                  << std::setw(7) << lt.tyre_age_start_laps << " -> "
                  << std::setw(5) << lt.tyre_age_end_laps << " | "
                  << std::setw(7) << std::fixed << std::setprecision(1)
                  << lt.health_start.meanHealthPct() << "% -> "
                  << std::setw(6) << lt.health_end.meanHealthPct() << "% | "
                  << std::setw(13) << std::setprecision(2) << lt.fuel_mid_lap_kg << " | "
                  << std::setw(14) << std::setprecision(3) << lt.lap_breakdown.total_flying_lap_time_s << " | "
                  << std::setw(12) << lt.pit_stop_loss_after_lap_s << " | "
                  << std::setw(11) << lt.cumulative_elapsed_time_s << "\n";
    }

    // Verify post-run tyre inventory reset invariant
    bool all_reset = true;
    for (const auto& s : session.tyreInventory()) {
        if (s.currently_fitted || s.consumed_in_current_run ||
            s.runtime_age_laps != s.configured_age_laps) {
            all_reset = false;
            break;
        }
    }

    std::cout << "\n--- Post-Run Inventory & Diagnostics Summary ---\n";
    std::cout << "  Post-Run Inventory Reset : "
              << (all_reset ? "VERIFIED (all 7 sets restored to pre-race configured state)" : "FAILED")
              << "\n";
    std::cout << "  Session Events Logged    : " << result.event_log.size() << "\n";
    std::cout << "  Session Diagnostics      : " << result.diagnostics.size()
              << " (DAL + Engine warnings/notices)\n";
    std::cout << "================================================================================\n";

    return all_reset ? kExitOk : kExitSimulationError;
}