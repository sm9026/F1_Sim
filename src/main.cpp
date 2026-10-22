#include "f1_sim/data_access/data_access_layer.hpp"
#include "f1_sim/simulation/race_session.hpp"

#include <algorithm>
#include <cctype>
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

namespace f1_sim {
namespace cli {

// Process exit codes (Section 3.8)
constexpr int kExitOk              = 0;
constexpr int kExitCliUsageError   = 1;
constexpr int kExitDataLoadError   = 2;
constexpr int kExitValidationError = 3;
constexpr int kExitSimulationError = 4;

/**
 * @brief Parsed command-line options for `f1_sim`.
 *
 * When launched without `--smoke-test`, `--help`, or `--check-only`, the application
 * opens the interactive menu-driven CLI using any supplied configuration flags as
 * initial session settings.
 */
struct CliOptions {
    bool show_help = false;
    bool check_only = false;
    bool smoke_test = false;
    bool strict_calibrated = false;
    bool verbose_laps = false;
    bool explicit_data_root = false;
    bool explicit_event = false;

    fs::path data_root;
    std::string event_id = "2026_british_gp";
    RaceMode race_mode = RaceMode::Preplanned;
    ModelStage model_stage = ModelStage::M1_TrackAwareCalibratedDry;
    double driver_mass_kg = 75.0;
};

namespace detail {

inline std::string trimString(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

inline std::string toUpperAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline bool isBackOrCancelToken(const std::string& token) {
    const std::string lower = toLowerAscii(trimString(token));
    return lower == "b" || lower == "back" || lower == "c" || lower == "cancel" || lower == "q";
}

inline bool isLikelyDataRoot(const fs::path& candidate) noexcept {
    std::error_code ec;
    return fs::exists(candidate, ec) && !ec &&
           fs::is_directory(candidate, ec) && !ec &&
           fs::is_directory(candidate / "events", ec) && !ec &&
           fs::is_directory(candidate / "tracks", ec) && !ec &&
           fs::is_directory(candidate / "compounds", ec) && !ec;
}

inline std::string formatHealthSummary(const TyreSetState& s) {
    if (!s.derived_starting_health.has_value() ||
        s.health_status == TyreSetHealthStatus::MissingCalibrationForAgeDerivation) {
        return "UNAVAILABLE (uncalibrated wear rate)";
    }
    std::ostringstream oss;
    const auto pcts = s.derived_starting_health->percentageValues();
    oss << std::fixed << std::setprecision(1)
        << s.derived_starting_health->meanHealthPct() << "% (FL:"
        << pcts[0] << " FR:" << pcts[1] << " RL:" << pcts[2] << " RR:" << pcts[3] << "%)";
    return oss.str();
}

inline std::string formatRuntimeHealthSummary(const CornerHealthVector& h) {
    std::ostringstream oss;
    const auto pcts = h.percentageValues();
    oss << std::fixed << std::setprecision(1)
        << h.meanHealthPct() << "% (min corner: " << h.minCornerHealthPct()
        << "% | FL:" << pcts[0] << " FR:" << pcts[1] << " RL:" << pcts[2] << " RR:" << pcts[3] << "%)";
    return oss.str();
}

} // namespace detail

inline void printUsage(std::ostream& out, const char* prog_name) {
    const std::string bin = (prog_name != nullptr && prog_name[0] != '\0') ? prog_name : "f1_sim";
    out << "F1_Sim — Formula 1 Race Strategy Simulator CLI\n"
        << "==============================================\n\n"
        << "Usage:\n"
        << "  " << bin << " [options]\n\n"
        << "Default Interactive Behaviour:\n"
        << "  Launching '" << bin << "' without '--smoke-test', '--check-only', or '--help'\n"
        << "  opens the interactive menu-driven CLI. Any configuration flags supplied on the\n"
        << "  command line are applied as the initial preselected settings for the session.\n\n"
        << "Options:\n"
        << "  --help, -h                  Display this help message and exit (code 0).\n"
        << "  --smoke-test                Execute the deterministic 2026 British GP (Silverstone,\n"
        << "                              52 laps) Soft 18 / Hard 34 smoke-test scenario and exit.\n"
        << "  --check-only                Load dataset via DAL, report raw calibration readiness\n"
        << "                              and parameter gaps, and exit without running a race.\n"
        << "  --data-root <path>          Path to local read-only runtime dataset directory\n"
        << "                              (defaults to './data' or '../data' relative to CWD/binary).\n"
        << "  --event <event_id>          Preselect event identifier to load via DAL\n"
        << "                              (default: 2026_british_gp).\n"
        << "  --mode <preplanned|interactive>\n"
        << "                              Select or preselect simulation operating mode\n"
        << "                              (default: preplanned).\n"
        << "  --stage <m0|m1|m2|m3>       Select or preselect mathematical model stage\n"
        << "                              (default: m1).\n"
        << "  --driver-mass <kg>          Configure driver mass in kg within [40.0, 160.0]\n"
        << "                              (default: 75.0).\n"
        << "  --strict-calibrated         Require strictly CALIBRATED parameters (blocks execution\n"
        << "                              if the compound catalogue is uncalibrated).\n"
        << "  --verbose-laps              Print all lap trace entries instead of milestone laps.\n\n"
        << "Exit Codes:\n"
        << "  0  Success (interactive exit, --smoke-test completed, or --help / --check-only succeeded)\n"
        << "  1  Invalid command-line arguments\n"
        << "  2  Data Access Layer (DAL) dataset resolution, loading, or schema validation failure\n"
        << "  3  Simulation readiness, calibration gate, or strategy validation failure\n"
        << "  4  Runtime simulation execution failure\n";
}

inline bool parseCliArgs(int argc, char** argv, CliOptions& out_opts, std::string& out_error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out_opts.show_help = true;
            return true;
        } else if (arg == "--check-only") {
            out_opts.check_only = true;
        } else if (arg == "--smoke-test") {
            out_opts.smoke_test = true;
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
            out_opts.explicit_event = true;
        } else if (arg.rfind("--event=", 0) == 0) {
            out_opts.event_id = arg.substr(std::string("--event=").size());
            out_opts.explicit_event = true;
        } else if (arg == "--mode") {
            if (i + 1 >= argc) {
                out_error = "Missing mode argument after '--mode' (expected 'preplanned' or 'interactive').";
                return false;
            }
            const std::string val = detail::toLowerAscii(argv[++i]);
            if (val == "preplanned") {
                out_opts.race_mode = RaceMode::Preplanned;
            } else if (val == "interactive") {
                out_opts.race_mode = RaceMode::Interactive;
            } else {
                out_error = "Invalid '--mode' value '" + val + "' (expected 'preplanned' or 'interactive').";
                return false;
            }
        } else if (arg.rfind("--mode=", 0) == 0) {
            const std::string val = detail::toLowerAscii(arg.substr(std::string("--mode=").size()));
            if (val == "preplanned") {
                out_opts.race_mode = RaceMode::Preplanned;
            } else if (val == "interactive") {
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
            const std::string val = detail::toLowerAscii(argv[++i]);
            if (val == "m0") {
                out_opts.model_stage = ModelStage::M0_DeterministicBaseline;
            } else if (val == "m1") {
                out_opts.model_stage = ModelStage::M1_TrackAwareCalibratedDry;
            } else if (val == "m2") {
                out_opts.model_stage = ModelStage::M2_DynamicEnvironmentDry;
            } else if (val == "m3") {
                out_opts.model_stage = ModelStage::M3_NonlinearCliffAndFourCorner;
            } else {
                out_error = "Invalid '--stage' value '" + val + "' (expected m0, m1, m2, or m3).";
                return false;
            }
        } else if (arg.rfind("--stage=", 0) == 0) {
            const std::string val = detail::toLowerAscii(arg.substr(std::string("--stage=").size()));
            if (val == "m0") {
                out_opts.model_stage = ModelStage::M0_DeterministicBaseline;
            } else if (val == "m1") {
                out_opts.model_stage = ModelStage::M1_TrackAwareCalibratedDry;
            } else if (val == "m2") {
                out_opts.model_stage = ModelStage::M2_DynamicEnvironmentDry;
            } else if (val == "m3") {
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
        } else if (arg.rfind("--driver-mass=", 0) == 0) {
            const std::string val = arg.substr(std::string("--driver-mass=").size());
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

inline fs::path resolveDefaultDataRoot(const char* argv0) {
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
        if (detail::isLikelyDataRoot(cand)) {
            return cand;
        }
    }
    return fs::path("data");
}

inline ModelParameters makeDeclaredLowerFidelitySmokeTestParams(bool allow_estimated = true) {
    ModelParameters params;
    params.param_set_id = "silverstone_2026_smoke_test_estimated_v1";
    params.parameter_source_status = SourceStatus::Estimated;
    params.ref_lap_time_s = 88.500;
    params.pit_loss_total_s = 21.000;
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

    addEstimatedCompound(PhysicalCompoundId::C1, +0.700, 0.012, 4.000);
    addEstimatedCompound(PhysicalCompoundId::C2, +0.350, 0.018, 4.500);
    addEstimatedCompound(PhysicalCompoundId::C3,  0.000, 0.025, 5.000);
    addEstimatedCompound(PhysicalCompoundId::C4, -0.450, 0.034, 5.500);
    addEstimatedCompound(PhysicalCompoundId::C5, -0.850, 0.045, 6.000);

    return params;
}

inline std::string formatRaceDuration(double total_seconds) {
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

inline void printDiagnosticsList(
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

inline void printRaceResultReport(
    std::ostream& out,
    const RaceResult& result,
    const std::vector<TyreSetState>& post_run_inventory,
    bool verbose_laps) {
    out << "\n================================================================================\n";
    out << "SIMULATION RESULT SUMMARY — " << result.official_event_name << "\n";
    out << "================================================================================\n";
    out << "Terminal State       : " << toString(result.terminal_state)
        << " (full distance: " << (result.completed_full_race_distance ? "YES" : "NO") << ")\n";
    out << "Completed Laps       : " << result.completed_race_laps << " / "
        << result.configured_race_distance_laps << "\n";
    out << "Operating Mode       : " << toString(result.race_mode) << "\n";
    out << "Model Stage          : " << toString(result.model_stage) << "\n";
    out << "Fidelity Status      : " << toString(result.fidelity_status) << "\n";
    out << "Fidelity Note        : " << result.compatibility_and_fidelity_note << "\n";
    out << std::fixed << std::setprecision(3);
    out << "Total Race Time      : " << result.total_race_time_s << " s ("
        << formatRaceDuration(result.total_race_time_s) << ")\n";
    out << "Total Flying Time    : " << result.total_flying_lap_time_s << " s\n";
    out << "Total Pit Time Loss  : " << result.total_pit_stop_time_loss_s << " s ("
        << result.total_pit_stops << " stop(s): "
        << result.planned_pit_stops << " planned, "
        << result.mandatory_health_stops << " mandatory)\n";
    out << "Fuel Summary         : initial=" << std::setprecision(2) << result.initial_fuel_kg
        << " kg, consumed=" << result.fuel_consumed_kg
        << " kg, remaining=" << result.final_fuel_remaining_kg << " kg\n";

    out << "\n--- Stint Summary ---\n";
    out << "Stint | Role   | Compound | Set | Start Lap | Target | Actual | Shortfall | Start Health | End Health | Flying Time (s)\n";
    out << "------+--------+----------+-----+-----------+--------+--------+-----------+--------------+------------+----------------\n";
    for (const auto& st : result.stints) {
        out << std::setw(5) << (st.stint_index + 1) << " | "
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

    out << "\n--- Pit-Stop Summary ---\n";
    if (result.pit_stops.empty()) {
        out << "  (No pit stops executed)\n";
    } else {
        for (const auto& ps : result.pit_stops) {
            out << "  Stop #" << ps.stop_sequence_number
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

    if (!result.interventions.empty()) {
        out << "\n--- Strategy Interventions & Shortfall Cascades ---\n";
        for (const auto& iv : result.interventions) {
            out << "  [#" << iv.sequence_id << " @ Lap " << iv.completed_race_lap
                << " | " << iv.intervention_type << "] " << iv.summary << "\n";
        }
    }

    out << "\n--- Sample Lap-by-Lap Trace "
        << (verbose_laps ? "(All Laps)" : "(Key Milestone Laps; enable --verbose-laps for all)")
        << " ---\n";
    out << "Lap | Stint | Set | Role/Comp | Age (start->end) | Health (start->end) | Fuel Mid (kg) | Flying Lap (s) | Pit Loss (s) | Elapsed (s)\n";
    out << "----+-------+-----+-----------+------------------+---------------------+---------------+----------------+--------------+------------\n";
    for (std::size_t idx = 0; idx < result.lap_trace.size(); ++idx) {
        const auto& lt = result.lap_trace[idx];
        const bool is_milestone =
            verbose_laps ||
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
        out << std::setw(3) << lt.race_lap << " | "
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

    bool all_reset = true;
    for (const auto& s : post_run_inventory) {
        if (s.currently_fitted || s.consumed_in_current_run ||
            s.runtime_age_laps != s.configured_age_laps) {
            all_reset = false;
            break;
        }
    }

    out << "\n--- Post-Run Inventory & Diagnostics Summary ---\n";
    out << "  Post-Run Inventory Reset : "
        << (all_reset ? "VERIFIED (all 7 sets restored to pre-race configured state)" : "FAILED")
        << "\n";
    out << "  Session Events Logged    : " << result.event_log.size() << "\n";
    out << "  Session Diagnostics      : " << result.diagnostics.size()
        << " (DAL + Engine warnings/notices)\n";
    out << "================================================================================\n";
}

/**
 * @brief Interactive and batch CLI controller for the F1 Race Strategy Simulator.
 */
class CliApp {
public:
    CliApp(
        const CliOptions& opts,
        std::istream& in,
        std::ostream& out,
        std::ostream& err)
        : opts_(opts),
          in_(in),
          out_(out),
          err_(err) {}

    int run(const char* prog_name = "f1_sim") {
        if (opts_.show_help) {
            printUsage(out_, prog_name);
            return kExitOk;
        }

        effective_data_root_ = opts_.explicit_data_root
            ? opts_.data_root
            : resolveDefaultDataRoot(prog_name);

        if (opts_.check_only || opts_.smoke_test) {
            return runSmokeTestOrCheckOnly();
        }

        return runInteractiveSession();
    }

    int runSmokeTestOrCheckOnly() {
        out_ << "================================================================================\n";
        out_ << "F1_Sim — Formula 1 Strategy Simulator CLI ("
             << (opts_.check_only ? "--check-only" : "--smoke-test") << ")\n";
        out_ << "================================================================================\n";
        out_ << "Configured Data Root : " << effective_data_root_.generic_string()
             << (opts_.explicit_data_root ? " (explicit --data-root)" : " (auto-discovered)") << "\n";
        out_ << "Target Event ID      : " << opts_.event_id << "\n";
        out_ << "Operating Mode       : " << toString(opts_.race_mode) << "\n";
        out_ << "Model Stage          : " << toString(opts_.model_stage) << "\n";
        out_ << "Driver Mass          : " << std::fixed << std::setprecision(1)
             << opts_.driver_mass_kg << " kg\n";
        out_ << "--------------------------------------------------------------------------------\n";
        out_.flush();

        diag_collector_ = std::make_shared<InMemoryDiagnosticCollector>();
        dal_ = std::make_shared<DataAccessLayer>(effective_data_root_, diag_collector_);

        if (!dal_->resolver().isRootValid()) {
            err_ << "[DAL ERROR] Invalid local data_root directory: "
                 << effective_data_root_.generic_string() << "\n";
            if (dal_->resolver().rootError().has_value()) {
                printDiagnosticsList(err_, {*dal_->resolver().rootError()});
            }
            return kExitDataLoadError;
        }

        session_ = std::make_unique<RaceSession>(dal_);
        auto select_res = session_->selectEvent(opts_.event_id);
        if (!select_res.ok() || session_->eventBundle() == nullptr) {
            err_ << "[DAL ERROR] Failed to load event bundle '" << opts_.event_id
                 << "' (EngineStatus: " << toString(select_res.status) << "):\n";
            printDiagnosticsList(err_, select_res.diagnostics, true);
            return kExitDataLoadError;
        }

        const EventBundle& bundle = *session_->eventBundle();
        out_ << "[1/4] DAL EventBundle Loaded & Cross-Validated Successfully\n";
        printEventSummary(bundle);

        const CapabilityReadinessReport& raw_readiness = *select_res.value;
        const Result<void> dal_sufficiency = dal_->checkBaselineSimulationSufficiency(bundle);
        out_ << "\n[2/4] Raw Dataset Calibration & Readiness Audit\n";
        out_ << "  Bundle Load Valid  : " << (raw_readiness.bundle_load_valid ? "YES" : "NO") << "\n";
        out_ << "  Catalogue Status   : " << bundle.compound_catalogue.calibration_status << "\n";
        out_ << "  Raw DAL Sufficiency: " << toString(dal_sufficiency.status)
             << " (" << dal_sufficiency.diagnostics.size() << " uncalibrated compound fields)\n";
        out_ << "  Raw Engine Fidelity: " << toString(raw_readiness.fidelity_status) << "\n";
        out_ << "  Raw Execution Gate : " << toString(raw_readiness.execution_permission)
             << " (" << raw_readiness.parameter_gaps.size() << " parameter gaps in raw bundle)\n";

        if (!raw_readiness.parameter_gaps.empty()) {
            out_ << "  Explicit Parameter Gaps Detected in Raw On-Disk Bundle:\n";
            for (const auto& gap : raw_readiness.parameter_gaps) {
                out_ << "    - " << gap.field_path << " [" << toString(gap.status)
                     << " -> " << toString(gap.missing_policy) << "]: " << gap.detail << "\n";
            }
        }
        out_.flush();

        if (opts_.check_only) {
            out_ << "\n[--check-only] Dataset and readiness inspection complete.\n";
            return kExitOk;
        }

        auto mode_res = session_->setRaceMode(opts_.race_mode);
        auto stage_res = session_->setModelStage(opts_.model_stage);
        auto mass_res = session_->setDriverMassKg(opts_.driver_mass_kg);
        if (!mode_res.ok() || !stage_res.ok() || !mass_res.ok()) {
            err_ << "[VALIDATION ERROR] Failed to configure session parameters:\n";
            printDiagnosticsList(err_, mode_res.diagnostics, true);
            printDiagnosticsList(err_, stage_res.diagnostics, true);
            printDiagnosticsList(err_, mass_res.diagnostics, true);
            return kExitValidationError;
        }

        if (opts_.strict_calibrated) {
            ModelParameters strict_params;
            strict_params.allow_estimated_or_assumed_parameters = false;
            session_->setModelParameters(strict_params);
        } else {
            ModelParameters est_params = makeDeclaredLowerFidelitySmokeTestParams(true);
            session_->setModelParameters(est_params);
        }

        const int total_laps = bundle.event_config.race_distance_laps;
        const int stint1_laps = std::max(bundle.event_config.min_stint_length_laps, total_laps * 18 / 52);
        const int stint2_laps = std::max(bundle.event_config.min_stint_length_laps, total_laps - stint1_laps);

        StrategyPlan strategy;
        strategy.stints = {
            StintSpec{TyreRole::Soft, stint1_laps, std::nullopt},
            StintSpec{TyreRole::Hard, stint2_laps, std::nullopt}
        };
        session_->setStrategyPlan(strategy);

        out_ << "\n[3/4] Strategy & Capability Validation\n";
        out_ << "  Planned Strategy   : Stint 1 = SOFT (" << stint1_laps
             << " laps), Stint 2 = HARD (" << stint2_laps << " laps)\n";
        out_.flush();

        auto val_res = session_->validate();
        if (!val_res.ok()) {
            err_ << "[VALIDATION BLOCKED] Engine refused execution (EngineStatus: "
                 << toString(val_res.status) << "):\n";
            printDiagnosticsList(err_, val_res.diagnostics, true);
            return kExitValidationError;
        }

        out_ << "  Validation Status  : " << toString(session_->state()) << "\n";
        out_ << "  Active Fidelity    : " << toString(val_res.value->fidelity_status) << "\n";
        out_ << "  Execution Gate     : " << toString(val_res.value->execution_permission) << "\n";
        out_ << "  Pre-Race Inventory : ";
        for (std::size_t i = 0; i < session_->tyreInventory().size(); ++i) {
            const auto& s = session_->tyreInventory()[i];
            out_ << s.set_id << "(age=" << s.configured_age_laps << ", health=";
            if (s.derived_starting_health.has_value()) {
                out_ << std::fixed << std::setprecision(0) << s.derived_starting_health->meanHealthPct() << "%";
            } else {
                out_ << "UNAVAILABLE";
            }
            out_ << ")" << (i + 1 < session_->tyreInventory().size() ? ", " : "\n");
        }
        out_.flush();

        out_ << "\n[4/4] Running Deterministic Race Simulation...\n";
        out_.flush();
        auto start_res = session_->start();
        if (!start_res.ok()) {
            err_ << "[SIMULATION ERROR] Failed to start session ("
                 << toString(start_res.status) << "):\n";
            printDiagnosticsList(err_, start_res.diagnostics, true);
            return kExitSimulationError;
        }

        while (session_->state() == SessionState::Running) {
            auto run_res = session_->runUntilStopOrCompletion();
            if (!run_res.ok()) {
                err_ << "[SIMULATION ERROR] Simulation failed during execution ("
                     << toString(run_res.status) << "):\n";
                printDiagnosticsList(err_, run_res.diagnostics, true);
                return kExitSimulationError;
            }

            if (session_->state() == SessionState::DecisionRequired &&
                session_->pendingDecision().has_value()) {
                const PendingDecisionPrompt& prompt = *session_->pendingDecision();
                out_ << "  [SMOKE-TEST INTERACTIVE PROMPT @ Lap " << prompt.completed_race_lap
                     << "] Reason: " << toString(prompt.reason)
                     << " | Active Set: " << prompt.current_tyre_set_id
                     << " (mean health: " << std::fixed << std::setprecision(1)
                     << prompt.current_tyre_health.meanHealthPct() << "%)"
                     << " | Applying KeepOriginalPlan & resuming...\n";
                out_.flush();

                InteractiveDecision decision;
                decision.action_type = DecisionActionType::KeepOriginalPlan;
                decision.user_note = "CLI smoke-test automatic confirmation of planned pit stop.";
                auto dec_res = session_->applyDecision(decision);
                if (!dec_res.ok()) {
                    err_ << "[SIMULATION ERROR] Failed to apply interactive decision ("
                         << toString(dec_res.status) << "):\n";
                    printDiagnosticsList(err_, dec_res.diagnostics, true);
                    return kExitSimulationError;
                }
                auto res_res = session_->resume();
                if (!res_res.ok()) {
                    err_ << "[SIMULATION ERROR] Failed to resume session ("
                         << toString(res_res.status) << "):\n";
                    printDiagnosticsList(err_, res_res.diagnostics, true);
                    return kExitSimulationError;
                }
            }
        }

        if (session_->state() != SessionState::Completed || !session_->lastResult().has_value()) {
            err_ << "[SIMULATION ERROR] Session terminated in non-completed state: "
                 << toString(session_->state()) << "\n";
            return kExitSimulationError;
        }

        printRaceResultReport(out_, *session_->lastResult(), session_->tyreInventory(), opts_.verbose_laps);

        for (const auto& s : session_->tyreInventory()) {
            if (s.currently_fitted || s.consumed_in_current_run ||
                s.runtime_age_laps != s.configured_age_laps) {
                return kExitSimulationError;
            }
        }
        return kExitOk;
    }

    int runInteractiveSession() {
        if (!initializeDalAndDefaultEvent()) {
            return opts_.explicit_event || !dal_->resolver().isRootValid()
                ? kExitDataLoadError
                : kExitValidationError;
        }

        out_ << "================================================================================\n";
        out_ << "F1_Sim — Interactive Formula 1 Race Strategy Simulator\n";
        out_ << "================================================================================\n";

        while (!input_eof_) {
            printMainMenuBanner();
            out_ << "Main Menu:\n"
                 << "  1. Select event / track\n"
                 << "  2. Configure tyre inventory\n"
                 << "  3. Build or edit race strategy\n"
                 << "  4. Configure race settings\n"
                 << "  5. Inspect data readiness and model assumptions\n"
                 << "  6. Start simulation\n"
                 << "  7. Exit\n";

            int choice = 0;
            if (!promptMenuChoice("Select an option [1-7]: ", 1, 7, choice)) {
                out_ << "\nEnd of input reached. Exiting F1_Sim.\n";
                break;
            }

            switch (choice) {
                case 1:
                    handleSelectEventMenu();
                    break;
                case 2:
                    handleTyreInventoryMenu();
                    break;
                case 3:
                    handleStrategyBuilderMenu();
                    break;
                case 4:
                    handleRaceSettingsMenu();
                    break;
                case 5:
                    handleInspectReadinessMenu();
                    break;
                case 6:
                    handleStartSimulationFlow();
                    break;
                case 7:
                    out_ << "Exiting F1_Sim. Goodbye!\n";
                    return kExitOk;
                default:
                    break;
            }
        }

        return kExitOk;
    }

    const RaceSession& session() const noexcept { return *session_; }
    RaceSession& mutableSession() noexcept { return *session_; }
    const DataAccessLayer& dal() const noexcept { return *dal_; }

private:
    bool initializeDalAndDefaultEvent() {
        diag_collector_ = std::make_shared<InMemoryDiagnosticCollector>();
        dal_ = std::make_shared<DataAccessLayer>(effective_data_root_, diag_collector_);

        if (!dal_->resolver().isRootValid()) {
            err_ << "[DAL ERROR] Invalid local data_root directory: "
                 << effective_data_root_.generic_string() << "\n";
            if (dal_->resolver().rootError().has_value()) {
                printDiagnosticsList(err_, {*dal_->resolver().rootError()});
            }
            return false;
        }

        session_ = std::make_unique<RaceSession>(dal_);

        if (opts_.strict_calibrated) {
            ModelParameters strict_params;
            strict_params.allow_estimated_or_assumed_parameters = false;
            session_->setModelParameters(strict_params);
        } else {
            session_->setModelParameters(makeDeclaredLowerFidelitySmokeTestParams(true));
        }

        session_->setRaceMode(opts_.race_mode);
        session_->setModelStage(opts_.model_stage);
        auto mass_res = session_->setDriverMassKg(opts_.driver_mass_kg);
        if (!mass_res.ok()) {
            err_ << "[VALIDATION ERROR] Invalid initial driver mass (" << opts_.driver_mass_kg << " kg):\n";
            printDiagnosticsList(err_, mass_res.diagnostics, true);
            return false;
        }

        auto list_res = dal_->listAvailableEventIds();
        if (!list_res.ok() || list_res.value->empty()) {
            out_ << "[NOTICE] No valid events were found under data_root/events/.\n";
            return !opts_.explicit_event;
        }

        std::string target_event = opts_.event_id;
        if (!opts_.explicit_event) {
            bool found_default = false;
            for (const auto& id : *list_res.value) {
                if (id == target_event) {
                    found_default = true;
                    break;
                }
            }
            if (!found_default) {
                target_event = list_res.value->front();
            }
        }

        auto sel_res = session_->selectEvent(target_event);
        if (!sel_res.ok()) {
            if (opts_.explicit_event) {
                err_ << "[DAL ERROR] Failed to load explicitly requested event '" << target_event << "':\n";
                printDiagnosticsList(err_, sel_res.diagnostics, true);
                return false;
            }
            out_ << "[WARNING] Could not auto-select default event '" << target_event << "'.\n";
        }

        return true;
    }

    void printMainMenuBanner() const {
        out_ << "\n--------------------------------------------------------------------------------\n";
        out_ << "CURRENT SESSION CONFIGURATION\n";
        out_ << "--------------------------------------------------------------------------------\n";
        out_ << "  Data Root      : " << effective_data_root_.generic_string() << "\n";
        if (session_->eventBundle() != nullptr) {
            const EventBundle& b = *session_->eventBundle();
            out_ << "  Selected Event : " << b.event_config.event_id << " — "
                 << b.event_config.official_event_name << " ("
                 << b.track_layout.track_name << ", "
                 << b.event_config.race_distance_laps << " laps, "
                 << std::fixed << std::setprecision(3) << b.event_config.scheduled_race_distance_km << " km)\n";
            out_ << "  Role Mapping   : ";
            for (std::size_t i = 0; i < b.role_mapping.mappings.size(); ++i) {
                const auto& m = b.role_mapping.mappings[i];
                out_ << toString(m.role_id) << "->" << toString(m.compound_id)
                     << (i + 1 < b.role_mapping.mappings.size() ? ", " : "\n");
            }
        } else {
            out_ << "  Selected Event : [NONE SELECTED]\n";
        }

        out_ << "  Tyre Inventory : ";
        for (std::size_t i = 0; i < session_->tyreInventory().size(); ++i) {
            const auto& s = session_->tyreInventory()[i];
            out_ << s.set_id << "(age=" << s.configured_age_laps << "L, ";
            if (s.derived_starting_health.has_value()) {
                out_ << std::fixed << std::setprecision(0) << s.derived_starting_health->meanHealthPct() << "%";
            } else {
                out_ << "UNAVAIL";
            }
            out_ << ")" << (i + 1 < session_->tyreInventory().size() ? ", " : "\n");
        }

        const StrategyPlan& committed = session_->strategyPlan();
        if (committed.stints.empty()) {
            out_ << "  Race Strategy  : [NOT CONFIGURED — select Option 3 to build a strategy]\n";
        } else {
            int sum_laps = 0;
            out_ << "  Race Strategy  : " << committed.stints.size() << " stint(s): ";
            for (std::size_t i = 0; i < committed.stints.size(); ++i) {
                const auto& st = committed.stints[i];
                sum_laps += st.target_laps;
                out_ << "#" << (i + 1) << " " << toString(st.role) << "("
                     << (st.requested_set_id.has_value() ? *st.requested_set_id : "AUTO")
                     << ", " << st.target_laps << "L)"
                     << (i + 1 < committed.stints.size() ? " -> " : "");
            }
            out_ << " [Total: " << sum_laps << " laps]\n";
        }

        const auto readiness = session_->assessReadiness();
        out_ << "  Race Settings  : Mode=" << toString(session_->raceMode())
             << " | Stage=" << toString(session_->modelStage())
             << " | Driver Mass=" << std::fixed << std::setprecision(1)
             << session_->vehicleConfig().driver_mass_kg << " kg\n";
        out_ << "  Readiness Gate : Fidelity=" << toString(readiness.fidelity_status)
             << " | Permission=" << toString(readiness.execution_permission)
             << (opts_.strict_calibrated ? " [STRICT CALIBRATED]" : " [LOWER-FIDELITY ALLOWED]") << "\n";
        out_ << "--------------------------------------------------------------------------------\n";
    }

    void printEventSummary(const EventBundle& bundle) const {
        out_ << "  Event ID           : " << bundle.event_config.event_id << "\n";
        out_ << "  Event Name         : " << bundle.event_config.official_event_name
             << " (Round " << bundle.event_config.round_number << ", "
             << bundle.event_config.season_year << ")\n";
        out_ << "  Competition Format : " << toString(bundle.event_config.competition_format) << "\n";
        out_ << "  Circuit Layout     : " << bundle.track_layout.track_name
             << " (" << bundle.track_layout.track_id
             << ", layout v" << bundle.track_layout.layout_version
             << ", surface v" << bundle.track_surface.surface_version << ")\n";
        out_ << "  Lap & Race Distance: " << std::fixed << std::setprecision(1)
             << bundle.track_layout.lap_length_m << " m/lap x "
             << bundle.event_config.race_distance_laps << " laps = "
             << std::setprecision(3) << bundle.event_config.scheduled_race_distance_km << " km\n";
        out_ << "  Pit Lane Geometry  : " << std::setprecision(1)
             << bundle.track_layout.pit_lane_length_m << " m @ "
             << bundle.track_layout.pit_speed_limit_kmh << " km/h\n";
        out_ << "  Strategy Rules     : stops=[" << bundle.event_config.min_pit_stops << ", "
             << bundle.event_config.max_pit_stops << "], min_stint="
             << bundle.event_config.min_stint_length_laps << " lap(s), min_distinct_compounds="
             << bundle.event_config.min_distinct_dry_compounds << "\n";
        out_ << "  Role Mapping (m_e) : ";
        for (std::size_t i = 0; i < bundle.role_mapping.mappings.size(); ++i) {
            const auto& m = bundle.role_mapping.mappings[i];
            out_ << toString(m.role_id) << " -> " << toString(m.compound_id)
                 << " (" << m.sidewall_colour << ", rank " << m.hardness_rank << ")"
                 << (i + 1 < bundle.role_mapping.mappings.size() ? ", " : "\n");
        }
    }

    void handleSelectEventMenu() {
        out_ << "\n================================================================================\n";
        out_ << "EVENT & TRACK SELECTION\n";
        out_ << "================================================================================\n";

        auto list_res = dal_->listAvailableEventIds();
        if (!list_res.ok()) {
            err_ << "[DAL ERROR] Could not list events from local data directory:\n";
            printDiagnosticsList(err_, list_res.diagnostics, true);
            return;
        }

        struct DiscoveredEvent {
            std::string event_id;
            EventBundle bundle;
        };
        std::vector<DiscoveredEvent> valid_events;

        for (const auto& eid : *list_res.value) {
            auto bundle_res = dal_->loadEventBundle(eid);
            if (bundle_res.ok() && bundle_res.value.has_value()) {
                valid_events.push_back(DiscoveredEvent{eid, std::move(*bundle_res.value)});
            } else {
                out_ << "  [SKIPPED INVALID EVENT '" << eid << "']: "
                     << toString(bundle_res.status) << "\n";
            }
        }

        if (valid_events.empty()) {
            out_ << "No valid events could be loaded from the local dataset directory ("
                 << effective_data_root_.generic_string() << ").\n";
            return;
        }

        if (valid_events.size() == 1) {
            out_ << "Available Events in Local Repository: 1 valid event found (only one event is available in the current dataset).\n\n";
        } else {
            out_ << "Available Events in Local Repository: " << valid_events.size() << " valid events found.\n\n";
        }

        for (std::size_t i = 0; i < valid_events.size(); ++i) {
            const auto& ev = valid_events[i];
            const bool is_current = (session_->eventBundle() != nullptr &&
                                     session_->eventBundle()->event_config.event_id == ev.event_id);
            out_ << "[" << (i + 1) << "] " << ev.event_id
                 << (is_current ? " (CURRENTLY SELECTED)" : "") << "\n";
            printEventSummary(ev.bundle);
            out_ << "\n";
        }

        while (!input_eof_) {
            out_ << "Enter event number [1-" << valid_events.size()
                 << "], event_id, or 'b' to return to main menu: ";
            out_.flush();

            std::string line;
            if (!readLineTrimmed(line)) {
                return;
            }
            if (line.empty()) {
                out_ << "  [INPUT ERROR] Please enter an event number, event_id, or 'b' to go back.\n";
                continue;
            }
            if (detail::isBackOrCancelToken(line) || line == "0") {
                out_ << "Returning to main menu without changing event.\n";
                return;
            }

            std::optional<std::size_t> matched_idx;
            char* end_ptr = nullptr;
            const long idx_val = std::strtol(line.c_str(), &end_ptr, 10);
            if (end_ptr != line.c_str() && *end_ptr == '\0') {
                if (idx_val >= 1 && static_cast<std::size_t>(idx_val) <= valid_events.size()) {
                    matched_idx = static_cast<std::size_t>(idx_val - 1);
                }
            }
            if (!matched_idx.has_value()) {
                for (std::size_t i = 0; i < valid_events.size(); ++i) {
                    if (valid_events[i].event_id == line) {
                        matched_idx = i;
                        break;
                    }
                }
            }

            if (!matched_idx.has_value()) {
                out_ << "  [INPUT ERROR] Unknown event selection '" << line
                     << "'. Please enter a number in [1, " << valid_events.size()
                     << "] or a valid event_id.\n";
                continue;
            }

            const std::string& chosen_id = valid_events[*matched_idx].event_id;
            out_ << "Notice: Selecting or switching an event resets all 7 tyre sets' configured ages to 0 laps (100% starting health).\n";
            bool confirmed = false;
            if (!promptYesNo("Confirm selecting event '" + chosen_id + "'? (y/n): ", true, confirmed)) {
                return;
            }
            if (!confirmed) {
                out_ << "Event switch cancelled. Retaining existing event and tyre-set ages.\n";
                return;
            }

            auto sel_res = session_->selectEvent(chosen_id);
            if (!sel_res.ok()) {
                err_ << "  [ENGINE ERROR] Failed to select event '" << chosen_id << "':\n";
                printDiagnosticsList(err_, sel_res.diagnostics, true);
                return;
            }

            if (!session_->strategyPlan().stints.empty() && session_->eventBundle() != nullptr) {
                int sum = 0;
                for (const auto& s : session_->strategyPlan().stints) {
                    sum += s.target_laps;
                }
                if (sum != session_->eventBundle()->event_config.race_distance_laps) {
                    session_->setStrategyPlan(StrategyPlan{});
                    draft_strategy_.stints.clear();
                    out_ << "  [NOTE] Previous strategy lap count did not match new event distance and was cleared.\n";
                }
            }

            out_ << "Successfully selected event '" << chosen_id
                 << "'. All 7 physical tyre sets have been reset to age 0 laps.\n";
            return;
        }
    }

    void printTyreInventoryTable() const {
        out_ << "\n--- Physical Tyre Inventory (7 Sets: 3 Soft, 2 Medium, 2 Hard) ---\n";
        out_ << "Set ID | Role   | Mapped Compound | Configured Age | Derived Starting Health                       | Status & Diagnostics\n";
        out_ << "-------+--------+-----------------+----------------+-----------------------------------------------+----------------------------------------\n";
        for (const auto& s : session_->tyreInventory()) {
            const std::string comp_str = s.mapped_compound_id.has_value()
                ? toString(*s.mapped_compound_id)
                : "UNMAPPED";
            std::string status_note = toString(s.health_status);
            if (s.health_status == TyreSetHealthStatus::DepletedBelowMandatoryThreshold) {
                status_note += " (BLOCKED: below <30% mean / <12% corner)";
            } else if (s.health_status == TyreSetHealthStatus::MissingCalibrationForAgeDerivation) {
                status_note += " (BLOCKED: missing wear calibration)";
            }
            out_ << std::left << std::setw(6) << s.set_id << " | "
                 << std::setw(6) << toString(s.role) << " | "
                 << std::setw(15) << comp_str << " | "
                 << std::right << std::setw(9) << s.configured_age_laps << " laps | "
                 << std::left << std::setw(45) << detail::formatHealthSummary(s) << " | "
                 << status_note << "\n";
        }
        out_ << "  [Inventory Rule] You may configure prior tyre age (in laps), but tyre health is\n"
             << "                   strictly derived by the engine and can never be entered or overridden directly.\n";
    }

    void handleTyreInventoryMenu() {
        while (!input_eof_) {
            printTyreInventoryTable();
            out_ << "\nTyre Inventory Menu:\n"
                 << "  1. View complete inventory\n"
                 << "  2. Edit configured age of a selected set\n"
                 << "  3. Reset all seven sets to age zero\n"
                 << "  4. Return to main menu\n";

            int choice = 0;
            if (!promptMenuChoice("Select tyre inventory option [1-4]: ", 1, 4, choice)) {
                return;
            }

            if (choice == 1) {
                continue;
            } else if (choice == 2) {
                while (!input_eof_) {
                    out_ << "Enter set ID to configure (S1, S2, S3, M1, M2, H1, H2) or 'b' to cancel: ";
                    out_.flush();
                    std::string raw_set;
                    if (!readLineTrimmed(raw_set)) {
                        return;
                    }
                    if (raw_set.empty()) {
                        out_ << "  [INPUT ERROR] Set ID cannot be empty.\n";
                        continue;
                    }
                    if (detail::isBackOrCancelToken(raw_set)) {
                        break;
                    }
                    const std::string set_id = detail::toUpperAscii(raw_set);
                    bool found_set = false;
                    for (const auto& s : session_->tyreInventory()) {
                        if (s.set_id == set_id) {
                            found_set = true;
                            break;
                        }
                    }
                    if (!found_set) {
                        auto err_res = session_->setConfiguredTyreAge(set_id, 0);
                        out_ << "  [INVALID SET ID] ";
                        printDiagnosticsList(out_, err_res.diagnostics, true, "");
                        continue;
                    }

                    bool backed_out = false;
                    int new_age = 0;
                    if (!promptIntInRange(
                            "Enter prior age in completed laps for " + set_id + " [0-" +
                                std::to_string(TyreInventoryManager::kMaxAllowedConfiguredAgeLaps) +
                                "] (or 'b' to cancel): ",
                            0,
                            TyreInventoryManager::kMaxAllowedConfiguredAgeLaps,
                            new_age,
                            true,
                            &backed_out)) {
                        return;
                    }
                    if (backed_out) {
                        break;
                    }

                    auto upd_res = session_->setConfiguredTyreAge(set_id, new_age);
                    if (!upd_res.ok()) {
                        out_ << "  [ENGINE REJECTED AGE UPDATE]\n";
                        printDiagnosticsList(out_, upd_res.diagnostics, true, "    ");
                        continue;
                    }

                    const TyreSetState& updated = *upd_res.value;
                    out_ << "  [UPDATED] Set " << updated.set_id << " (" << toString(updated.role)
                         << ") configured age = " << updated.configured_age_laps
                         << " laps -> Derived Starting Health: " << detail::formatHealthSummary(updated)
                         << " [" << toString(updated.health_status) << "]\n";
                    if (!upd_res.diagnostics.empty()) {
                        printDiagnosticsList(out_, upd_res.diagnostics, false, "    ");
                    }
                    break;
                }
            } else if (choice == 3) {
                auto res = session_->resetConfiguredTyreAgesToDefault();
                if (!res.ok()) {
                    printDiagnosticsList(out_, res.diagnostics, true, "  ");
                } else {
                    out_ << "All seven physical tyre sets (S1..S3, M1..M2, H1..H2) reset to age 0 laps (100.0% health).\n";
                }
            } else if (choice == 4) {
                return;
            }
        }
    }

    void printStrategyPreviewTable(
        const StrategyPlan& plan,
        const std::vector<std::string>* allocated_sets = nullptr) const {
        if (session_->eventBundle() == nullptr) {
            out_ << "  (No event selected)\n";
            return;
        }
        const EventBundle& bundle = *session_->eventBundle();
        const int race_laps = bundle.event_config.race_distance_laps;

        out_ << "\n--- Strategy Preview (Target Race Distance: " << race_laps << " laps) ---\n";
        if (plan.stints.empty()) {
            out_ << "  (Strategy has 0 stints configured)\n";
            return;
        }

        out_ << "Stint | Role   | Mapped Compound | Requested Set | Allocated Set | Prior Age | Starting Health               | Lap Window  | Target Laps | Pit Stop After Lap\n";
        out_ << "------+--------+-----------------+---------------+---------------+-----------+-------------------------------+-------------+-------------+-------------------\n";

        int cum_lap = 0;
        for (std::size_t i = 0; i < plan.stints.size(); ++i) {
            const StintSpec& st = plan.stints[i];
            const auto cid_opt = bundle.role_mapping.compoundForRole(st.role);
            const std::string comp_str = cid_opt.has_value() ? toString(*cid_opt) : "UNMAPPED";
            const std::string req_str = st.requested_set_id.has_value() ? *st.requested_set_id : "AUTO";
            const std::string alloc_str =
                (allocated_sets != nullptr && i < allocated_sets->size() && !(*allocated_sets)[i].empty())
                    ? (*allocated_sets)[i]
                    : (st.requested_set_id.has_value() ? *st.requested_set_id : "(pending)");

            const TyreSetState* set_ptr = nullptr;
            for (const auto& s : session_->tyreInventory()) {
                if (s.set_id == alloc_str) {
                    set_ptr = &s;
                    break;
                }
            }

            const std::string age_str = set_ptr ? (std::to_string(set_ptr->configured_age_laps) + " L") : "N/A";
            std::string health_str = "N/A";
            if (set_ptr != nullptr) {
                if (set_ptr->derived_starting_health.has_value()) {
                    std::ostringstream hss;
                    hss << std::fixed << std::setprecision(1)
                        << set_ptr->derived_starting_health->meanHealthPct() << "% ("
                        << toString(set_ptr->health_status) << ")";
                    health_str = hss.str();
                } else {
                    health_str = "UNAVAILABLE";
                }
            }

            const int start_lap = cum_lap + 1;
            const int end_lap = cum_lap + st.target_laps;
            cum_lap = end_lap;

            std::ostringstream win_ss;
            win_ss << "L" << start_lap << " - L" << end_lap;
            const std::string pit_lap_str =
                (i + 1 < plan.stints.size()) ? ("Lap " + std::to_string(end_lap)) : ("(Finish L" + std::to_string(end_lap) + ")");

            out_ << std::setw(5) << (i + 1) << " | "
                 << std::left << std::setw(6) << toString(st.role) << " | "
                 << std::setw(15) << comp_str << " | "
                 << std::setw(13) << req_str << " | "
                 << std::setw(13) << alloc_str << " | "
                 << std::right << std::setw(9) << age_str << " | "
                 << std::left << std::setw(29) << health_str << " | "
                 << std::setw(11) << win_ss.str() << " | "
                 << std::right << std::setw(11) << st.target_laps << " | "
                 << std::left << pit_lap_str << "\n";
        }
        out_ << "  Total Planned Laps: " << cum_lap << " / " << race_laps
             << " | Planned Pit Stops: " << (plan.stints.empty() ? 0 : static_cast<int>(plan.stints.size()) - 1) << "\n";
    }

    bool promptTyreRole(const std::string& prompt, TyreRole& out_role, bool& out_backed_out) {
        out_backed_out = false;
        while (!input_eof_) {
            out_ << prompt;
            out_.flush();
            std::string line;
            if (!readLineTrimmed(line)) {
                return false;
            }
            if (detail::isBackOrCancelToken(line)) {
                out_backed_out = true;
                return true;
            }
            const std::string upper = detail::toUpperAscii(line);
            if (upper == "1" || upper == "S" || upper == "SOFT") {
                out_role = TyreRole::Soft;
                return true;
            } else if (upper == "2" || upper == "M" || upper == "MEDIUM") {
                out_role = TyreRole::Medium;
                return true;
            } else if (upper == "3" || upper == "H" || upper == "HARD") {
                out_role = TyreRole::Hard;
                return true;
            }
            out_ << "  [INPUT ERROR] Invalid tyre role '" << line
                 << "'. Enter 1/S/SOFT, 2/M/MEDIUM, 3/H/HARD, or 'b' to cancel.\n";
        }
        return false;
    }

    bool promptSetIdOrAuto(
        const std::string& prompt,
        TyreRole /*expected_role*/,
        std::optional<std::string>& out_set_id,
        bool& out_backed_out) {
        out_backed_out = false;
        while (!input_eof_) {
            out_ << prompt;
            out_.flush();
            std::string line;
            if (!readLineTrimmed(line)) {
                return false;
            }
            if (detail::isBackOrCancelToken(line)) {
                out_backed_out = true;
                return true;
            }
            const std::string upper = detail::toUpperAscii(line);
            if (upper.empty() || upper == "AUTO" || upper == "A" || upper == "0") {
                out_set_id = std::nullopt;
                return true;
            }
            out_set_id = upper;
            return true;
        }
        return false;
    }

    bool collectStintSequenceFromUser(
        int required_total_laps,
        int min_stints,
        int max_stints,
        const std::vector<std::string>& already_used_sets,
        std::vector<StintSpec>& out_stints) {
        const EventBundle* bundle = session_->eventBundle();
        if (bundle == nullptr) {
            return false;
        }

        out_ << "  Target total laps to allocate: " << required_total_laps
             << " laps (regulatory stint count range: [" << min_stints << ", " << max_stints << "])\n";
        bool backed_out = false;
        int num_stints = 0;
        if (!promptIntInRange(
                "Enter number of stints [1-7] (or 'b' to cancel): ",
                1,
                7,
                num_stints,
                true,
                &backed_out)) {
            return false;
        }
        if (backed_out) {
            return false;
        }

        std::vector<StintSpec> stints;
        stints.reserve(static_cast<std::size_t>(num_stints));
        int laps_allocated_so_far = 0;

        for (int i = 0; i < num_stints; ++i) {
            const int remaining_to_plan = required_total_laps - laps_allocated_so_far;
            out_ << "\n  --- Configuring Stint " << (i + 1) << " of " << num_stints
                 << " (Remaining unallocated laps: " << remaining_to_plan << ") ---\n";
            out_ << "  Available Role Mappings: ";
            for (std::size_t m_idx = 0; m_idx < bundle->role_mapping.mappings.size(); ++m_idx) {
                const auto& m = bundle->role_mapping.mappings[m_idx];
                out_ << toString(m.role_id) << "->" << toString(m.compound_id)
                     << (m_idx + 1 < bundle->role_mapping.mappings.size() ? ", " : "\n");
            }

            TyreRole role = TyreRole::Soft;
            if (!promptTyreRole(
                    "  Stint " + std::to_string(i + 1) + " Tyre Role [1=SOFT, 2=MEDIUM, 3=HARD, b=cancel]: ",
                    role,
                    backed_out)) {
                return false;
            }
            if (backed_out) {
                return false;
            }

            const auto mapped_cid = bundle->role_mapping.compoundForRole(role);
            out_ << "  Selected Role: " << toString(role) << " (Maps to Pirelli "
                 << (mapped_cid.has_value() ? toString(*mapped_cid) : "UNMAPPED") << ")\n";
            out_ << "  Inventory Sets for " << toString(role) << ": ";
            for (const auto& s : session_->tyreInventory()) {
                if (s.role != role) continue;
                bool used = s.consumed_in_current_run || s.currently_fitted ||
                            (std::find(already_used_sets.begin(), already_used_sets.end(), s.set_id) !=
                             already_used_sets.end());
                out_ << s.set_id << "(age=" << s.configured_age_laps << "L, health=";
                if (s.derived_starting_health.has_value()) {
                    out_ << std::fixed << std::setprecision(1) << s.derived_starting_health->meanHealthPct() << "%";
                } else {
                    out_ << "UNAVAIL";
                }
                if (used) {
                    out_ << ", USED";
                }
                out_ << ") ";
            }
            out_ << "\n";

            std::optional<std::string> req_set;
            if (!promptSetIdOrAuto(
                    "  Stint " + std::to_string(i + 1) +
                        " Set ID [S1..S3/M1..M2/H1..H2 or AUTO, b=cancel]: ",
                    role,
                    req_set,
                    backed_out)) {
                return false;
            }
            if (backed_out) {
                return false;
            }

            int stint_laps = 0;
            if (!promptIntInRange(
                    "  Stint " + std::to_string(i + 1) + " Target Laps [1-" +
                        std::to_string(bundle->event_config.race_distance_laps) +
                        ", suggested remaining=" + std::to_string(std::max(1, remaining_to_plan)) +
                        ", b=cancel]: ",
                    1,
                    bundle->event_config.race_distance_laps,
                    stint_laps,
                    true,
                    &backed_out)) {
                return false;
            }
            if (backed_out) {
                return false;
            }

            StintSpec spec;
            spec.role = role;
            spec.target_laps = stint_laps;
            spec.requested_set_id = req_set;
            stints.push_back(std::move(spec));
            laps_allocated_so_far += stint_laps;
        }

        out_stints = std::move(stints);
        return true;
    }

    void handleStrategyBuilderMenu() {
        if (session_->eventBundle() == nullptr) {
            out_ << "  [NOTICE] Please select an event first (Option 1).\n";
            return;
        }

        if (draft_strategy_.stints.empty() && !session_->strategyPlan().stints.empty()) {
            draft_strategy_ = session_->strategyPlan();
        }

        const EventBundle& bundle = *session_->eventBundle();
        const int race_laps = bundle.event_config.race_distance_laps;

        while (!input_eof_) {
            out_ << "\n================================================================================\n";
            out_ << "RACE STRATEGY BUILDER — " << bundle.event_config.official_event_name
                 << " (" << race_laps << " laps)\n";
            out_ << "================================================================================\n";
            out_ << "  Rules: Total Laps = " << race_laps
                 << " | Allowed Stops = [" << bundle.event_config.min_pit_stops << ", "
                 << bundle.event_config.max_pit_stops << "] (Stints: ["
                 << (bundle.event_config.min_pit_stops + 1) << ", "
                 << (bundle.event_config.max_pit_stops + 1) << "])"
                 << " | Min Stint = " << bundle.event_config.min_stint_length_laps << " lap(s)"
                 << " | Min Distinct Compounds = " << bundle.event_config.min_distinct_dry_compounds << "\n";

            TyreInventoryManager preview_inv;
            preview_inv.restoreSetsFromSnapshot(session_->tyreInventory());
            auto alloc_preview = StrategyAndReadinessValidator::validateInitialStrategy(
                draft_strategy_, bundle, preview_inv);

            printStrategyPreviewTable(
                draft_strategy_,
                alloc_preview.ok() ? &(*alloc_preview.value) : nullptr);

            if (!draft_strategy_.stints.empty()) {
                if (alloc_preview.ok()) {
                    out_ << "  Draft Validation Status: VALID (all regulatory, distance, and inventory rules satisfied)\n";
                } else {
                    out_ << "  Draft Validation Status: INVALID — Engine diagnostics:\n";
                    printDiagnosticsList(out_, alloc_preview.diagnostics, true, "    ");
                }
            }

            out_ << "\nStrategy Builder Menu:\n"
                 << "  1. Build a new strategy from scratch\n"
                 << "  2. Append a stint to draft strategy\n"
                 << "  3. Edit an existing stint in draft strategy\n"
                 << "  4. Remove a stint from draft strategy\n"
                 << "  5. Clear all stints\n"
                 << "  6. Validate and commit draft strategy to session\n"
                 << "  7. Return to main menu\n";

            int choice = 0;
            if (!promptMenuChoice("Select strategy builder option [1-7]: ", 1, 7, choice)) {
                return;
            }

            if (choice == 1) {
                std::vector<StintSpec> new_stints;
                if (collectStintSequenceFromUser(
                        race_laps,
                        bundle.event_config.min_pit_stops + 1,
                        bundle.event_config.max_pit_stops + 1,
                        {},
                        new_stints)) {
                    draft_strategy_.stints = std::move(new_stints);
                    auto val_res = StrategyAndReadinessValidator::validateInitialStrategy(
                        draft_strategy_, bundle, preview_inv);
                    printStrategyPreviewTable(
                        draft_strategy_,
                        val_res.ok() ? &(*val_res.value) : nullptr);

                    if (!val_res.ok()) {
                        out_ << "  [STRATEGY VALIDATION FAILED] The engine rejected this strategy:\n";
                        printDiagnosticsList(out_, val_res.diagnostics, true, "    ");
                        out_ << "  You can edit individual stints (Option 3) or rebuild (Option 1).\n";
                    } else {
                        out_ << "  [STRATEGY VALID] All stint lengths, role mappings, and physical set allocations are valid.\n";
                        bool confirm_commit = false;
                        if (!promptYesNo("Commit this strategy to the active race session now? (y/n): ", true, confirm_commit)) {
                            return;
                        }
                        if (confirm_commit) {
                            session_->setStrategyPlan(draft_strategy_);
                            out_ << "  Strategy committed to session.\n";
                        }
                    }
                }
            } else if (choice == 2) {
                if (draft_strategy_.stints.size() >= 7) {
                    out_ << "  [ERROR] Cannot exceed 7 stints (only 7 physical tyre sets exist in inventory).\n";
                    continue;
                }
                bool backed_out = false;
                TyreRole role = TyreRole::Soft;
                if (!promptTyreRole("  New Stint Tyre Role [1=SOFT, 2=MEDIUM, 3=HARD, b=cancel]: ", role, backed_out) ||
                    backed_out) {
                    continue;
                }
                std::optional<std::string> req_set;
                if (!promptSetIdOrAuto("  New Stint Set ID [S1..H2 or AUTO, b=cancel]: ", role, req_set, backed_out) ||
                    backed_out) {
                    continue;
                }
                int laps = 0;
                if (!promptIntInRange("  New Stint Target Laps [1-" + std::to_string(race_laps) + ", b=cancel]: ",
                                     1, race_laps, laps, true, &backed_out) ||
                    backed_out) {
                    continue;
                }
                draft_strategy_.stints.push_back(StintSpec{role, laps, req_set});
            } else if (choice == 3) {
                if (draft_strategy_.stints.empty()) {
                    out_ << "  [ERROR] No stints in draft strategy to edit.\n";
                    continue;
                }
                bool backed_out = false;
                int stint_num = 1;
                if (!promptIntInRange(
                        "  Enter stint number to edit [1-" + std::to_string(draft_strategy_.stints.size()) +
                            ", b=cancel]: ",
                        1,
                        static_cast<int>(draft_strategy_.stints.size()),
                        stint_num,
                        true,
                        &backed_out) ||
                    backed_out) {
                    continue;
                }
                StintSpec& target = draft_strategy_.stints[static_cast<std::size_t>(stint_num - 1)];
                TyreRole role = target.role;
                if (!promptTyreRole("  Updated Tyre Role [1=SOFT, 2=MEDIUM, 3=HARD, b=cancel]: ", role, backed_out) ||
                    backed_out) {
                    continue;
                }
                std::optional<std::string> req_set = target.requested_set_id;
                if (!promptSetIdOrAuto("  Updated Set ID [S1..H2 or AUTO, b=cancel]: ", role, req_set, backed_out) ||
                    backed_out) {
                    continue;
                }
                int laps = target.target_laps;
                if (!promptIntInRange("  Updated Target Laps [1-" + std::to_string(race_laps) + ", b=cancel]: ",
                                     1, race_laps, laps, true, &backed_out) ||
                    backed_out) {
                    continue;
                }
                target.role = role;
                target.requested_set_id = req_set;
                target.target_laps = laps;
            } else if (choice == 4) {
                if (draft_strategy_.stints.empty()) {
                    out_ << "  [ERROR] No stints in draft strategy to remove.\n";
                    continue;
                }
                bool backed_out = false;
                int stint_num = 1;
                if (!promptIntInRange(
                        "  Enter stint number to remove [1-" + std::to_string(draft_strategy_.stints.size()) +
                            ", b=cancel]: ",
                        1,
                        static_cast<int>(draft_strategy_.stints.size()),
                        stint_num,
                        true,
                        &backed_out) ||
                    backed_out) {
                    continue;
                }
                draft_strategy_.stints.erase(draft_strategy_.stints.begin() + (stint_num - 1));
                out_ << "  Removed stint #" << stint_num << ".\n";
            } else if (choice == 5) {
                draft_strategy_.stints.clear();
                session_->setStrategyPlan(StrategyPlan{});
                out_ << "  Cleared all stints.\n";
            } else if (choice == 6) {
                auto val_res = StrategyAndReadinessValidator::validateInitialStrategy(
                    draft_strategy_, bundle, preview_inv);
                if (!val_res.ok()) {
                    out_ << "  [CANNOT COMMIT INVALID STRATEGY] Please resolve the following engine validation errors:\n";
                    printDiagnosticsList(out_, val_res.diagnostics, true, "    ");
                } else {
                    printStrategyPreviewTable(draft_strategy_, &(*val_res.value));
                    bool confirmed = false;
                    if (!promptYesNo("Confirm committing this validated strategy to the session? (y/n): ", true, confirmed)) {
                        return;
                    }
                    if (confirmed) {
                        session_->setStrategyPlan(draft_strategy_);
                        out_ << "  Strategy successfully committed to session!\n";
                    }
                }
            } else if (choice == 7) {
                return;
            }
        }
    }

    void handleRaceSettingsMenu() {
        while (!input_eof_) {
            out_ << "\n================================================================================\n";
            out_ << "RACE SETTINGS CONFIGURATION\n";
            out_ << "================================================================================\n";
            out_ << "  1. Race Mode                 : " << toString(session_->raceMode()) << "\n";
            out_ << "  2. Mathematical Model Stage  : " << toString(session_->modelStage()) << "\n";
            out_ << "  3. Driver Mass (kg)          : " << std::fixed << std::setprecision(1)
                 << session_->vehicleConfig().driver_mass_kg << " kg (accepted range: [40.0, 160.0] kg)\n";
            out_ << "  4. Calibration Gate Policy   : "
                 << (opts_.strict_calibrated
                         ? "STRICT CALIBRATED ONLY (blocks uncalibrated raw datasets)"
                         : "LOWER-FIDELITY PERMITTED (uses declared ESTIMATED supplement with user confirmation)")
                 << "\n";
            out_ << "  5. Verbose Lap-Trace Output  : " << (opts_.verbose_laps ? "ON (all laps)" : "OFF (milestone laps)") << "\n";
            out_ << "  6. Return to main menu\n";

            int choice = 0;
            if (!promptMenuChoice("Select race setting to configure [1-6]: ", 1, 6, choice)) {
                return;
            }

            if (choice == 1) {
                out_ << "  Select Race Mode:\n"
                     << "    1. PREPLANNED (autonomous execution with automatic mandatory-stop shortfall cascading)\n"
                     << "    2. INTERACTIVE (prompts for real user decisions at pit windows and mandatory stops)\n";
                int mode_choice = 0;
                if (!promptMenuChoice("  Enter mode [1-2]: ", 1, 2, mode_choice)) {
                    return;
                }
                const RaceMode new_mode = (mode_choice == 2) ? RaceMode::Interactive : RaceMode::Preplanned;
                auto res = session_->setRaceMode(new_mode);
                if (!res.ok()) {
                    printDiagnosticsList(out_, res.diagnostics, true, "  ");
                } else {
                    opts_.race_mode = new_mode;
                    out_ << "  Race mode updated to " << toString(new_mode) << ".\n";
                }
            } else if (choice == 2) {
                out_ << "  Select Mathematical Model Stage:\n"
                     << "    1. M0 — Deterministic Baseline (symmetric 4-corner wear, linear degradation)\n"
                     << "    2. M1 — Track-Aware Calibrated Dry (circuit energy indices, surface abrasiveness, track evolution)\n"
                     << "    3. M2 — Dynamic Environment Dry (temperature degradation multiplier F_env)\n"
                     << "    4. M3 — Nonlinear Cliff & Four-Corner Asymmetry (asymmetric corner wear & cliff threshold)\n";
                int st_choice = 0;
                if (!promptMenuChoice("  Enter model stage [1-4]: ", 1, 4, st_choice)) {
                    return;
                }
                ModelStage new_stage = ModelStage::M1_TrackAwareCalibratedDry;
                if (st_choice == 1) new_stage = ModelStage::M0_DeterministicBaseline;
                if (st_choice == 2) new_stage = ModelStage::M1_TrackAwareCalibratedDry;
                if (st_choice == 3) new_stage = ModelStage::M2_DynamicEnvironmentDry;
                if (st_choice == 4) new_stage = ModelStage::M3_NonlinearCliffAndFourCorner;

                auto res = session_->setModelStage(new_stage);
                if (!res.ok()) {
                    printDiagnosticsList(out_, res.diagnostics, true, "  ");
                } else {
                    opts_.model_stage = new_stage;
                    out_ << "  Model stage updated to " << toString(new_stage)
                         << " (tyre starting health recomputed for active stage).\n";
                }
            } else if (choice == 3) {
                bool backed_out = false;
                double mass_kg = session_->vehicleConfig().driver_mass_kg;
                if (!promptDoubleInRange(
                        "  Enter driver mass in kg [40.0 - 160.0] (default 75.0, or 'b' to cancel): ",
                        40.0,
                        160.0,
                        mass_kg,
                        true,
                        &backed_out)) {
                    return;
                }
                if (!backed_out) {
                    auto res = session_->setDriverMassKg(mass_kg);
                    if (!res.ok()) {
                        printDiagnosticsList(out_, res.diagnostics, true, "  ");
                    } else {
                        opts_.driver_mass_kg = mass_kg;
                        out_ << "  Driver mass updated to " << std::fixed << std::setprecision(1) << mass_kg << " kg.\n";
                    }
                }
            } else if (choice == 4) {
                opts_.strict_calibrated = !opts_.strict_calibrated;
                if (opts_.strict_calibrated) {
                    ModelParameters strict_params;
                    strict_params.allow_estimated_or_assumed_parameters = false;
                    session_->setModelParameters(strict_params);
                    out_ << "  Strict Calibrated Gate ENABLED: uncalibrated raw datasets will block execution.\n";
                } else {
                    session_->setModelParameters(makeDeclaredLowerFidelitySmokeTestParams(true));
                    out_ << "  Declared Lower-Fidelity Supplement ENABLED: estimated parameters permitted with pre-race confirmation.\n";
                }
            } else if (choice == 5) {
                opts_.verbose_laps = !opts_.verbose_laps;
                out_ << "  Verbose lap-trace output is now " << (opts_.verbose_laps ? "ON" : "OFF") << ".\n";
            } else if (choice == 6) {
                return;
            }
        }
    }

    void handleInspectReadinessMenu() const {
        out_ << "\n================================================================================\n";
        out_ << "DATA READINESS, CALIBRATION AUDIT & MODEL ASSUMPTIONS\n";
        out_ << "================================================================================\n";

        if (session_->eventBundle() == nullptr) {
            out_ << "  No event bundle is currently loaded. Select an event in Option 1 first.\n";
            return;
        }

        const EventBundle& bundle = *session_->eventBundle();
        const Result<void> raw_dal_suff = dal_->checkBaselineSimulationSufficiency(bundle);

        TyreInventoryManager raw_inv;
        ModelParameters raw_empty_params;
        raw_empty_params.allow_estimated_or_assumed_parameters = false;
        raw_inv.resetForNewEvent(&bundle, session_->vehicleConfig(), raw_empty_params, session_->modelStage());
        const CapabilityReadinessReport raw_report = StrategyAndReadinessValidator::assessReadiness(
            &bundle,
            LoadStatus::Success,
            {},
            session_->vehicleConfig(),
            session_->environmentConfig(),
            raw_empty_params,
            raw_inv,
            session_->modelStage(),
            nullptr);

        out_ << "[1] Raw On-Disk Dataset Readiness ('" << bundle.event_config.event_id << "')\n";
        out_ << "  Bundle Schema Load Valid : " << (raw_report.bundle_load_valid ? "YES" : "NO") << "\n";
        out_ << "  Compound Catalogue Status: " << bundle.compound_catalogue.calibration_status << "\n";
        out_ << "  Raw DAL Sufficiency      : " << toString(raw_dal_suff.status)
             << " (" << raw_dal_suff.diagnostics.size() << " uncalibrated compound fields)\n";
        out_ << "  Raw Fidelity Status      : " << toString(raw_report.fidelity_status) << "\n";
        out_ << "  Raw Execution Permission : " << toString(raw_report.execution_permission) << "\n";
        if (!raw_report.parameter_gaps.empty()) {
            out_ << "  Missing / Uncalibrated Required Parameters in Raw Dataset:\n";
            for (const auto& gap : raw_report.parameter_gaps) {
                out_ << "    - " << gap.field_path << " [" << toString(gap.status)
                     << " -> " << toString(gap.missing_policy) << "]: " << gap.detail << "\n";
            }
        }

        const CapabilityReadinessReport active_report = session_->assessReadiness();
        out_ << "\n[2] Active Session Readiness & Parameter Supplements\n";
        out_ << "  Strict Calibrated Gate   : " << (opts_.strict_calibrated ? "ENABLED (strict)" : "DISABLED (lower-fidelity permitted)") << "\n";
        out_ << "  Active Parameter Set ID  : " << session_->modelParameters().param_set_id << "\n";
        out_ << "  Parameter Source Status  : " << toString(session_->modelParameters().parameter_source_status) << "\n";
        out_ << "  Active Simulation Fidelity: " << toString(active_report.fidelity_status) << "\n";
        out_ << "  Active Execution Gate    : " << toString(active_report.execution_permission)
             << (active_report.canExecute() ? " (EXECUTION PERMITTED)" : " (EXECUTION BLOCKED)") << "\n";

        if (!opts_.strict_calibrated && session_->modelParameters().ref_lap_time_s.has_value()) {
            const ModelParameters& mp = session_->modelParameters();
            out_ << "\n[3] Declared Lower-Fidelity (ESTIMATED) Parameter Supplements in Use:\n";
            out_ << std::fixed << std::setprecision(3);
            out_ << "  Reference Lap Time (T_ref) : " << *mp.ref_lap_time_s << " s [ESTIMATED]\n";
            out_ << "  Net Pit-Stop Loss (P_ref)  : " << mp.pit_loss_total_s.value_or(0.0)
                 << " s (stationary service: " << std::setprecision(2) << mp.configured_stationary_time_s << " s) [ESTIMATED]\n";
            out_ << "  Fuel Sensitivity (gamma)   : " << std::setprecision(3) << mp.fuel_lap_sens_s_per_kg
                 << " s/kg | Standing Start Loss: " << std::setprecision(2) << mp.lap1_standing_start_loss_s
                 << " s | Outlap Loss: " << mp.outlap_warmup_loss_s << " s\n";
            out_ << "  Estimated Compound Overrides (C1..C5):\n";
            for (const auto& kv : mp.compound_overrides) {
                const auto& ov = kv.second;
                out_ << "    - " << toString(kv.first) << " [" << toString(ov.source_status) << "]: "
                     << "pace_offset=" << std::showpos << std::setprecision(3) << ov.pace_offset_ref_s.value_or(0.0)
                     << std::noshowpos << " s, base_wear_rate=" << ov.base_wear_rate_per_lap.value_or(0.0)
                     << " /lap, linear_deg_coeff=" << ov.linear_deg_coeff_s.value_or(0.0) << " s\n";
            }
        }

        if (!active_report.diagnostics.empty()) {
            out_ << "\n[4] Active Readiness Diagnostics:\n";
            printDiagnosticsList(out_, active_report.diagnostics, false, "  ");
        }
    }

    void printPreRaceConfigurationSummary(
        const std::vector<std::string>& allocated_sets,
        const CapabilityReadinessReport& readiness) const {
        const EventBundle& b = *session_->eventBundle();
        out_ << "\n================================================================================\n";
        out_ << "PRE-RACE CONFIGURATION SUMMARY\n";
        out_ << "================================================================================\n";
        out_ << "  Event & Track      : " << b.event_config.official_event_name << " ("
             << b.event_config.event_id << " @ " << b.track_layout.track_name << ")\n";
        out_ << "  Race Distance      : " << b.event_config.race_distance_laps << " laps ("
             << std::fixed << std::setprecision(3) << b.event_config.scheduled_race_distance_km << " km)\n";
        out_ << "  Operating Mode     : " << toString(session_->raceMode()) << "\n";
        out_ << "  Model Stage        : " << toString(session_->modelStage()) << "\n";
        out_ << "  Driver Mass        : " << std::fixed << std::setprecision(1)
             << session_->vehicleConfig().driver_mass_kg << " kg (initial fuel: "
             << session_->vehicleConfig().initial_fuel_load_kg << " kg)\n";
        out_ << "  Active Fidelity    : " << toString(readiness.fidelity_status) << "\n";
        out_ << "  Execution Gate     : " << toString(readiness.execution_permission) << "\n";

        printStrategyPreviewTable(session_->strategyPlan(), &allocated_sets);
        out_ << "================================================================================\n";
    }

    void handleStartSimulationFlow() {
        while (!input_eof_) {
            if (session_->eventBundle() == nullptr) {
                out_ << "\n[CANNOT START] No event is currently selected. Use Option 1 to select an event.\n";
                return;
            }
            if (session_->strategyPlan().stints.empty()) {
                out_ << "\n[CANNOT START] No race strategy is configured. Use Option 3 to build a strategy first.\n";
                return;
            }

            const EventBundle& bundle = *session_->eventBundle();
            TyreInventoryManager preview_inv;
            preview_inv.restoreSetsFromSnapshot(session_->tyreInventory());
            auto alloc_res = StrategyAndReadinessValidator::validateInitialStrategy(
                session_->strategyPlan(), bundle, preview_inv);
            if (!alloc_res.ok()) {
                out_ << "\n[STRATEGY VALIDATION BLOCKED] Current strategy is invalid:\n";
                printDiagnosticsList(out_, alloc_res.diagnostics, true, "  ");
                return;
            }

            auto val_res = session_->validate();
            if (!val_res.ok()) {
                out_ << "\n[EXECUTION BLOCKED BY ENGINE READINESS GATE] (Status: "
                     << toString(val_res.status) << "):\n";
                printDiagnosticsList(out_, val_res.diagnostics, true, "  ");
                return;
            }

            const CapabilityReadinessReport& readiness = *val_res.value;
            printPreRaceConfigurationSummary(*alloc_res.value, readiness);

            if (readiness.execution_permission == ExecutionPermission::AllowedLowerFidelity) {
                out_ << "\n[CALIBRATION HONESTY NOTICE]\n"
                     << "  The raw on-disk compound dataset for '" << bundle.event_config.event_id
                     << "' contains uncalibrated (null) wear/degradation coefficients.\n"
                     << "  Execution is permitted ONLY at lower fidelity (ESTIMATED_OR_ASSUMED_LOWER_FIDELITY)\n"
                     << "  using the declared estimated parameter supplement.\n";
                bool confirm_low_fidelity = false;
                if (!promptYesNo("Proceed with lower-fidelity simulation? (y/n): ", true, confirm_low_fidelity)) {
                    return;
                }
                if (!confirm_low_fidelity) {
                    out_ << "Simulation start declined by user. Returning to configuration menu.\n";
                    return;
                }
            }

            auto start_res = session_->start();
            if (!start_res.ok()) {
                err_ << "[SIMULATION ERROR] Engine failed to start simulation ("
                     << toString(start_res.status) << "):\n";
                printDiagnosticsList(err_, start_res.diagnostics, true, "  ");
                return;
            }

            out_ << "\n[SIMULATION STARTED] Mode: " << toString(session_->raceMode())
                 << " | Opening Set: " << start_res.value->active_tyre_set_id << "\n";

            if (!runActiveSimulationLoop()) {
                return;
            }

            if (session_->lastResult().has_value()) {
                printRaceResultReport(
                    out_, *session_->lastResult(), session_->tyreInventory(), opts_.verbose_laps);
            }

            bool exit_app = false;
            const bool repeat_immediately = handlePostRaceMenu(exit_app);
            if (exit_app) {
                input_eof_ = true;
                return;
            }
            if (!repeat_immediately) {
                return;
            }
        }
    }

    void printLiveSnapshotSummary(const SessionSnapshot& snap) const {
        out_ << "\n--- Live Race Snapshot (State: " << toString(snap.session_state) << ") ---\n";
        out_ << "  Completed Laps     : " << snap.completed_laps << " / " << snap.total_race_laps
             << " (Remaining: " << (snap.total_race_laps - snap.completed_laps) << " laps)\n";
        out_ << "  Current Stint      : #" << (snap.current_stint_index + 1)
             << " on Set " << snap.active_tyre_set_id << "\n";
        for (const auto& s : snap.tyre_inventory) {
            if (s.set_id == snap.active_tyre_set_id) {
                out_ << "  Active Tyre State  : Age=" << s.runtime_age_laps << " laps | Health="
                     << detail::formatRuntimeHealthSummary(s.runtime_health) << "\n";
                break;
            }
        }
        out_ << std::fixed << std::setprecision(3);
        out_ << "  Cumulative Elapsed : " << snap.elapsed_race_time_s << " s ("
             << formatRaceDuration(snap.elapsed_race_time_s) << ") | Pit Stops: "
             << snap.pit_stops.size() << "\n";
        out_ << "  Fuel Remaining     : " << std::setprecision(2) << snap.current_fuel_remaining_kg << " kg\n";
    }

    bool handleStepByStepMenu(bool& out_switch_to_auto) {
        out_switch_to_auto = false;
        while (!input_eof_ &&
               (session_->state() == SessionState::Running || session_->state() == SessionState::Paused)) {
            const SessionSnapshot snap = session_->snapshot();
            out_ << "\n[LAP BOUNDARY CONTROL | Lap " << snap.completed_laps << "/" << snap.total_race_laps
                 << " | State: " << toString(snap.session_state)
                 << " | Active Set: " << snap.active_tyre_set_id << "]\n";
            out_ << "  1. Run automatically until next decision point or race finish\n"
                 << "  2. Advance 1 lap\n"
                 << "  3. Advance N laps\n"
                 << "  4. " << (session_->state() == SessionState::Running ? "Pause simulation" : "Resume simulation") << "\n"
                 << "  5. Inspect live telemetry snapshot\n"
                 << "  6. Cancel active race\n";

            int choice = 0;
            if (!promptMenuChoice("Select execution action [1-6]: ", 1, 6, choice)) {
                return false;
            }

            if (choice == 1) {
                if (session_->state() == SessionState::Paused) {
                    auto res = session_->resume();
                    if (!res.ok()) {
                        printDiagnosticsList(out_, res.diagnostics, true, "  ");
                        continue;
                    }
                }
                out_switch_to_auto = true;
                return true;
            } else if (choice == 2 || choice == 3) {
                int laps_to_step = 1;
                if (choice == 3) {
                    const int rem = std::max(1, snap.total_race_laps - snap.completed_laps);
                    if (!promptIntInRange("  Enter number of laps to advance [1-" + std::to_string(rem) + "]: ",
                                          1, rem, laps_to_step)) {
                        return false;
                    }
                }
                if (session_->state() == SessionState::Paused) {
                    auto res = session_->resume();
                    if (!res.ok()) {
                        printDiagnosticsList(out_, res.diagnostics, true, "  ");
                        continue;
                    }
                    out_ << "  [RESUMED] Session transitioned from PAUSED to RUNNING.\n";
                }
                for (int step = 0; step < laps_to_step && session_->state() == SessionState::Running; ++step) {
                    auto lap_res = session_->advanceLap();
                    if (!lap_res.ok()) {
                        err_ << "  [SIMULATION ERROR] advanceLap() failed (" << toString(lap_res.status) << "):\n";
                        printDiagnosticsList(err_, lap_res.diagnostics, true, "    ");
                        return true;
                    }
                    const LapTraceEntry& lt = *lap_res.value;
                    out_ << "  -> Completed Lap " << lt.race_lap << "/" << snap.total_race_laps
                         << " (Stint #" << (lt.stint_index + 1) << ", Set " << lt.tyre_set_id
                         << "): flying=" << std::fixed << std::setprecision(3)
                         << lt.lap_breakdown.total_flying_lap_time_s << " s, end health="
                         << std::setprecision(1) << lt.health_end.meanHealthPct() << "%";
                    if (lt.pit_stop_occurred_after_lap) {
                        out_ << " [PIT STOP EXECUTED: +" << std::setprecision(3) << lt.pit_stop_loss_after_lap_s << " s]";
                    }
                    out_ << "\n";
                }
                if (session_->state() == SessionState::DecisionRequired ||
                    isTerminalState(session_->state())) {
                    return true;
                }
            } else if (choice == 4) {
                if (session_->state() == SessionState::Running) {
                    auto p_res = session_->pause();
                    if (!p_res.ok()) {
                        printDiagnosticsList(out_, p_res.diagnostics, true, "  ");
                    } else {
                        out_ << "  [PAUSED] Simulation paused at safe lap boundary after Lap "
                             << p_res.value->completed_laps << ".\n";
                    }
                } else if (session_->state() == SessionState::Paused) {
                    auto r_res = session_->resume();
                    if (!r_res.ok()) {
                        printDiagnosticsList(out_, r_res.diagnostics, true, "  ");
                    } else {
                        out_ << "  [RESUMED] Simulation resumed from Lap "
                             << r_res.value->completed_laps << ".\n";
                    }
                }
            } else if (choice == 5) {
                printLiveSnapshotSummary(session_->snapshot());
            } else if (choice == 6) {
                auto c_res = session_->cancel();
                if (!c_res.ok()) {
                    printDiagnosticsList(out_, c_res.diagnostics, true, "  ");
                } else {
                    out_ << "  [CANCELLED] Active race cancelled after Lap "
                         << c_res.value->completed_race_laps
                         << ". Runtime tyre inventory state restored to configured pre-race condition.\n";
                    return true;
                }
            }
        }
        return !input_eof_;
    }

    bool handleInteractiveDecisionPrompt(bool& out_cancelled) {
        out_cancelled = false;
        while (!input_eof_ && session_->state() == SessionState::DecisionRequired &&
               session_->pendingDecision().has_value()) {
            const PendingDecisionPrompt& prompt = *session_->pendingDecision();

            out_ << "\n================================================================================\n";
            out_ << "INTERACTIVE RACE DECISION REQUIRED — END OF LAP " << prompt.completed_race_lap << "\n";
            out_ << "================================================================================\n";
            out_ << "  Decision Reason      : " << toString(prompt.reason) << "\n";
            out_ << "  Engine Explanation   : " << prompt.explanation << "\n";
            out_ << "  Race Progress        : Completed Lap " << prompt.completed_race_lap
                 << " | Remaining Race Distance: " << prompt.remaining_race_laps << " lap(s)\n";
            out_ << "  Current Stint        : Stint #" << (prompt.current_stint_index + 1)
                 << " | Fitted Set: " << prompt.current_tyre_set_id << " ("
                 << toString(prompt.current_role) << " -> " << toString(prompt.current_compound) << ")\n";
            out_ << "  Current Stint Laps   : Actual = " << prompt.current_stint_actual_laps
                 << " / Target = " << prompt.current_stint_target_laps
                 << " (Shortfall: " << prompt.current_stint_shortfall_laps << " lap(s))\n";
            out_ << "  Current Tyre Health  : " << detail::formatRuntimeHealthSummary(prompt.current_tyre_health) << "\n";
            out_ << "  Mandatory Stop Active: "
                 << (prompt.mandatory_stop_required
                         ? "YES (Mean health < 30% or Corner health < 12% — EXTENSION PROHIBITED)"
                         : "NO (Extension permitted)")
                 << "\n";
            out_ << std::fixed << std::setprecision(2);
            out_ << "  Current Fuel & Time  : Fuel Remaining = " << prompt.current_fuel_remaining_kg
                 << " kg | Cumulative Elapsed = " << std::setprecision(3) << prompt.cumulative_elapsed_time_s
                 << " s (" << formatRaceDuration(prompt.cumulative_elapsed_time_s) << ")\n";

            out_ << "\n  --- Current Stint Plan Summary ---\n";
            int planned_rem_sum = 0;
            for (std::size_t j = 0; j < prompt.current_stint_plan.size(); ++j) {
                const auto& sr = prompt.current_stint_plan[j];
                const bool is_cur = (static_cast<int>(j) == prompt.current_stint_index);
                const bool is_fut = (static_cast<int>(j) > prompt.current_stint_index);
                if (is_fut) {
                    planned_rem_sum += sr.effective_target_laps;
                }
                out_ << "    Stint #" << (j + 1) << ": " << toString(sr.role) << "/"
                     << toString(sr.physical_compound) << " (Set " << sr.assigned_set_id
                     << ") | Target=" << sr.effective_target_laps
                     << "L, Actual=" << sr.actual_laps_completed << "L"
                     << (is_cur ? " <-- [ACTIVE STINT]" : (is_fut ? " [REMAINING PLANNED]" : " [COMPLETED]"))
                     << "\n";
            }
            out_ << "  Remaining Planned Stints Sum: " << planned_rem_sum
                 << " lap(s) vs. Remaining Race Distance: " << prompt.remaining_race_laps << " lap(s)\n";
            if (prompt.mandatory_stop_required && planned_rem_sum != prompt.remaining_race_laps) {
                out_ << "  [MANDATORY STOP RULE] Interactive mode does NOT automatically cascade the "
                     << prompt.current_stint_shortfall_laps
                     << "-lap shortfall into future stints. You must revise the remaining strategy (Option 3)\n"
                     << "                        so remaining stints sum to " << prompt.remaining_race_laps << " laps.\n";
            }

            out_ << "\nAvailable Interactive Decisions:\n";
            out_ << "  1. Keep original plan (pit now onto next planned stint)\n";
            if (prompt.extension_permitted && !prompt.mandatory_stop_required) {
                out_ << "  2. Extend current stint (add additional laps > 0 and revise remaining stints)\n";
            } else {
                out_ << "  2. [UNAVAILABLE — MANDATORY HEALTH STOP] Extend current stint (prohibited by engine when health < 30% mean / < 12% corner)\n";
            }
            out_ << "  3. Pit now with a revised plan (re-plan remaining stint sequence & sets for "
                 << prompt.remaining_race_laps << " laps)\n";
            out_ << "  4. Cancel active race\n";

            int choice = 0;
            if (!promptMenuChoice("Select decision [1-4]: ", 1, 4, choice)) {
                return false;
            }

            if (choice == 1) {
                InteractiveDecision decision;
                decision.action_type = DecisionActionType::KeepOriginalPlan;
                decision.user_note = "User selected KeepOriginalPlan at lap " +
                    std::to_string(prompt.completed_race_lap) + ".";
                auto dec_res = session_->applyDecision(decision);
                if (!dec_res.ok()) {
                    out_ << "\n  [DECISION REJECTED BY ENGINE] (" << toString(dec_res.status) << "):\n";
                    printDiagnosticsList(out_, dec_res.diagnostics, true, "    ");
                    out_ << "  Session remains at DECISION_REQUIRED. Please select a valid decision.\n";
                    continue;
                }
                out_ << "  [DECISION APPLIED] Kept original plan and executed pit stop at Lap "
                     << prompt.completed_race_lap << ".\n";
                return true;
            } else if (choice == 2) {
                if (!prompt.extension_permitted || prompt.mandatory_stop_required) {
                    InteractiveDecision forbidden_dec;
                    forbidden_dec.action_type = DecisionActionType::ExtendCurrentStint;
                    forbidden_dec.extension_laps = 1;
                    auto dec_res = session_->applyDecision(forbidden_dec);
                    out_ << "\n  [MANDATORY STOP CANNOT BE OVERRIDDEN] (" << toString(dec_res.status) << "):\n";
                    printDiagnosticsList(out_, dec_res.diagnostics, true, "    ");
                    out_ << "  A mandatory tyre-health stop is active; you must pit now (Option 3 or Option 1).\n";
                    continue;
                }

                bool backed_out = false;
                int ext_laps = 0;
                if (!promptIntInRange(
                        "  Enter number of ADDITIONAL laps to extend current stint [1-" +
                            std::to_string(prompt.remaining_race_laps) + ", b=cancel]: ",
                        1,
                        prompt.remaining_race_laps,
                        ext_laps,
                        true,
                        &backed_out)) {
                    return false;
                }
                if (backed_out) {
                    continue;
                }

                const int rem_after_ext = prompt.remaining_race_laps - ext_laps;
                out_ << "  [EXTENSION NOTE] Extending current stint by +" << ext_laps
                     << " lap(s) leaves " << rem_after_ext
                     << " lap(s) to be covered by future stints after the extended stint.\n"
                     << "                   The engine never automatically subtracts extension laps from future stints.\n";

                std::optional<std::vector<StintSpec>> revised_future;
                if (rem_after_ext == 0) {
                    revised_future = std::vector<StintSpec>{};
                } else {
                    bool revise_now = true;
                    if (!promptYesNo(
                            "  Revise remaining future stints now so they sum to " +
                                std::to_string(rem_after_ext) + " lap(s)? (y/n): ",
                            true,
                            revise_now)) {
                        return false;
                    }
                    if (revise_now) {
                        std::vector<std::string> used_sets;
                        for (int j = 0; j <= prompt.current_stint_index; ++j) {
                            used_sets.push_back(prompt.current_stint_plan[static_cast<std::size_t>(j)].assigned_set_id);
                        }
                        std::vector<StintSpec> collected;
                        if (!collectStintSequenceFromUser(rem_after_ext, 1, 6, used_sets, collected)) {
                            continue;
                        }
                        revised_future = std::move(collected);
                    }
                }

                InteractiveDecision decision;
                decision.action_type = DecisionActionType::ExtendCurrentStint;
                decision.extension_laps = ext_laps;
                decision.revised_remaining_stints = revised_future;
                decision.user_note = "User extended Stint #" +
                    std::to_string(prompt.current_stint_index + 1) + " by +" +
                    std::to_string(ext_laps) + " lap(s) at Lap " +
                    std::to_string(prompt.completed_race_lap) + ".";

                auto dec_res = session_->applyDecision(decision);
                if (!dec_res.ok()) {
                    out_ << "\n  [DECISION REJECTED BY ENGINE] (" << toString(dec_res.status) << "):\n";
                    printDiagnosticsList(out_, dec_res.diagnostics, true, "    ");
                    out_ << "  Session remains at DECISION_REQUIRED without modifying your active race state.\n";
                    continue;
                }
                out_ << "  [DECISION APPLIED] Extended current stint by +" << ext_laps
                     << " lap(s) and updated remaining stint plan.\n";
                return true;
            } else if (choice == 3) {
                std::vector<std::string> used_sets;
                for (int j = 0; j <= prompt.current_stint_index; ++j) {
                    used_sets.push_back(prompt.current_stint_plan[static_cast<std::size_t>(j)].assigned_set_id);
                }
                out_ << "\n  --- Revising Remaining Stint Plan for Remaining "
                     << prompt.remaining_race_laps << " Race Laps ---\n";
                out_ << "  Already Consumed / Fitted Sets in This Race: ";
                for (std::size_t u = 0; u < used_sets.size(); ++u) {
                    out_ << used_sets[u] << (u + 1 < used_sets.size() ? ", " : "\n");
                }

                std::vector<StintSpec> revised_stints;
                if (!collectStintSequenceFromUser(prompt.remaining_race_laps, 1, 6, used_sets, revised_stints)) {
                    continue;
                }

                InteractiveDecision decision;
                decision.action_type = DecisionActionType::PitNowWithRevisedPlan;
                decision.extension_laps = 0;
                decision.revised_remaining_stints = std::move(revised_stints);
                decision.user_note = "User pitted with revised remaining plan at Lap " +
                    std::to_string(prompt.completed_race_lap) + ".";

                auto dec_res = session_->applyDecision(decision);
                if (!dec_res.ok()) {
                    out_ << "\n  [DECISION REJECTED BY ENGINE] (" << toString(dec_res.status) << "):\n";
                    printDiagnosticsList(out_, dec_res.diagnostics, true, "    ");
                    out_ << "  Session remains at DECISION_REQUIRED without corrupting runtime state.\n";
                    continue;
                }
                out_ << "  [DECISION APPLIED] Pitted at Lap " << prompt.completed_race_lap
                     << " onto revised strategy plan.\n";
                return true;
            } else if (choice == 4) {
                auto c_res = session_->cancel();
                if (!c_res.ok()) {
                    printDiagnosticsList(out_, c_res.diagnostics, true, "  ");
                } else {
                    out_ << "  [CANCELLED] Race cancelled at decision point (Lap "
                         << c_res.value->completed_race_laps
                         << "). Runtime tyre inventory restored to pre-race state.\n";
                    out_cancelled = true;
                    return true;
                }
            }
        }
        return false;
    }

    bool runActiveSimulationLoop() {
        out_ << "\nExecution Control:\n"
             << "  1. Run automatically (advance to next decision point or race finish)\n"
             << "  2. Step-by-step / Pauseable lap control (advance 1 or N laps, pause, resume, cancel)\n";
        int exec_mode = 1;
        if (!promptMenuChoice("Select execution progression mode [1-2]: ", 1, 2, exec_mode)) {
            session_->cancel();
            return false;
        }

        bool auto_progression = (exec_mode == 1);

        while (!input_eof_ && isActiveRunState(session_->state())) {
            if (session_->state() == SessionState::DecisionRequired) {
                bool cancelled = false;
                if (!handleInteractiveDecisionPrompt(cancelled)) {
                    session_->cancel();
                    return false;
                }
                if (cancelled || isTerminalState(session_->state())) {
                    return true;
                }
                if (auto_progression && session_->state() == SessionState::Paused) {
                    auto r_res = session_->resume();
                    if (!r_res.ok()) {
                        err_ << "[SIMULATION ERROR] Failed to resume session after decision:\n";
                        printDiagnosticsList(err_, r_res.diagnostics, true, "  ");
                        return false;
                    }
                }
                continue;
            }

            if (!auto_progression) {
                bool switched_to_auto = false;
                if (!handleStepByStepMenu(switched_to_auto)) {
                    session_->cancel();
                    return false;
                }
                if (switched_to_auto) {
                    auto_progression = true;
                }
                continue;
            }

            const std::size_t prev_stops = session_->snapshot().pit_stops.size();
            auto run_res = session_->runUntilStopOrCompletion();
            if (!run_res.ok()) {
                err_ << "\n[SIMULATION RUNTIME FAILURE] (" << toString(run_res.status) << "):\n";
                printDiagnosticsList(err_, run_res.diagnostics, true, "  ");
                return true;
            }

            const SessionSnapshot snap = session_->snapshot();
            for (std::size_t s_idx = prev_stops; s_idx < snap.pit_stops.size(); ++s_idx) {
                const auto& ps = snap.pit_stops[s_idx];
                out_ << "  [PIT STOP #" << ps.stop_sequence_number << " @ Lap " << ps.completed_race_lap
                     << " | " << toString(ps.reason) << "] " << ps.set_removed_id
                     << " -> " << ps.set_fitted_id << " (loss: " << std::fixed << std::setprecision(3)
                     << ps.total_stop_time_loss_s << " s)\n";
            }
        }

        return true;
    }

    bool handlePostRaceMenu(bool& out_exit_app) {
        out_exit_app = false;
        while (!input_eof_) {
            out_ << "\nPost-Simulation Options:\n"
                 << "  1. Run another simulation with the same event, inventory ages, and strategy\n"
                 << "  2. Edit the strategy\n"
                 << "  3. Edit tyre-set ages\n"
                 << "  4. Change the event\n"
                 << "  5. Return to the main menu\n"
                 << "  6. Exit\n";

            int choice = 0;
            if (!promptMenuChoice("Select post-simulation option [1-6]: ", 1, 6, choice)) {
                out_exit_app = true;
                return false;
            }

            if (choice == 1) {
                session_->resetForNewRun();
                return true;
            } else if (choice == 2) {
                session_->resetForNewRun();
                handleStrategyBuilderMenu();
                return false;
            } else if (choice == 3) {
                session_->resetForNewRun();
                handleTyreInventoryMenu();
                return false;
            } else if (choice == 4) {
                session_->resetForNewRun();
                handleSelectEventMenu();
                return false;
            } else if (choice == 5) {
                session_->resetForNewRun();
                return false;
            } else if (choice == 6) {
                out_ << "Exiting F1_Sim. Goodbye!\n";
                out_exit_app = true;
                return false;
            }
        }
        out_exit_app = true;
        return false;
    }

    bool readLineTrimmed(std::string& out_line) {
        if (input_eof_ || !in_.good()) {
            input_eof_ = true;
            return false;
        }
        std::string raw;
        if (!std::getline(in_, raw)) {
            input_eof_ = true;
            return false;
        }
        out_line = detail::trimString(raw);
        return true;
    }

    bool promptMenuChoice(const std::string& prompt, int min_val, int max_val, int& out_choice) {
        return promptIntInRange(prompt, min_val, max_val, out_choice, false, nullptr);
    }

    bool promptIntInRange(
        const std::string& prompt,
        int min_val,
        int max_val,
        int& out_val,
        bool allow_back = false,
        bool* out_backed_out = nullptr) {
        if (out_backed_out != nullptr) {
            *out_backed_out = false;
        }
        while (!input_eof_) {
            out_ << prompt;
            out_.flush();
            std::string line;
            if (!readLineTrimmed(line)) {
                return false;
            }
            if (line.empty()) {
                out_ << "  [INPUT ERROR] Input cannot be empty. Expected an integer in ["
                     << min_val << ", " << max_val << "].\n";
                continue;
            }
            if (allow_back && detail::isBackOrCancelToken(line)) {
                if (out_backed_out != nullptr) {
                    *out_backed_out = true;
                }
                return true;
            }

            char* end_ptr = nullptr;
            const long parsed = std::strtol(line.c_str(), &end_ptr, 10);
            if (end_ptr == line.c_str() || *end_ptr != '\0') {
                out_ << "  [INPUT ERROR] Non-numeric input '" << line
                     << "'. Please enter a valid integer in [" << min_val << ", " << max_val << "].\n";
                continue;
            }
            if (parsed < static_cast<long>(min_val) || parsed > static_cast<long>(max_val)) {
                out_ << "  [INPUT ERROR] Value " << parsed << " is out of range ["
                     << min_val << ", " << max_val << "].\n";
                continue;
            }
            out_val = static_cast<int>(parsed);
            return true;
        }
        return false;
    }

    bool promptDoubleInRange(
        const std::string& prompt,
        double min_val,
        double max_val,
        double& out_val,
        bool allow_back = false,
        bool* out_backed_out = nullptr) {
        if (out_backed_out != nullptr) {
            *out_backed_out = false;
        }
        while (!input_eof_) {
            out_ << prompt;
            out_.flush();
            std::string line;
            if (!readLineTrimmed(line)) {
                return false;
            }
            if (line.empty()) {
                out_ << "  [INPUT ERROR] Input cannot be empty. Expected a number in ["
                     << min_val << ", " << max_val << "].\n";
                continue;
            }
            if (allow_back && detail::isBackOrCancelToken(line)) {
                if (out_backed_out != nullptr) {
                    *out_backed_out = true;
                }
                return true;
            }

            char* end_ptr = nullptr;
            const double parsed = std::strtod(line.c_str(), &end_ptr);
            if (end_ptr == line.c_str() || *end_ptr != '\0' || !std::isfinite(parsed)) {
                out_ << "  [INPUT ERROR] Invalid numeric input '" << line
                     << "'. Please enter a finite number in [" << min_val << ", " << max_val << "].\n";
                continue;
            }
            if (parsed < min_val || parsed > max_val) {
                out_ << "  [INPUT ERROR] Value " << parsed << " is outside accepted range ["
                     << min_val << ", " << max_val << "].\n";
                continue;
            }
            out_val = parsed;
            return true;
        }
        return false;
    }

    bool promptYesNo(const std::string& prompt, bool default_yes, bool& out_yes) {
        while (!input_eof_) {
            out_ << prompt;
            out_.flush();
            std::string line;
            if (!readLineTrimmed(line)) {
                return false;
            }
            if (line.empty()) {
                out_yes = default_yes;
                return true;
            }
            const std::string lower = detail::toLowerAscii(line);
            if (lower == "y" || lower == "yes" || lower == "1") {
                out_yes = true;
                return true;
            }
            if (lower == "n" || lower == "no" || lower == "0" || detail::isBackOrCancelToken(lower)) {
                out_yes = false;
                return true;
            }
            out_ << "  [INPUT ERROR] Please enter 'y' (yes) or 'n' (no).\n";
        }
        return false;
    }

    CliOptions opts_;
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    bool input_eof_ = false;

    fs::path effective_data_root_;
    std::shared_ptr<InMemoryDiagnosticCollector> diag_collector_;
    std::shared_ptr<DataAccessLayer> dal_;
    std::unique_ptr<RaceSession> session_;
    StrategyPlan draft_strategy_;
};

} // namespace cli
} // namespace f1_sim

#ifndef F1_SIM_CLI_LIBRARY_ONLY
int main(int argc, char** argv) {
    f1_sim::cli::CliOptions opts;
    std::string cli_error;
    if (!f1_sim::cli::parseCliArgs(argc, argv, opts, cli_error)) {
        std::cerr << "[CLI ERROR] " << cli_error << "\n";
        return f1_sim::cli::kExitCliUsageError;
    }

    f1_sim::cli::CliApp app(opts, std::cin, std::cout, std::cerr);
    return app.run(argc > 0 ? argv[0] : "f1_sim");
}
#endif // F1_SIM_CLI_LIBRARY_ONLY