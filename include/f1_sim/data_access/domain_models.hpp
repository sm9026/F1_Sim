#ifndef F1_SIM_DATA_ACCESS_DOMAIN_MODELS_HPP
#define F1_SIM_DATA_ACCESS_DOMAIN_MODELS_HPP

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace f1_sim {
namespace data_access {

/**
 * @brief Epistemological status of a parameter record or field group
 *        (Mathematical Framework Section 4.1).
 */
enum class SourceStatus {
    PublishedObserved,
    Derived,
    Calibrated,
    Estimated,
    Assumed,
    UnknownUnavailable
};

/**
 * @brief Qualitative reliability tier for a parameter or field group.
 */
enum class ConfidenceTier {
    High,
    Medium,
    Low,
    Provisional
};

/**
 * @brief Declared instruction to the Simulation Engine when a nullable field is null/unknown.
 */
enum class MissingPolicy {
    RejectSimulation,
    UseDeclaredDefault,
    InterpolateBounded,
    DegradeToLowerStage
};

/**
 * @brief Circuit racing direction enum.
 */
enum class RaceDirection {
    Clockwise,
    Anticlockwise,
    FigureEight
};

/**
 * @brief Weekend competition format enum.
 */
enum class CompetitionFormat {
    Standard,
    AlternativeFormatSprint
};

/**
 * @brief Event-specific dry tyre role label R = {HARD, MEDIUM, SOFT}.
 */
enum class TyreRole {
    Hard,
    Medium,
    Soft
};

/**
 * @brief Pirelli physical dry slick compound identifier C = {C1, C2, C3, C4, C5}.
 */
enum class PhysicalCompoundId {
    C1 = 1,
    C2 = 2,
    C3 = 3,
    C4 = 4,
    C5 = 5
};

inline const char* toString(SourceStatus status) noexcept {
    switch (status) {
        case SourceStatus::PublishedObserved:  return "PUBLISHED_OBSERVED";
        case SourceStatus::Derived:            return "DERIVED";
        case SourceStatus::Calibrated:         return "CALIBRATED";
        case SourceStatus::Estimated:          return "ESTIMATED";
        case SourceStatus::Assumed:            return "ASSUMED";
        case SourceStatus::UnknownUnavailable: return "UNKNOWN_UNAVAILABLE";
    }
    return "UNKNOWN_SOURCE_STATUS";
}

inline const char* toString(ConfidenceTier tier) noexcept {
    switch (tier) {
        case ConfidenceTier::High:        return "HIGH";
        case ConfidenceTier::Medium:      return "MEDIUM";
        case ConfidenceTier::Low:         return "LOW";
        case ConfidenceTier::Provisional: return "PROVISIONAL";
    }
    return "UNKNOWN_CONFIDENCE_TIER";
}

inline const char* toString(MissingPolicy policy) noexcept {
    switch (policy) {
        case MissingPolicy::RejectSimulation:    return "REJECT_SIMULATION";
        case MissingPolicy::UseDeclaredDefault:  return "USE_DECLARED_DEFAULT";
        case MissingPolicy::InterpolateBounded:  return "INTERPOLATE_BOUNDED";
        case MissingPolicy::DegradeToLowerStage: return "DEGRADE_TO_LOWER_STAGE";
    }
    return "UNKNOWN_MISSING_POLICY";
}

inline const char* toString(RaceDirection dir) noexcept {
    switch (dir) {
        case RaceDirection::Clockwise:     return "CLOCKWISE";
        case RaceDirection::Anticlockwise: return "ANTICLOCKWISE";
        case RaceDirection::FigureEight:   return "FIGURE_EIGHT";
    }
    return "UNKNOWN_DIRECTION";
}

inline const char* toString(CompetitionFormat fmt) noexcept {
    switch (fmt) {
        case CompetitionFormat::Standard:                return "STANDARD";
        case CompetitionFormat::AlternativeFormatSprint: return "ALTERNATIVE_FORMAT_SPRINT";
    }
    return "UNKNOWN_FORMAT";
}

inline const char* toString(TyreRole role) noexcept {
    switch (role) {
        case TyreRole::Hard:   return "HARD";
        case TyreRole::Medium: return "MEDIUM";
        case TyreRole::Soft:   return "SOFT";
    }
    return "UNKNOWN_ROLE";
}

inline const char* toString(PhysicalCompoundId id) noexcept {
    switch (id) {
        case PhysicalCompoundId::C1: return "C1";
        case PhysicalCompoundId::C2: return "C2";
        case PhysicalCompoundId::C3: return "C3";
        case PhysicalCompoundId::C4: return "C4";
        case PhysicalCompoundId::C5: return "C5";
    }
    return "UNKNOWN_COMPOUND";
}

/**
 * @brief Canonical hardness rank (1 = C1 hardest ... 5 = C5 softest) per Section 7.1.
 */
inline int canonicalHardnessRank(PhysicalCompoundId id) noexcept {
    return static_cast<int>(id);
}

/**
 * @brief Universal provenance and uncertainty envelope (Section 4.1).
 */
struct ProvenanceMetadata {
    std::optional<std::string> source_id;
    SourceStatus source_status = SourceStatus::UnknownUnavailable;
    std::string source_name;
    std::string source_ref_uri;
    std::string retrieved_at_utc;
    std::string valid_from_date;
    std::optional<std::string> valid_to_date;
    std::optional<std::string> derivation_method;
    std::optional<double> uncertainty_std;
    std::optional<double> ci_lower_95;
    std::optional<double> ci_upper_95;
    ConfidenceTier confidence_tier = ConfidenceTier::Provisional;
    MissingPolicy missing_policy = MissingPolicy::RejectSimulation;
};

/**
 * @brief Schema 1A: Versioned static circuit geometry and layout (TrackLayoutVersion).
 *
 * Nullable fields (e.g. sector lengths and speed-band fractions when telemetry
 * integration is deferred) are represented explicitly as std::optional<double>
 * and never silently converted to 0.0.
 */
struct TrackLayoutVersion {
    std::string schema_name;
    std::string schema_version;
    std::string track_id;
    int layout_version = 0;
    std::string track_name;
    std::string country;
    std::string locality;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_msl_m = 0.0;
    std::string valid_from_date;
    std::optional<std::string> valid_to_date;

    double lap_length_m = 0.0;
    std::optional<double> sector1_length_m;
    std::optional<double> sector2_length_m;
    std::optional<double> sector3_length_m;

    RaceDirection race_direction = RaceDirection::Clockwise;
    double pit_lane_length_m = 0.0;
    double pit_speed_limit_kmh = 0.0;

    int corners_total_count = 0;
    int corners_left_count = 0;
    int corners_right_count = 0;

    std::optional<double> frac_lap_straight;
    std::optional<double> frac_lap_corner_slow;
    std::optional<double> frac_lap_corner_med;
    std::optional<double> frac_lap_corner_high;

    double lateral_energy_index_norm = 1.0;
    double braking_energy_index_norm = 1.0;
    double traction_energy_index_norm = 1.0;

    std::map<std::string, ProvenanceMetadata> field_provenance;

    bool hasCompleteSectorLengths() const noexcept {
        return sector1_length_m.has_value() &&
               sector2_length_m.has_value() &&
               sector3_length_m.has_value();
    }

    bool hasCompleteSpeedBandFractions() const noexcept {
        return frac_lap_straight.has_value() &&
               frac_lap_corner_slow.has_value() &&
               frac_lap_corner_med.has_value() &&
               frac_lap_corner_high.has_value();
    }
};

/**
 * @brief Schema 1B: Versioned asphalt surface profile (TrackSurfaceProfile),
 *        kept strictly separate from TrackLayoutVersion.
 */
struct TrackSurfaceProfile {
    std::string schema_name;
    std::string schema_version;
    std::string track_id;
    int layout_version = 0;
    int surface_version = 0;
    std::string valid_from_date;
    std::optional<std::string> valid_to_date;

    double surface_abrasiveness_score = 3.0;
    double surface_grip_baseline_score = 3.0;
    std::optional<double> bumpiness_index_score;

    double track_evolution_amplitude_s = 0.0;
    double track_evolution_decay_laps = 20.0;
    std::optional<std::string> surface_description_notes;

    std::map<std::string, ProvenanceMetadata> field_provenance;
};

/**
 * @brief Key weekend session dates stored inside EventConfiguration.
 */
struct EventDates {
    std::optional<std::string> fp1_and_sprint_qualifying_date;
    std::optional<std::string> sprint_and_qualifying_date;
    std::optional<std::string> fp1_and_fp2_date;
    std::optional<std::string> fp3_and_qualifying_date;
    std::string race_date;
};

/**
 * @brief Governing FIA regulation document citations stored inside EventConfiguration.
 */
struct GoverningRegulations {
    std::string sporting_regulations_doc;
    std::string technical_regulations_doc;
};

/**
 * @brief Schema 2: Event configuration and regulatory constraints (EventConfiguration).
 */
struct EventConfiguration {
    std::string schema_name;
    std::string schema_version;
    std::string event_id;
    std::string official_event_name;
    int round_number = 0;
    EventDates event_dates;
    CompetitionFormat competition_format = CompetitionFormat::Standard;
    int season_year = 0;

    std::string track_id;
    int layout_version = 0;
    int surface_version = 0;

    int race_distance_laps = 0;
    double scheduled_race_distance_km = 0.0;

    std::array<PhysicalCompoundId, 3> nominated_compounds_set{};
    bool is_hypothetical_override = false;

    int min_distinct_dry_compounds = 2;
    int min_pit_stops = 1;
    int max_pit_stops = 4;
    int min_stint_length_laps = 1;
    std::optional<int> max_stint_length_laps;

    int available_sets_hard = 2;
    int available_sets_med = 3;
    int available_sets_soft = 8;

    std::optional<std::string> typical_pre_race_sunday_remaining_sets_note;
    GoverningRegulations governing_regulations;
    std::map<std::string, ProvenanceMetadata> field_provenance;
};

/**
 * @brief Single role-to-compound mapping entry in EventRoleMapping.
 */
struct RoleMappingEntry {
    std::string event_id;
    TyreRole role_id = TyreRole::Hard;
    PhysicalCompoundId compound_id = PhysicalCompoundId::C1;
    std::string sidewall_colour;
    int hardness_rank = 1;
};

/**
 * @brief Schema 5: Bijective mapping between event tyre roles {HARD, MEDIUM, SOFT}
 *        and the three nominated physical compounds N_e (EventRoleMapping).
 */
struct EventRoleMapping {
    std::string schema_name;
    std::string schema_version;
    std::string event_id;
    int season_year = 0;
    int spec_version = 1;
    bool is_hypothetical_override = false;
    std::array<PhysicalCompoundId, 3> nominated_compounds_set{};
    std::vector<RoleMappingEntry> mappings;
    ProvenanceMetadata provenance;

    std::optional<PhysicalCompoundId> compoundForRole(TyreRole role) const noexcept {
        for (const auto& entry : mappings) {
            if (entry.role_id == role) {
                return entry.compound_id;
            }
        }
        return std::nullopt;
    }

    const RoleMappingEntry* entryForRole(TyreRole role) const noexcept {
        for (const auto& entry : mappings) {
            if (entry.role_id == role) {
                return &entry;
            }
        }
        return nullptr;
    }
};

/**
 * @brief Single physical dry slick compound specification (C1..C5) inside
 *        PhysicalCompoundCatalogue.
 *
 * Calibrated model coefficients (pace_offset_ref_s, base_wear_rate_per_lap,
 * linear_deg_coeff_s, opt_temp_window_min_c, opt_temp_window_max_c) are
 * std::optional<double> so uncalibrated baseline catalogues load cleanly
 * while preserving exact unknown states.
 */
struct PhysicalCompoundSpec {
    int season_year = 0;
    int spec_version = 1;
    PhysicalCompoundId compound_id = PhysicalCompoundId::C1;
    int hardness_rank = 1;
    std::string description;

    std::optional<double> front_tyre_width_reduction_vs_2025_mm;
    std::optional<double> rear_tyre_width_reduction_vs_2025_mm;
    std::optional<double> front_outer_diameter_reduction_vs_2025_mm;
    std::optional<double> rear_outer_diameter_reduction_vs_2025_mm;
    std::optional<int> wheel_rim_diameter_in;

    std::optional<double> pace_offset_ref_s;
    std::optional<double> base_wear_rate_per_lap;
    std::optional<double> linear_deg_coeff_s;
    std::optional<double> cliff_wear_threshold;
    std::optional<double> cliff_amplitude_s;
    std::optional<double> cliff_exponent;
    std::optional<double> thermal_deg_sens_per_c;
    std::optional<double> opt_temp_window_min_c;
    std::optional<double> opt_temp_window_max_c;

    std::map<std::string, ProvenanceMetadata> field_provenance;

    /**
     * @brief Returns true if the three mandatory Stage M0-M2 linear pace/wear
     *        coefficients are present (non-null).
     */
    bool hasBaselineLinearCalibration() const noexcept {
        return pace_offset_ref_s.has_value() &&
               base_wear_rate_per_lap.has_value() &&
               linear_deg_coeff_s.has_value();
    }

    /**
     * @brief Returns true if the Stage M4 optimal temperature window bounds are present.
     */
    bool hasThermalWindowCalibration() const noexcept {
        return opt_temp_window_min_c.has_value() &&
               opt_temp_window_max_c.has_value();
    }
};

/**
 * @brief Schema 4: Season-versioned catalogue of physical dry slick compounds C1..C5.
 */
struct PhysicalCompoundCatalogue {
    std::string schema_name;
    std::string schema_version;
    int season_year = 0;
    int spec_version = 1;
    std::string homologation_date;
    std::string calibration_status;
    std::vector<PhysicalCompoundSpec> compounds;

    const PhysicalCompoundSpec* findCompound(PhysicalCompoundId id) const noexcept {
        for (const auto& comp : compounds) {
            if (comp.compound_id == id) {
                return &comp;
            }
        }
        return nullptr;
    }
};

/**
 * @brief Complete, cross-validated, read-only snapshot of all datasets required
 *        for an event simulation setup.
 *
 * Loaded once via DataAccessLayer::loadEventBundle(event_id) and reused across
 * repeated strategy evaluations without reopening or reparsing JSON files.
 */
struct EventBundle {
    EventConfiguration event_config;
    EventRoleMapping role_mapping;
    TrackLayoutVersion track_layout;
    TrackSurfaceProfile track_surface;
    PhysicalCompoundCatalogue compound_catalogue;

    /**
     * @brief Resolve an event tyre role {HARD, MEDIUM, SOFT} directly to its
     *        physical compound specification in the catalogue.
     */
    const PhysicalCompoundSpec* compoundForRole(TyreRole role) const noexcept {
        auto comp_id = role_mapping.compoundForRole(role);
        if (!comp_id.has_value()) {
            return nullptr;
        }
        return compound_catalogue.findCompound(*comp_id);
    }

    /**
     * @brief Check whether all three nominated compounds for this event have
     *        calibrated values for pace_offset_ref_s, base_wear_rate_per_lap,
     *        and linear_deg_coeff_s.
     */
    bool areNominatedCompoundsCalibratedForBaselineM0() const noexcept {
        for (PhysicalCompoundId comp_id : event_config.nominated_compounds_set) {
            const PhysicalCompoundSpec* spec = compound_catalogue.findCompound(comp_id);
            if (spec == nullptr || !spec->hasBaselineLinearCalibration()) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief List all null/uncalibrated baseline parameters across the event's
     *        three nominated physical compounds.
     */
    std::vector<std::string> uncalibratedNominatedCompoundFields() const {
        std::vector<std::string> missing;
        for (PhysicalCompoundId comp_id : event_config.nominated_compounds_set) {
            const PhysicalCompoundSpec* spec = compound_catalogue.findCompound(comp_id);
            const std::string prefix = std::string(toString(comp_id)) + ".";
            if (spec == nullptr) {
                missing.push_back(prefix + "<missing_spec>");
                continue;
            }
            if (!spec->pace_offset_ref_s.has_value()) {
                missing.push_back(prefix + "pace_offset_ref_s");
            }
            if (!spec->base_wear_rate_per_lap.has_value()) {
                missing.push_back(prefix + "base_wear_rate_per_lap");
            }
            if (!spec->linear_deg_coeff_s.has_value()) {
                missing.push_back(prefix + "linear_deg_coeff_s");
            }
        }
        return missing;
    }
};

} // namespace data_access
} // namespace f1_sim

#endif // F1_SIM_DATA_ACCESS_DOMAIN_MODELS_HPP