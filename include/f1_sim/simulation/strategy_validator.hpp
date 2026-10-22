#ifndef F1_SIM_SIMULATION_STRATEGY_VALIDATOR_HPP
#define F1_SIM_SIMULATION_STRATEGY_VALIDATOR_HPP

#include "f1_sim/data_access/data_access_layer.hpp"
#include "f1_sim/simulation/engine_types.hpp"
#include "f1_sim/simulation/tyre_inventory.hpp"

namespace f1_sim {
namespace simulation {

/**
 * @brief Validates strategy plans, interactive mid-race plan revisions, and
 *        capability/calibration readiness against the loaded DAL EventBundle and
 *        TyreInventoryManager (Design Spec Sections 4, 6, 8 & Architecture Spec Section 6).
 */
class StrategyAndReadinessValidator {
public:
    /**
     * @brief Produce a complete CapabilityReadinessReport separating:
     *  - Schema/load validity
     *  - User input validity (vehicle, environment, model parameters, inventory ages)
     *  - Mathematical capability (availability of required coefficients or authorised fallbacks)
     *  - Calibration status (Calibrated vs Estimated/Assumed vs UncalibratedIncomplete)
     *  - Execution permission
     *
     * Also reports any missing required capabilities or domain discrepancies to the DAL's
     * diagnostic feedback path (`DataAccessLayer::reportEngineDiscrepancy`) when `dal` is provided.
     */
    static CapabilityReadinessReport assessReadiness(
        const EventBundle* bundle,
        LoadStatus bundle_load_status,
        const std::vector<Diagnostic>& bundle_load_diagnostics,
        const VehicleConfig& vehicle,
        const EnvironmentScenarioConfig& env_config,
        const ModelParameters& params,
        const TyreInventoryManager& inventory,
        ModelStage stage,
        const data_access::DataAccessLayer* dal = nullptr);

    /**
     * @brief Validate a complete initial pre-race StrategyPlan against the EventBundle,
     *        regulatory rules, and 7-set TyreInventoryManager.
     *
     * On success, returns the deterministically allocated physical set IDs for each stint.
     */
    static EngineResult<std::vector<std::string>> validateInitialStrategy(
        const StrategyPlan& strategy,
        const EventBundle& bundle,
        const TyreInventoryManager& inventory);

    /**
     * @brief Validate a proposed InteractiveDecision at a decision boundary (`completed_laps < L`).
     *
     * Enforces:
     *  - A mandatory health-triggered stop cannot be overridden by `ExtendCurrentStint`.
     *  - In `ExtendCurrentStint`, `extension_laps` must be a positive integer (> 0), and the
     *    engine does NOT automatically subtract `extension_laps` from the next stint; the
     *    remaining stints must be explicitly adjusted by the user so that
     *    `extension_laps + sum(remaining_stints.target_laps) == L - completed_laps`.
     *  - In `KeepOriginalPlan` or `PitNowWithRevisedPlan`, `sum(remaining_stints.target_laps)`
     *    must equal `L - completed_laps`.
     *  - Full race compound diversity (`min_distinct_dry_compounds`), stint bounds, and
     *    distinct physical tyre set availability across already-used sets and remaining stints.
     */
    static EngineResult<std::vector<std::string>> validateInteractiveDecision(
        const InteractiveDecision& decision,
        const PendingDecisionPrompt& prompt,
        const EventBundle& bundle,
        const TyreInventoryManager& inventory,
        const std::vector<StintRuntimeRecord>& current_stint_records,
        int current_stint_index,
        int completed_laps);
};

} // namespace simulation
} // namespace f1_sim

#endif // F1_SIM_SIMULATION_STRATEGY_VALIDATOR_HPP
