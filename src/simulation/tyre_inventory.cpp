#include "f1_sim/simulation/tyre_inventory.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace f1_sim {
namespace simulation {

TyreInventoryManager::TyreInventoryManager() {
    initializeDefaultSets();
}

void TyreInventoryManager::initializeDefaultSets() {
    sets_.clear();
    sets_.reserve(kTotalSetCount);

    // Fixed 7-set inventory: 3 Soft (S1..S3), 2 Medium (M1..M2), 2 Hard (H1..H2)
    const std::pair<const char*, TyreRole> layout[kTotalSetCount] = {
        {"S1", TyreRole::Soft},
        {"S2", TyreRole::Soft},
        {"S3", TyreRole::Soft},
        {"M1", TyreRole::Medium},
        {"M2", TyreRole::Medium},
        {"H1", TyreRole::Hard},
        {"H2", TyreRole::Hard}
    };

    for (const auto& item : layout) {
        TyreSetState s;
        s.set_id = item.first;
        s.role = item.second;
        s.mapped_compound_id = std::nullopt;
        s.configured_age_laps = 0;
        s.derived_starting_health = CornerHealthVector::uniform(1.0);
        s.health_status = TyreSetHealthStatus::ReadyNewSet;
        s.runtime_age_laps = 0;
        s.runtime_health = CornerHealthVector::uniform(1.0);
        s.currently_fitted = false;
        s.consumed_in_current_run = false;
        s.assigned_stint_index = std::nullopt;
        sets_.push_back(std::move(s));
    }
}

void TyreInventoryManager::resetForNewEvent(
    const EventBundle* bundle,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) {
    initializeDefaultSets();
    recomputeDerivedStartingHealth(bundle, vehicle, params, stage);
}

void TyreInventoryManager::recomputeDerivedStartingHealth(
    const EventBundle* bundle,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) {
    for (auto& s : sets_) {
        if (bundle != nullptr) {
            s.mapped_compound_id = bundle->role_mapping.compoundForRole(s.role);
        } else {
            s.mapped_compound_id = std::nullopt;
        }

        if (s.configured_age_laps == 0) {
            s.derived_starting_health = CornerHealthVector::uniform(1.0);
            s.health_status = TyreSetHealthStatus::ReadyNewSet;
        } else if (bundle != nullptr && s.mapped_compound_id.has_value()) {
            const ResolvedCompoundParams comp =
                LapStintEvaluator::resolveCompoundParameters(*bundle, *s.mapped_compound_id, params);
            auto derived = LapStintEvaluator::deriveStartingHealthFromAge(
                s.configured_age_laps, *bundle, comp, vehicle, params, stage);
            if (!derived.has_value()) {
                s.derived_starting_health = std::nullopt;
                s.health_status = TyreSetHealthStatus::MissingCalibrationForAgeDerivation;
            } else {
                s.derived_starting_health = *derived;
                const auto check = LapStintEvaluator::evaluateMandatoryStopThresholds(*derived);
                if (check.mandatory_stop_required ||
                    derived->minCornerHealth() < params.structural_health_floor_h_min) {
                    s.health_status = TyreSetHealthStatus::DepletedBelowMandatoryThreshold;
                } else {
                    s.health_status = TyreSetHealthStatus::ReadyDerivedFromAge;
                }
            }
        } else {
            s.derived_starting_health = std::nullopt;
            s.health_status = TyreSetHealthStatus::MissingCalibrationForAgeDerivation;
        }
    }

    resetRuntimeStateToConfigured();
}

EngineResult<void> TyreInventoryManager::setConfiguredAge(
    const std::string& set_id,
    int age_laps,
    bool session_active,
    const EventBundle* bundle,
    const VehicleConfig& vehicle,
    const ModelParameters& params,
    ModelStage stage) {
    if (session_active) {
        return EngineResult<void>::failure(
            EngineStatus::ConfigurationLockedWhileRunning,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Configured tyre ages cannot be modified while a simulation is running or paused.",
                bundle ? bundle->event_config.event_id : "session",
                "",
                "/inventory/" + set_id + "/configured_age_laps",
                "SessionState not in {RUNNING, PAUSED, DECISION_REQUIRED}",
                "active simulation"
            }});
    }

    TyreSetState* target = findMutableSet(set_id);
    if (target == nullptr) {
        return EngineResult<void>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Unknown tyre set ID '" + set_id + "'; expected one of S1, S2, S3, M1, M2, H1, H2.",
                bundle ? bundle->event_config.event_id : "session",
                "",
                "/inventory/" + set_id,
                "S1|S2|S3|M1|M2|H1|H2",
                set_id
            }});
    }

    if (age_laps < 0 || age_laps > kMaxAllowedConfiguredAgeLaps) {
        return EngineResult<void>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Configured tyre age must be a non-negative integer in [0, 100] laps.",
                bundle ? bundle->event_config.event_id : "session",
                "",
                "/inventory/" + set_id + "/configured_age_laps",
                "[0, 100]",
                std::to_string(age_laps)
            }});
    }

    target->configured_age_laps = age_laps;
    recomputeDerivedStartingHealth(bundle, vehicle, params, stage);

    if (target->health_status == TyreSetHealthStatus::MissingCalibrationForAgeDerivation) {
        return EngineResult<void>::success(
            EngineStatus::Ok,
            {Diagnostic{
                Severity::Warning,
                DiagnosticCode::InsufficientDataForCapability,
                "Configured age > 0 on set '" + set_id + "', but base_wear_rate_per_lap is uncalibrated; starting health cannot be derived until compound wear parameters are provided.",
                bundle ? bundle->event_config.event_id : "session",
                "",
                "/inventory/" + set_id + "/derived_starting_health",
                "calibrated base_wear_rate_per_lap",
                "null"
            }});
    }

    return EngineResult<void>::success();
}

