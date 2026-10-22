#ifndef F1_SIM_SIMULATION_TYRE_INVENTORY_HPP
#define F1_SIM_SIMULATION_TYRE_INVENTORY_HPP

#include "f1_sim/simulation/engine_types.hpp"
#include "f1_sim/simulation/lap_evaluator.hpp"

namespace f1_sim {
namespace simulation {

/**
 * @brief Manages the fixed 7-set physical tyre inventory (3 Soft, 2 Medium, 2 Hard)
 *        for the selected track/event session (Design Spec Section 7).
 *
 * Core Invariants:
 *  - Fixed allocation: S1, S2, S3 (Soft), M1, M2 (Medium), H1, H2 (Hard).
 *  - Users can configure prior age (`configured_age_laps >= 0`) ONLY when no simulation
 *    is running or paused.
 *  - Users can NEVER directly override tyre health; `derived_starting_health` is always
 *    computed dynamically from `configured_age_laps` and the active compound/track/model
 *    parameters via `LapStintEvaluator::deriveStartingHealthFromAge`.
 *  - Changing the selected event/track resets all configured ages to 0 laps (100% health).
 *  - Ending, failing, or cancelling a simulation resets temporary runtime wear/consumption
 *    back to the configured pre-race condition while preserving `configured_age_laps`.
 *  - Deterministic set selection: highest derived starting health, then lowest configured age,
 *    then lexicographically smallest stable set_id. Never assigns the same physical set to
 *    two stints in one race.
 */
class TyreInventoryManager {
public:
    static constexpr std::size_t kSoftSetCount = 3;
    static constexpr std::size_t kMediumSetCount = 2;
    static constexpr std::size_t kHardSetCount = 2;
    static constexpr std::size_t kTotalSetCount = 7;
    static constexpr int kMaxAllowedConfiguredAgeLaps = 100;

    TyreInventoryManager();

    /**
     * @brief Reset the entire 7-set inventory to default pre-race configuration
     *        (all configured ages = 0 laps, derived starting health = 100%) for a newly
     *        selected event/track.
     */
    void resetForNewEvent(
        const EventBundle* bundle,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage);

    /**
     * @brief Recompute derived starting health for all 7 sets from their current
     *        `configured_age_laps` when model parameters or vehicle/stage settings change.
     *        Also resets runtime state to match the updated starting health.
     */
    void recomputeDerivedStartingHealth(
        const EventBundle* bundle,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage);

    /**
     * @brief Configure the pre-race age (in completed prior laps) of a specific set ID.
     *        Rejected if `session_active == true` or if `age_laps` is negative/out-of-range.
     */
    EngineResult<void> setConfiguredAge(
        const std::string& set_id,
        int age_laps,
        bool session_active,
        const EventBundle* bundle,
        const VehicleConfig& vehicle,
        const ModelParameters& params,
        ModelStage stage);

    /**
     * @brief Reset only the temporary runtime tyre wear, runtime age, fitted flags, and
     *        consumption flags back to the configured pre-race state. Preserves all
     *        `configured_age_laps` for repeatable A/B strategy comparisons.
     */
    void resetRuntimeStateToConfigured() noexcept;

    /**
     * @briefDeterministically allocate distinct physical set IDs for a sequence of stints.
     *
     * If `stints[j].requested_set_id` is provided, validates that the set exists, matches
     * `stints[j].role`, and has not already been allocated to an earlier stint.
     * Otherwise selects the best available set of `stints[j].role` ordered by:
     *   1. Highest `derived_starting_health->meanHealth()`
     *   2. Lowest `configured_age_laps`
     *   3. Lexicographically smallest `set_id` ("S1" < "S2" < "S3")
     *
     * Note: Can also be called mid-race in Interactive mode by passing `already_used_set_ids`
     * for completed/active stints so that remaining stints never reuse a consumed set.
     */
    EngineResult<std::vector<std::string>> allocateSetsForStints(
        const std::vector<StintSpec>& stints,
        const std::vector<std::string>& already_used_set_ids = {},
        int stint_index_offset = 0) const;

    /**
     * @brief Fit a specific set ID at the start of a stint during a race run.
     */
    EngineResult<void> fitSetForStint(const std::string& set_id, int stint_index);

    /**
     * @brief Update the runtime health and increment runtime age of the currently fitted set
     *        after a completed lap.
     */
    EngineResult<void> updateActiveSetAfterLap(
        const std::string& set_id,
        const CornerHealthVector& new_health);

    const std::vector<TyreSetState>& sets() const noexcept { return sets_; }
    std::vector<TyreSetState>& mutableSets() noexcept { return sets_; }

    const TyreSetState* findSet(const std::string& set_id) const noexcept;
    TyreSetState* findMutableSet(const std::string& set_id) noexcept;

    void restoreSetsFromSnapshot(const std::vector<TyreSetState>& snapshot_sets);

private:
    void initializeDefaultSets();

    std::vector<TyreSetState> sets_;
};

} // namespace simulation
} // namespace f1_sim

#endif // F1_SIM_SIMULATION_TYRE_INVENTORY_HPP