void TyreInventoryManager::resetRuntimeStateToConfigured() noexcept {
    for (auto& s : sets_) {
        s.runtime_age_laps = s.configured_age_laps;
        s.runtime_health = s.derived_starting_health.value_or(CornerHealthVector::uniform(0.0));
        s.currently_fitted = false;
        s.consumed_in_current_run = false;
        s.assigned_stint_index = std::nullopt;
    }
}

EngineResult<std::vector<std::string>> TyreInventoryManager::allocateSetsForStints(
    const std::vector<StintSpec>& stints,
    const std::vector<std::string>& already_used_set_ids,
    int stint_index_offset) const {
    std::set<std::string> used(already_used_set_ids.begin(), already_used_set_ids.end());
    std::vector<std::string> allocated(stints.size());
    std::vector<Diagnostic> errors;

    // Pass 1: Reserve explicitly requested set IDs first so automatic selection for earlier
    // stints never steals a set that a later stint explicitly requested.
    for (std::size_t i = 0; i < stints.size(); ++i) {
        const StintSpec& st = stints[i];
        if (!st.requested_set_id.has_value() || st.requested_set_id->empty()) {
            continue;
        }
        const std::string& req_id = *st.requested_set_id;
        const std::string field_ptr =
            "/strategy/stints/" + std::to_string(stint_index_offset + static_cast<int>(i));
        const TyreSetState* found = findSet(req_id);
        if (found == nullptr) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Requested physical tyre set '" + req_id + "' does not exist in the 7-set inventory.",
                "strategy",
                "",
                field_ptr + "/requested_set_id",
                "S1|S2|S3|M1|M2|H1|H2",
                req_id
            });
            continue;
        }
        if (found->role != st.role) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Requested tyre set '" + req_id + "' has role " + data_access::toString(found->role) +
                    ", which does not match stint role " + data_access::toString(st.role) + ".",
                "strategy",
                "",
                field_ptr + "/requested_set_id",
                data_access::toString(st.role),
                data_access::toString(found->role)
            });
            continue;
        }
        if (!used.insert(req_id).second) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Physical tyre set '" + req_id + "' is assigned to multiple stints in the same race.",
                "strategy",
                "",
                field_ptr + "/requested_set_id",
                "Unique physical set per stint",
                req_id
            });
            continue;
        }
        allocated[i] = req_id;
    }

    // Pass 2: Deterministically allocate sets for stints without explicit set IDs.
    for (std::size_t i = 0; i < stints.size(); ++i) {
        if (!allocated[i].empty()) {
            continue;
        }
        const StintSpec& st = stints[i];
        const std::string field_ptr =
            "/strategy/stints/" + std::to_string(stint_index_offset + static_cast<int>(i));

        std::vector<const TyreSetState*> candidates;
        for (const auto& s : sets_) {
            if (s.role == st.role && used.find(s.set_id) == used.end()) {
                candidates.push_back(&s);
            }
        }

        if (candidates.empty()) {
            errors.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                std::string("Insufficient physical tyre sets in inventory for role ") +
                    data_access::toString(st.role) +
                    " (fixed allocation has 3 SOFT, 2 MEDIUM, 2 HARD sets).",
                "strategy",
                "",
                field_ptr + "/role",
                "Available unused set of role " + std::string(data_access::toString(st.role)),
                "0 remaining sets"
            });
            continue;
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const TyreSetState* a, const TyreSetState* b) {
                      const double ha = a->derived_starting_health.has_value()
                          ? a->derived_starting_health->meanHealth()
                          : -1.0;
                      const double hb = b->derived_starting_health.has_value()
                          ? b->derived_starting_health->meanHealth()
                          : -1.0;
                      if (std::fabs(ha - hb) > 1e-12) {
                          return ha > hb;
                      }
                      if (a->configured_age_laps != b->configured_age_laps) {
                          return a->configured_age_laps < b->configured_age_laps;
                      }
                      return a->set_id < b->set_id;
                  });

        const TyreSetState* chosen = candidates.front();
        used.insert(chosen->set_id);
        allocated[i] = chosen->set_id;
    }

    // Pass 3: Check that every allocated set has a valid, non-depleted starting health
    for (std::size_t i = 0; i < allocated.size(); ++i) {
        if (allocated[i].empty()) {
            continue;
        }
        const TyreSetState* s = findSet(allocated[i]);
        const std::string field_ptr =
            "/strategy/stints/" + std::to_string(stint_index_offset + static_cast<int>(i));
        if (s != nullptr) {
            if (s->health_status == TyreSetHealthStatus::MissingCalibrationForAgeDerivation ||
                !s->derived_starting_health.has_value()) {
                errors.push_back(Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InsufficientDataForCapability,
                    "Allocated tyre set '" + s->set_id + "' has configured_age_laps = " +
                        std::to_string(s->configured_age_laps) +
                        ", but starting health cannot be derived because compound wear rate is uncalibrated.",
                    "strategy",
                    "",
                    field_ptr + "/assigned_set_id",
                    "Valid derived starting health",
                    "nullopt (missing base_wear_rate_per_lap)"
                });
            } else if (s->health_status == TyreSetHealthStatus::DepletedBelowMandatoryThreshold) {
                errors.push_back(Diagnostic{
                    Severity::Error,
                    DiagnosticCode::InvalidRangeOrEnum,
                    "Allocated tyre set '" + s->set_id + "' (configured age " +
                        std::to_string(s->configured_age_laps) +
                        " laps) has derived starting health below the mandatory stop threshold.",
                    "strategy",
                    "",
                    field_ptr + "/assigned_set_id",
                    "mean_health >= 30% and min_corner_health >= 12%",
                    std::to_string(s->derived_starting_health->meanHealthPct()) + "%"
                });
            }
        }
    }

    if (!errors.empty()) {
        return EngineResult<std::vector<std::string>>::failure(
            EngineStatus::ValidationFailed, std::move(errors));
    }
    return EngineResult<std::vector<std::string>>::success(std::move(allocated));
}

EngineResult<void> TyreInventoryManager::fitSetForStint(const std::string& set_id, int stint_index) {
    TyreSetState* target = findMutableSet(set_id);
    if (target == nullptr) {
        return EngineResult<void>::failure(
            EngineStatus::ValidationFailed,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRequestIdentifier,
                "Cannot fit unknown tyre set '" + set_id + "'.",
                "inventory",
                "",
                "/inventory/" + set_id,
                "Existing set ID",
                set_id
            }});
    }

    if (target->consumed_in_current_run) {
        return EngineResult<void>::failure(
            EngineStatus::RuntimeConstraintFailure,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InvalidRangeOrEnum,
                "Physical tyre set '" + set_id + "' was already used in an earlier stint of this race.",
                "inventory",
                "",
                "/inventory/" + set_id,
                "Unused physical set",
                "already consumed"
            }});
    }

    if (!target->derived_starting_health.has_value()) {
        return EngineResult<void>::failure(
            EngineStatus::InsufficientCapabilityOrCalibration,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Cannot fit tyre set '" + set_id + "' because its starting health could not be derived.",
                "inventory",
                "",
                "/inventory/" + set_id + "/derived_starting_health",
                "Valid derived starting health",
                "nullopt"
            }});
    }

    for (auto& s : sets_) {
        s.currently_fitted = false;
    }

    target->currently_fitted = true;
    target->consumed_in_current_run = true;
    target->assigned_stint_index = stint_index;
    target->runtime_age_laps = target->configured_age_laps;
    target->runtime_health = *target->derived_starting_health;
    return EngineResult<void>::success();
}

EngineResult<void> TyreInventoryManager::updateActiveSetAfterLap(
    const std::string& set_id,
    const CornerHealthVector& new_health) {
    TyreSetState* target = findMutableSet(set_id);
    if (target == nullptr || !target->currently_fitted) {
        return EngineResult<void>::failure(
            EngineStatus::RuntimeConstraintFailure,
            {Diagnostic{
                Severity::Error,
                DiagnosticCode::UnexpectedInternalFailure,
                "Active tyre set '" + set_id + "' is not currently fitted.",
                "inventory",
                "",
                "/inventory/" + set_id,
                "currently_fitted == true",
                "false"
            }});
    }

    target->runtime_age_laps += 1;
    target->runtime_health = new_health;
    return EngineResult<void>::success();
}

const TyreSetState* TyreInventoryManager::findSet(const std::string& set_id) const noexcept {
    for (const auto& s : sets_) {
        if (s.set_id == set_id) {
            return &s;
        }
    }
    return nullptr;
}

TyreSetState* TyreInventoryManager::findMutableSet(const std::string& set_id) noexcept {
    for (auto& s : sets_) {
        if (s.set_id == set_id) {
            return &s;
        }
    }
    return nullptr;
}

void TyreInventoryManager::restoreSetsFromSnapshot(const std::vector<TyreSetState>& snapshot_sets) {
    if (snapshot_sets.size() == kTotalSetCount) {
        sets_ = snapshot_sets;
    }
}

} // namespace simulation
} // namespace f1_sim
