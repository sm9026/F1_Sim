# Formula 1 Strategy Simulator — Mathematical Framework and Data Architecture Specification

## 1. Task Interpretation, Objectives, and System Boundaries

### 1.1 Executive Model Definition and Prediction Target

The mathematical framework defined in this specification governs a deterministic, data-driven Formula 1 race-strategy simulator engineered for implementation in C++. Rather than attempting an uncalibrated full-vehicle multibody physics simulation or relying on proprietary tyre thermomechanical finite-element models, the framework establishes a modular, empirically identifiable state-space formulation. Its primary objective is to predict total elapsed single-car race time $T_{\mathrm{race}}(\pi)$ (in seconds) across a finite integer race distance of $L$ laps for any feasible tyre-stint strategy $\pi$, and to identify the strategy $\pi^*$ that minimises total race time under explicitly declared regulatory, physical, and environmental assumptions.

In this formulation, the prediction target $T_{\mathrm{race}}(\pi)$ represents the cumulative elapsed time from the race start signal ($k = 1$ start line) to the completion of lap $L$ ($k = L$ finish line) under undisturbed green-flag racing conditions, decomposed into the sum of individual flying-lap times $T_k$ and discrete incremental pit-stop time losses $P_j$. A strategy $\pi$ is formally defined as an ordered sequence of $J$ stints, $\pi = ((r_1, n_1, \sigma_1), (r_2, n_2, \sigma_2), \ldots, (r_J, n_J, \sigma_J))$, where $r_j \in \{S, M, H\}$ denotes the event-level tyre role (Soft, Medium, or Hard), $n_j \in \mathbb{Z}^+$ denotes the integer number of laps completed in stint $j$ such that $\sum_{j=1}^J n_j = L$, and $\sigma_j$ identifies the specific physical tyre set from the driver's event allocation (capturing initial tyre state $\mathbf{z}_{j,0}$ when used sets are fitted).

### 1.2 Separation of Architectural Concerns

A foundational requirement of the architecture is the strict separation of five distinct domains that are frequently conflated in ad-hoc racing simulations:

1. **Static Circuit and Surface Database:** Versioned descriptions of physical track geometry, sector lengths, cornering/braking exposure proxies, and qualitative or normalised surface roughness/grip indicators tied to a specific layout and resurfacing epoch (`track_id`, `layout_version`, `surface_version`). Neither fixed lap times nor universal degradation rates are stored as intrinsic properties of a circuit.
2. **Dynamic Simulation Scenario and Environment:** Per-simulation event rules (lap count $L$, nominated compound triplet, mandatory compound rules, pit-lane speed limits) and environmental inputs (`track_temperature_c`, `air_temperature_c`, `relative_humidity_pct`, `surface_wetness`, `wind_speed_ms`, `solar_radiation_wm2`), represented either as constant race-day baselines or lap-indexed time series $\mathbf{w}_k$.
3. **Physical Compound Catalogue and Event Role Mapping:** A season-versioned catalogue of five dry slick physical compounds $\mathcal{C} = \{C1, C2, C3, C4, C5\}$ published by [Pirelli Motorsport](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1), paired with an event-specific injective mapping $m_e : \{S, M, H\} \hookrightarrow \mathcal{C}$ whose image equals the three-compound subset $\mathcal{N}_e \subset \mathcal{C}$ nominated for event $e$.
4. **Vehicle and Setup Specification:** Car-level mass, fuel burn rate, power/aerodynamic efficiency proxies, and reference baseline pace anchors separated from both the circuit geometry table and the tyre compound table.
5. **Calibrated Mathematical Model Parameters:** Versioned empirical coefficients (such as compound wear rates, nonlinear cliff exponents, thermal sensitivity parameters, and fuel-mass lap-time sensitivities) estimated by fitting the mathematical equations to historical session data from sources such as the [FastF1 Timing and Telemetry API](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) and the [OpenF1 API](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F), stored with explicit provenance, confidence intervals, and validity bounds.

### 1.3 System Boundaries: Baseline Inclusions Versus Deferred Extensions

To guarantee that the initial C++ implementation is testable, identifiable from publicly accessible timing data, and free from unverified parameter proliferation, the system boundary distinguishes between the required baseline (Stages M0–M2) and staged extensions (Stages M3–M5).

| Modelling Domain | Included in Baseline (Stages M0–M2) | Deferred to Staged Extensions (Stages M3–M5) | Architectural Provision in Core Schema |
| :--- | :--- | :--- | :--- |
| **Weather & Surface State** | Dry-weather slick racing; constant or lap-indexed track temperature $T_{\mathrm{trk},k}$, air temperature $T_{\mathrm{air},k}$, and relative humidity $\phi_k$. | Wet-track water film thickness dynamics, intermediate/wet compound crossovers, and stochastic rain arrival. | `ScenarioEnvironmentSeries` stores `precipitation_rate_mmph`, `surface_wetness`, and `track_state` enums per lap. |
| **Tyre Condition State** | Normalized per-set aggregate health state $h_k \in [0, 1]$ (or equivalent wear $w_k = 1 - h_k$) separated from lap-time penalty $\Delta T_{\mathrm{tyre},k}$. | Independent four-corner states $\mathbf{h}_k \in [0, 1]^4$ (`FL`, `FR`, `RL`, `RR`) driven by asymmetric circuit loading and nonlinear graining/blistering cliffs. | State vector $\mathbf{z}_k$ is defined over corner index $i \in \{\mathrm{FL}, \mathrm{FR}, \mathrm{RL}, \mathrm{RR}\}$; M0–M2 binds all four corners to a shared scalar. |
| **Tyre Thermal Dynamics** | Empirical environmental degradation multiplier $F_{\mathrm{env}}$ driven by track and air temperature relative to a reference state. | Dynamic carcass and surface temperature recurrence $\boldsymbol{\theta}_{k+1} = f_{\mathrm{heat}}(\boldsymbol{\theta}_k, \ldots)$ with convective/conductive heat transfer. | `CompoundCatalogue` and `ModelParameterSet` include thermal window and heat-transfer fields (`opt_temp_min_c`, `opt_temp_max_c`). |
| **Race Operations & Traffic** | Single-car undisturbed free-air pace; deterministic fuel burn; stationary plus pit-lane traversal incremental time loss $P_j$; standing-start lap-1 offset. | Multi-car traffic queuing, DRS overtaking probability, Virtual Safety Car (VSC) / Safety Car (SC) pit-loss discounts, and yellow flags. | Lap-time equation includes $\Delta T_{\mathrm{other},k}$ and pit-loss model accepts lap-indexed regime modifiers. |
| **Strategy Optimisation** | Exact deterministic shortest-path search on a Directed Acyclic Graph (DAG) and exhaustive enumeration verification over $\{S, M, H\}$ stints. | Stochastic dynamic programming / Monte Carlo tree search over weather ensembles and safety-car arrival processes. | Optimiser interface decouples strategy graph generation from the stint-cost evaluator. |

---

## 2. Terminology and Master Notation Table

Consistent mathematical notation is enforced across all derivations, schema mappings, and algorithmic specifications. Scalar quantities are written in italic Roman or Greek letters ($T_k, h_k, \alpha_c$), vectors in boldface lowercase ($\mathbf{x}_r, \mathbf{w}_k, \mathbf{z}_k$), and sets or spaces in calligraphic uppercase ($\mathcal{C}, \mathcal{R}, \Pi_{\mathrm{feasible}}$). Every variable carries a single meaning and explicit physical dimension or dimensionless normalisation.

| Symbol | Domain / Space | Unit | Mathematical Definition and Role |
| :--- | :--- | :--- | :--- |
| $L$ | $\mathbb{Z}^+$ | $\mathrm{laps}$ | Configured total integer number of laps in the race event. |
| $k$ | $\{1, 2, \ldots, L\}$ | $\mathrm{lap}$ | Discrete race lap index ($k=1$ is the opening lap from standing start). |
| $j$ | $\{1, 2, \ldots, J\}$ | $\text{dimensionless}$ | Stint index within a race strategy $\pi$, where $J \in \mathbb{Z}^+$ is the total number of stints. |
| $J - 1$ | $\mathbb{Z}_{\ge 0}$ | $\mathrm{stops}$ | Total number of inter-stint pit stops executed during the race. |
| $n_j$ | $\{1, 2, \ldots, L\}$ | $\mathrm{laps}$ | Integer number of laps completed during stint $j$, subject to $\sum_{j=1}^J n_j = L$. |
| $a_{j,m}$ | $\mathbb{Z}_{\ge 0}$ | $\mathrm{laps}$ | Cumulative tyre age (in laps completed prior to the start of the $m$-th lap of stint $j$, where $m \in \{1, \ldots, n_j\}$ and $a_{j,m} = a_{j,0} + m - 1$). |
| $a_k$ | $\mathbb{Z}_{\ge 0}$ | $\mathrm{laps}$ | Tyre age in completed laps at the start of race lap $k$ ($a_k = 0$ for a brand-new tyre set on its out-lap). |
| $\mathcal{C}$ | $\{C1, C2, C3, C4, C5\}$ | $\text{categorical}$ | Season-versioned catalogue of physical dry slick tyre compounds supplied by Pirelli. |
| $\mathcal{N}_e$ | $\subset \mathcal{C}, \lvert\mathcal{N}_e\rvert = 3$ | $\text{categorical}$ | The three physical compounds officially nominated for event $e$. |
| $\mathcal{R}$ | $\{S, M, H\}$ | $\text{categorical}$ | Event-level tyre roles: Soft ($S$), Medium ($M$), and Hard ($H$). |
| $m_e$ | $\mathcal{R} \hookrightarrow \mathcal{C}$ | $\text{mapping}$ | Injective event role mapping assigning each role $r \in \mathcal{R}$ to a distinct physical compound $c = m_e(r) \in \mathcal{N}_e$. |
| $r$ | $\text{TrackVersion ID}$ | $\text{identifier}$ | Composite circuit, layout version, and surface version identifier. |
| $v$ | $\text{Vehicle ID}$ | $\text{identifier}$ | Vehicle and baseline mechanical/aerodynamic configuration identifier. |
| $i$ | $\{\mathrm{FL}, \mathrm{FR}, \mathrm{RL}, \mathrm{RR}\}$ | $\text{categorical}$ | Individual tyre corner index: Front-Left, Front-Right, Rear-Left, Rear-Right (or $\mathrm{agg}$ for aggregate set model). |
| $T_{\mathrm{race}}(\pi)$ | $\mathbb{R}^+$ | $\mathrm{s}$ | Total predicted elapsed race time for strategy $\pi$. |
| $T_k$ | $\mathbb{R}^+$ | $\mathrm{s}$ | Predicted flying (green-flag) lap time on race lap $k$, excluding incremental pit-lane time loss. |
| $T_{\mathrm{ref}}(r, v)$ | $\mathbb{R}^+$ | $\mathrm{s}$ | Reference baseline lap time for vehicle $v$ on circuit version $r$ at reference fuel mass $M_{\mathrm{ref}}$, reference compound $c_{\mathrm{ref}}$, new tyre health ($h=1$), and reference environment $\mathbf{w}_{\mathrm{ref}}$. |
| $\Delta T_{\mathrm{fuel},k}$ | $\mathbb{R}$ | $\mathrm{s}$ | Lap-time offset on lap $k$ due to instantaneous fuel load deviation from $M_{\mathrm{ref}}$. |
| $\Delta T_{\mathrm{evol},k}$ | $\mathbb{R}$ | $\mathrm{s}$ | Lap-time offset on lap $k$ due to progressive circuit rubbering/track evolution. |
| $\Delta T_{\mathrm{start},k}$ | $\mathbb{R}_{\ge 0}$ | $\mathrm{s}$ | Standing-start and opening-lap geometry/acceleration time penalty (non-zero only for $k = 1$). |
| $\Delta T_{\mathrm{comp}}(c, r)$ | $\mathbb{R}$ | $\mathrm{s}$ | Fresh-tyre intrinsic pace offset of physical compound $c \in \mathcal{C}$ relative to reference compound $c_{\mathrm{ref}}$ on circuit $r$. |
| $\Delta T_{\mathrm{tyre},k}$ | $\mathbb{R}_{\ge 0}$ | $\mathrm{s}$ | Lap-time loss on lap $k$ caused by degraded tyre condition $\mathbf{z}_k$ and thermal state $\boldsymbol{\theta}_k$. |
| $\Delta T_{\mathrm{in/out},k}$ | $\mathbb{R}_{\ge 0}$ | $\mathrm{s}$ | Cold-tyre out-lap warm-up penalty or in-lap braking-approach residual not already captured in pit-lane traversal time $P_j$. |
| $P_j$ | $\mathbb{R}^+$ | $\mathrm{s}$ | Net incremental pit-stop time loss for pit stop $j \in \{1, \ldots, J-1\}$ relative to uninterrupted racing on the main straight. |
| $h_{k,i}$ | $[0, 1]$ | $\text{dimensionless}$ | Normalized remaining tyre health of corner $i$ at the start of lap $k$ ($h_{k,i} = 1$ is new; $h_{k,i} = 0$ is fully depleted to terminal wear limit). |
| $w_{k,i}$ | $[0, 1]$ | $\text{dimensionless}$ | Normalized tyre wear state defined strictly as the complement $w_{k,i} \equiv 1 - h_{k,i}$ ($w_{k,i} = 0$ is new; $w_{k,i} = 1$ is terminal wear). |
| $\Delta h_{k,i}$ | $[0, 1]$ | $\mathrm{lap}^{-1}$ | Per-lap health depletion (wear increment $\Delta w_{k,i}$) on lap $k$ for corner $i$. |
| $D_{\mathrm{base}}(c, r, i)$ | $\mathbb{R}^+$ | $\mathrm{lap}^{-1}$ | Static baseline health depletion rate per lap for physical compound $c$ on circuit version $r$ and corner $i$ under reference conditions. |
| $S_{\mathrm{str}}(r, i)$ | $\mathbb{R}^+$ | $\text{dimensionless}$ | Normalized circuit mechanical/energy stress index for circuit $r$ and corner $i$ ($S_{\mathrm{str}} = 1.0$ for reference circuit average). |
| $F_{\mathrm{env}}(c, \mathbf{w}_k)$ | $\mathbb{R}^+$ | $\text{dimensionless}$ | Dynamic environmental degradation multiplier on lap $k$ ($F_{\mathrm{env}} = 1.0$ at reference environment $\mathbf{w}_{\mathrm{ref}}$). |
| $F_{\mathrm{usage}}(k, a_k)$ | $\mathbb{R}^+$ | $\text{dimensionless}$ | Usage/load-dependent degradation multiplier accounting for instantaneous vehicle mass and tyre age ($F_{\mathrm{usage}} = 1.0$ at reference load). |
| $T_{\mathrm{trk},k}$ | $[-10, 80]$ | ${}^\circ\mathrm{C}$ | Track surface temperature on lap $k$. |
| $T_{\mathrm{air},k}$ | $[-10, 60]$ | ${}^\circ\mathrm{C}$ | Ambient air temperature (at $2\,\mathrm{m}$ height) on lap $k$. |
| $\phi_k$ | $[0, 100]$ | $\%$ | Relative humidity on lap $k$. |
| $\omega_k$ | $[0, 1]$ | $\text{dimensionless}$ | Normalized racing-line surface wetness index on lap $k$ ($\omega_k = 0$ is completely dry). |
| $\theta_{k,i}$ | $[0, 200]$ | ${}^\circ\mathrm{C}$ | Effective bulk/surface tyre temperature of corner $i$ on lap $k$ (used in Stage M4 thermal extension). |
| $M_{\mathrm{fuel},0}$ | $[0, 115]$ | $\mathrm{kg}$ | Initial race fuel load at lights out ($k=1$). |
| $\mu_{\mathrm{fuel}}(r, v)$ | $\mathbb{R}^+$ | $\mathrm{kg}\cdot\mathrm{lap}^{-1}$ | Mean fuel mass consumed per racing lap on circuit $r$ by vehicle $v$. |
| $\gamma_{\mathrm{fuel}}(r)$ | $\mathbb{R}^+$ | $\mathrm{s}\cdot\mathrm{kg}^{-1}$ | Lap-time sensitivity to carried fuel mass on circuit $r$. |
| $\alpha_c$ | $\mathbb{R}^+$ | $\mathrm{s}$ | Linear tyre-wear-to-lap-time-loss conversion coefficient for physical compound $c$ (seconds lost per unit of normalized wear $w$). |
| $\beta_c, \eta_c$ | $\mathbb{R}_{\ge 0}, \mathbb{R}_{>1}$ | $\mathrm{s}, \text{dimensionless}$ | Nonlinear cliff amplitude ($\mathrm{s}$) and dimensionless power-law/exponential cliff exponent after critical wear threshold $w_{\mathrm{crit},c}$. |

---

## 3. System Architecture and Causal Data-Flow Diagram

### 3.1 Modular Information Flow and Anti-Conflation Design

To prevent double-counting of physical effects and ensure clean C++ interface boundaries, the simulator separates **Offline Data Ingestion & Calibration** from **Runtime Strategy Evaluation & Optimisation**. The diagram below illustrates the exact causal and data-flow pathways from persistent storage tables through the mathematical evaluation engine to the strategy optimiser.

```
====================================================================================================
                        OFFLINE DATA INGESTION & CALIBRATION LAYER
====================================================================================================
 [Official FIA / Circuit Docs]   [FastF1 / OpenF1 Telemetry]   [Open-Meteo / Session Weather]
               |                              |                              |
               v                              v                              v
 +---------------------------+  +---------------------------+  +---------------------------+
 | 1. TrackLayoutVersion     |  | 8. HistoricalLapDataset   |  | 3. ScenarioEnvironment    |
 |    & TrackSurfaceProfile  |  |    (Cleaned Cal/Val/Test) |  |    (Constant or Series)   |
 +---------------------------+  +---------------------------+  +---------------------------+
               |                              |                              |
               |               +--------------+--------------+               |
               |               | Offline Calibration Engine  |               |
               |               | (Confounder Control & Fit)  |               |
               |               +--------------+--------------+               |
               |                              |                              |
               |                              v                              |
 +---------------------------+  +---------------------------+  +---------------------------+
 | 4. PhysicalCompoundCat.   |  | 7. CalibratedModelParams  |  | 6. VehicleSpecification   |
 |    (C1..C5 by Season)     |  |    (Versioned Fits + CI)  |  |    (Mass, Fuel, Ref Pace) |
 +---------------------------+  +---------------------------+  +---------------------------+
               \                              |                              /
                \                             |                             /
==============================================|=====================================================
                         RUNTIME SIMULATION & STRATEGY OPTIMISATION ENGINE
==============================================|=====================================================
                                              |
               +------------------------------+------------------------------+
               |                2. EventConfiguration & Rules                |
               |   5. EventRoleMapping: m_e : {S, M, H} >--> {C1..C5}        |
               |      (Bijection Check onto Nominated Set N_e, |N_e| = 3)    |
               +------------------------------+------------------------------+
                                              |
                                              v
               +-------------------------------------------------------------+
               |               Stint & Lap Mathematical Engine               |
               |                                                             |
               |  For each lap k in Stint j (role r_j -> compound c = m_e):  |
               |   1. Base Pace: T_ref(r,v) + dT_fuel(k) + dT_evol(k)        |
               |   2. Compound Offset: dT_comp(c, r)                         |
               |   3. Tyre Penalty: dT_tyre(c, z_k, theta_k)                 |
               |   4. State Update: z_{k+1} = z_k - D_base * F_env * F_usage |
               |   5. Pit Cost Accounting: P_j added at stint boundaries     |
               +------------------------------+------------------------------+
                                              |
                                              v
               +-------------------------------------------------------------+
               |             Strategy Optimiser (DAG / DP / Enum)            |
               |   Minimise T_race(pi) over Pi_feasible (FIA 2-compound      |
               |   rule, allocation limits, max wear h_k >= h_min)           |
               +------------------------------+------------------------------+
                                              |
                                              v
               +-------------------------------------------------------------+
               |       Output: Optimal Strategy pi*, Ranked Alternatives,    |
               |       Lap-by-Lap Trace, & Sensitivity Bounds                |
               +-------------------------------------------------------------+
```

### 3.2 Causal Separation and Double-Counting Prevention Rules

Every physical input enters the mathematical engine along a strictly defined causal pathway so that no effect is counted twice:

1. **Circuit Geometry vs. Surface Roughness:** Circuit length $L_{\mathrm{lap}}$ and speed profile determine the reference lap time $T_{\mathrm{ref}}(r, v)$, whereas lateral/longitudinal energy exposure $E_{\mathrm{lat}}, E_{\mathrm{long}}$ and macro-/micro-roughness $A_{\mathrm{surf}}$ enter the static degradation severity index $S_{\mathrm{str}}(r, i)$. Once $S_{\mathrm{str}}(r, i)$ is computed from corner/braking energies and surface abrasiveness, the tyre wear transition does not apply a second ad-hoc "high-speed corner multiplier."
2. **Fuel Mass Dual Pathway (Direct Pace vs. Mechanical Tyre Load):** Carried fuel mass $M_{\mathrm{fuel},k}$ slows the car directly on every lap via Newton's second law and induced drag ($\Delta T_{\mathrm{fuel},k} = \gamma_{\mathrm{fuel}}(M_{\mathrm{fuel},k} - M_{\mathrm{ref}})$), which is an immediate, reversible pace effect that vanishes as fuel burns off. Separately, higher vertical normal load increases tyre sliding work per lap via $F_{\mathrm{usage}}(k)$, which permanently depletes tyre health $h_{k,i}$. Because $\Delta T_{\mathrm{fuel},k}$ is controlled for during parameter estimation before fitting tyre wear slopes, the two pathways are orthogonal and do not double-count mass.
3. **Intrinsic Compound Pace vs. Tyre Condition Penalty:** The fresh-tyre pace offset $\Delta T_{\mathrm{comp}}(c, r)$ represents the grip difference between compound $c$ and $c_{\mathrm{ref}}$ at $h = 1$ (zero wear, optimal temperature). All subsequent lap-time increases as $h_k$ decreases are captured exclusively in $\Delta T_{\mathrm{tyre},k}$, which satisfies the boundary condition $\Delta T_{\mathrm{tyre}}(c, h=1, \theta_{\mathrm{opt}}) = 0$.
4. **Track Temperature vs. Air Temperature vs. Tyre Temperature:** Track surface temperature $T_{\mathrm{trk},k}$ governs conductive heat flux at the contact patch and viscoelastic hysteresis in the tread rubber, whereas ambient air temperature $T_{\mathrm{air},k}$ and wind speed $v_{\mathrm{wind},k}$ govern convective cooling from the exposed carcass surface. In Stages M0–M3 (where internal tyre temperature $\theta_k$ is not tracked as a dynamic state variable), $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ enter $F_{\mathrm{env}}$ with distinct, separately calibrated coefficients. When Stage M4 activates the explicit thermal state $\theta_{k,i}$, $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ drive the heat-balance differential equation for $\theta_{k,i}$, and $F_{\mathrm{env}}$ is replaced by the direct thermal degradation function $F_{\mathrm{therm}}(\theta_{k,i})$ so environmental temperatures are never counted both directly and via $\theta_{k,i}$.

---

## 4. Database Schema, Versioning, Provenance, and Missing-Data Policy

### 4.1 Universal Provenance and Metadata Envelope

In accordance with Section 2 of the specification, no numeric or categorical parameter in the database may exist as a bare value. Every parameter record or table row embeds (or foreign-keys to) a standardized `ParameterProvenanceMetadata` structure:

| Attribute Name | C++ / SQL Type | Permitted Domain / Values | Role and Semantics |
| :--- | :--- | :--- | :--- |
| `source_status` | `ENUM` | `PUBLISHED_OBSERVED`, `DERIVED`, `CALIBRATED`, `ESTIMATED`, `ASSUMED`, `UNKNOWN_UNAVAILABLE` | Mandatory classification of epistemological status. |
| `source_name` | `VARCHAR(128)` | Non-empty string (e.g., `"FIA_Event_Notes"`, `"FastF1_v3.4"`, `"OpenMeteo_ERA5"`) | Primary origin of the observation or estimate. |
| `source_ref_uri` | `VARCHAR(512)` | Valid URI or document citation | Traceable link or regulatory document identifier. |
| `retrieved_at_utc` | `TIMESTAMP` | ISO-8601 UTC timestamp | Exact time the value was ingested or fitted. |
| `valid_from_date` | `DATE` | ISO-8601 date | Start of validity window (e.g., resurfacing date or season start). |
| `valid_to_date` | `DATE NULL` | ISO-8601 date or `NULL` (currently active) | End of validity window; prevents historical overwrites. |
| `derivation_method` | `VARCHAR(256)` | Formula or estimator ID (e.g., `"OLS_Huber_Stint_Fit_v1.2"`) | Required whenever `source_status` is `DERIVED`, `CALIBRATED`, or `ESTIMATED`. |
| `uncertainty_std` | `DOUBLE NULL` | $\mathbb{R}_{\ge 0}$ or `NULL` | Standard error or $1\sigma$ uncertainty in native parameter units. |
| `ci_lower_95`, `ci_upper_95` | `DOUBLE NULL` | $\mathbb{R}$ ($ci\_lower \le value \le ci\_upper$) | $95\%$ confidence or credible interval bounds. |
| `confidence_tier` | `ENUM` | `HIGH`, `MEDIUM`, `LOW`, `PROVISIONAL` | Qualitative summary of parameter reliability. |
| `missing_policy` | `ENUM` | `REJECT_SIMULATION`, `USE_DECLARED_DEFAULT`, `INTERPOLATE_BOUNDED`, `DEGRADE_TO_LOWER_STAGE` | Explicit instruction to the C++ engine if value is `NULL` / `UNKNOWN_UNAVAILABLE`. |

**Strict Missing-Value Rule:** In C++, optional numeric fields must be represented using `std::optional<double>` (or explicit status flags) rather than sentinel zeros (`0.0`). A value of `0.0` is reserved strictly for a verified physical measurement of zero (e.g., `precipitation_rate_mmph = 0.0` during a dry session).

### 4.2 Entity-Relationship Overview of the Eight Core Schemas

The database comprises eight normalized relational tables (or C++ serializable structs):

1. `TrackLayoutVersion` (1) $\longrightarrow$ (M) `TrackSurfaceProfile` (linked via `track_id`, `layout_version`).
2. `TrackLayoutVersion` (1) $\longrightarrow$ (M) `EventConfiguration` (linked via `track_id`, `layout_version`).
3. `EventConfiguration` (1) $\longrightarrow$ (M) `ScenarioEnvironmentSeries` (linked via `scenario_id`).
4. `PhysicalCompoundCatalogue` (1) $\longrightarrow$ (M) `EventRoleMapping` (linked via `season_year`, `compound_id`).
5. `EventConfiguration` (1) $\longrightarrow$ (3) `EventRoleMapping` (linked via `event_id`, `role_id`).
6. `VehicleSpecification` (1) $\longrightarrow$ (M) `CalibratedModelParameters` (linked via `vehicle_id`, `model_version`).
7. `TrackSurfaceProfile` & `PhysicalCompoundCatalogue` $\longrightarrow$ `CalibratedModelParameters` (linked via `track_id`, `surface_version`, `compound_id`).
8. `HistoricalLapDataset` $\longrightarrow$ consumed offline by the calibration pipeline to populate `CalibratedModelParameters`.

### 4.3 Schema 1: `TrackLayoutVersion` and `TrackSurfaceProfile`

Circuit geometry and surface characteristics are versioned separately so that a mid-season resurfacing (which alters grip and abrasiveness without changing lap length) increments `surface_version` while preserving `layout_version`.

| Field Name | Type | Unit | Legal Range | Typical Status | Missing-Value Policy | Consuming Equation / Module |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `track_id` (PK) | `VARCHAR(32)` | ID | `[a-z0-9_]+` | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Primary key across all circuit lookups. |
| `layout_version` (PK) | `INT` | version | $\ge 1$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Versioned layout geometry key. |
| `surface_version` (PK) | `INT` | version | $\ge 1$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Versioned asphalt/resurfacing epoch key. |
| `track_name` | `VARCHAR(128)` | text | Non-empty | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Reporting and display metadata. |
| `country`, `locality` | `VARCHAR(64)` | text | ISO country / city | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` | Weather API coordinate verification. |
| `latitude_deg`, `longitude_deg` | `DOUBLE` | $\mathrm{deg}$ | $[-90,90], [-180,180]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Weather ingestion spatial query. |
| `altitude_msl_m` | `DOUBLE` | $\mathrm{m}$ | $[-50, 2500]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($0\,\mathrm{m}$) | Barometric air density calculation. |
| `valid_from_date`, `valid_to_date` | `DATE` | ISO date | Valid calendar dates | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` (`valid_from`) | Temporal consistency validation. |
| `lap_length_m` | `DOUBLE` | $\mathrm{m}$ | $[3000, 8000]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Eq. (6.1) reference pace scaling & fuel burn. |
| `sector1_length_m`, `sector2_length_m`, `sector3_length_m` | `DOUBLE NULL` | $\mathrm{m}$ | $\sum S_i = L_{\mathrm{lap}} \pm 5\,\mathrm{m}$ | `DERIVED` (telemetry distance) | `DEGRADE_TO_LOWER_STAGE` (lap-level) | Sector-resolved pace & wear extension. |
| `race_direction` | `ENUM` | category | `CLOCKWISE`, `ANTICLOCKWISE`, `FIGURE_EIGHT` | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` (`CLOCKWISE`) | Left/Right tyre load asymmetry check. |
| `pit_lane_length_m` | `DOUBLE` | $\mathrm{m}$ | $[200, 600]$ | `PUBLISHED_OBSERVED` / `DERIVED` | `USE_DECLARED_DEFAULT` (use calibrated $P_j$) | Eq. (9.2) pit-lane traversal time $T_{\mathrm{pit-lane}}$. |
| `pit_speed_limit_kmh` | `DOUBLE` | $\mathrm{km\cdot h^{-1}}$ | $[60.0, 80.0]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Eq. (9.2) pit-lane traversal time $T_{\mathrm{pit-lane}}$. |
| `corners_total_count` | `INT` | count | $[8, 28]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` | Descriptive geometry validation. |
| `corners_left_count`, `corners_right_count` | `INT` | count | $N_L + N_R = N_{\mathrm{tot}}$ | `PUBLISHED_OBSERVED` | `DEGRADE_TO_LOWER_STAGE` (symmetric) | Eq. (7.3) asymmetric corner load share $\lambda_{r,i}$. |
| `frac_lap_straight` | `DOUBLE NULL` | $\text{dimensionless}$ | $[0.20, 0.85]$ | `DERIVED` (telemetry) | `USE_DECLARED_DEFAULT` ($0.60$) | Cooling exposure & power sensitivity. |
| `frac_lap_corner_slow`, `med`, `high` | `DOUBLE NULL` | $\text{dimensionless}$ | $\sum f = 1 - f_{\mathrm{str}}$ | `DERIVED` (telemetry speed bands) | `DEGRADE_TO_LOWER_STAGE` | Eq. (7.2) lateral/traction stress proxy $S_{\mathrm{str}}$. |
| `lateral_energy_index_norm` | `DOUBLE` | $\text{dimensionless}$ | $[0.40, 2.00]$ (ref $=1.0$) | `DERIVED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($1.0$) | Eq. (7.2) static circuit stress $S_{\mathrm{str}}(r,i)$. |
| `braking_energy_index_norm` | `DOUBLE` | $\text{dimensionless}$ | $[0.40, 2.00]$ (ref $=1.0$) | `DERIVED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($1.0$) | Eq. (7.2) longitudinal braking wear share. |
| `traction_energy_index_norm` | `DOUBLE` | $\text{dimensionless}$ | $[0.40, 2.00]$ (ref $=1.0$) | `DERIVED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($1.0$) | Eq. (7.2) rear-axle traction wear share. |
| `surface_abrasiveness_score` | `DOUBLE` | $\text{dimensionless}$ | $[1.0, 5.0]$ (ref $=3.0$) | `ESTIMATED` (Pirelli preview) | `USE_DECLARED_DEFAULT` ($3.0$) | Eq. (7.2) surface roughness multiplier. |
| `surface_grip_baseline_score` | `DOUBLE` | $\text{dimensionless}$ | $[1.0, 5.0]$ (ref $=3.0$) | `ESTIMATED` | `USE_DECLARED_DEFAULT` ($3.0$) | Eq. (6.1) surface grip offset & sliding factor. |
| `bumpiness_index_score` | `DOUBLE NULL` | $\text{dimensionless}$ | $[1.0, 5.0]$ | `ESTIMATED` | `USE_DECLARED_DEFAULT` ($3.0$) | Vertical load variance note (descriptive in M0–M2). |
| `track_evolution_amplitude_s` | `DOUBLE` | $\mathrm{s}$ | $[0.0, 2.5]$ | `CALIBRATED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($0.0$) | Eq. (6.3) lap-time evolution $\Delta T_{\mathrm{evol},k}$. |
| `track_evolution_decay_laps` | `DOUBLE` | $\mathrm{laps}$ | $[5.0, 60.0]$ | `CALIBRATED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($20.0$) | Eq. (6.3) exponential saturation constant $\tau_{\mathrm{evol}}$. |

### 4.4 Schema 2: `EventConfiguration` and Regulatory Constraints

| Field Name | Type | Unit | Legal Range | Typical Status | Missing-Value Policy | Consuming Equation / Module |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `event_id` (PK) | `VARCHAR(64)` | ID | Non-empty | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Master event identifier. |
| `season_year` | `INT` | year | $[2018, 2035]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Joins `PhysicalCompoundCatalogue`. |
| `track_id`, `layout_version`, `surface_version` (FK) | `VARCHAR`, `INT` | ID | Valid FK | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Selects circuit geometry and surface profile. |
| `race_distance_laps` ($L$) | `INT` | $\mathrm{laps}$ | $[1, 100]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Eq. (9.1) stint sum constraint $\sum n_j = L$. |
| `nominated_compounds_set` ($\mathcal{N}_e$) | `ARRAY<ENUM>[3]` | subset of $\mathcal{C}$ | 3 distinct values in $\{C1..C5\}$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Section 7.2 bijection validation check. |
| `is_hypothetical_override` | `BOOL` | boolean | `true` / `false` | `ASSUMED` / `PUBLISHED` | `REJECT_SIMULATION` | Flags non-official scenario experiments. |
| `min_distinct_dry_compounds` | `INT` | count | $\{1, 2, 3\}$ (FIA dry default $=2$) | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($2$) | Eq. (9.5) regulatory compound diversity constraint. |
| `min_pit_stops`, `max_pit_stops` | `INT` | $\mathrm{stops}$ | $0 \le \text{min} \le \text{max} \le 6$ | `PUBLISHED_OBSERVED` / `ASSUMED` | `USE_DECLARED_DEFAULT` ($1, 4$) | Eq. (9.4) feasible stint count $J \in [P_{\min}+1, P_{\max}+1]$. |
| `min_stint_length_laps` | `INT` | $\mathrm{laps}$ | $[1, L]$ | `ASSUMED` / `PUBLISHED` | `USE_DECLARED_DEFAULT` ($1$) | Eq. (9.4) stint length lower bound. |
| `max_stint_length_laps` | `INT NULL` | $\mathrm{laps}$ | $[1, L]$ or `NULL` | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($L$) | Enforces FIA mandatory maximum stint rules (e.g., Qatar 2023). |
| `available_sets_soft`, `med`, `hard` | `INT` | sets | $[1, 8]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($2, 2, 2$) | Eq. (9.6) physical tyre inventory constraint. |

### 4.5 Schema 3: `ScenarioEnvironmentSeries`

Environmental inputs are stored in a lap-indexed table (`lap_index` $k \in \{1, \ldots, L\}$). In **Constant Mode**, a single row with `lap_index = 1` and `interpolation_mode = CONSTANT_HOLD` supplies fixed values across all $L$ laps. In **Time-Varying Mode**, timestamps from weather stations or forecasts are mapped onto lap indices using cumulative nominal race time, and missing intermediate laps are filled according to `interpolation_mode` (`LINEAR_BOUNDED` or `PREVIOUS_VALUE_HOLD`), never via unbounded extrapolation outside $[1, L]$.

| Field Name | Type | Unit | Legal Range | Typical Status | Missing-Value Policy | Consuming Equation / Module |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `scenario_id` (PK, FK) | `VARCHAR(64)` | ID | Non-empty | `PUBLISHED` / `ASSUMED` | `REJECT_SIMULATION` | Links environment to simulation run. |
| `lap_index` ($k$, PK) | `INT` | $\mathrm{lap}$ | $[1, L]$ | `DERIVED` / `ASSUMED` | `INTERPOLATE_BOUNDED` | Lap alignment key. |
| `measurement_semantics` | `ENUM` | category | `CIRCUIT_SENSOR`, `STATION_OBS`, `REANALYSIS_GRID`, `FORECAST`, `MANUAL_SCENARIO` | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Prevents conflating regional forecasts with track sensors. |
| `track_temperature_c` ($T_{\mathrm{trk},k}$) | `DOUBLE` | ${}^\circ\mathrm{C}$ | $[5.0, 70.0]$ | `PUBLISHED_OBSERVED` / `ESTIMATED` | `DEGRADE_TO_LOWER_STAGE` (infer via Eq. 8.4) | Eq. (8.1) thermal degradation multiplier $F_{\mathrm{env}}$. |
| `air_temperature_c` ($T_{\mathrm{air},k}$) | `DOUBLE` | ${}^\circ\mathrm{C}$ | $[0.0, 50.0]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Eq. (8.1) convective cooling term in $F_{\mathrm{env}}$ & Eq. (7.9). |
| `relative_humidity_pct` ($\phi_k$) | `DOUBLE` | $\%$ | $[0.0, 100.0]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($50.0\%$) | Eq. (8.1) provisional neutral humidity term ($\psi_{\mathrm{hum}} = 0$). |
| `air_pressure_hpa` ($p_{\mathrm{air},k}$) | `DOUBLE NULL` | $\mathrm{hPa}$ | $[750.0, 1060.0]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($1013.25\,\mathrm{hPa}$) | Optional air density drag/cooling adjustment. |
| `wind_speed_ms` ($v_{\mathrm{wind},k}$) | `DOUBLE NULL` | $\mathrm{m\cdot s^{-1}}$ | $[0.0, 35.0]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($0.0\,\mathrm{m\cdot s^{-1}}$) | Convective cooling coefficient in Stage M4. |
| `wind_direction_deg` | `DOUBLE NULL` | $\mathrm{deg}$ | $[0.0, 360.0)$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` (`NULL`) | Headwind/tailwind straight-line drag extension. |
| `solar_radiation_wm2` ($I_{\mathrm{sol},k}$) | `DOUBLE NULL` | $\mathrm{W\cdot m^{-2}}$ | $[0.0, 1300.0]$ | `PUBLISHED_OBSERVED` (ERA5) | `USE_DECLARED_DEFAULT` ($500.0\,\mathrm{W\cdot m^{-2}}$) | Eq. (8.4) fallback inference of $T_{\mathrm{trk},k}$ when missing. |
| `cloud_cover_pct` | `DOUBLE NULL` | $\%$ | $[0.0, 100.0]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($50.0\%$) | Radiative heat balance in Stage M4. |
| `precipitation_rate_mmph` | `DOUBLE` | $\mathrm{mm\cdot h^{-1}}$ | $[0.0, 100.0]$ | `PUBLISHED_OBSERVED` | `USE_DECLARED_DEFAULT` ($0.0$) | Wet-weather trigger check (Stage M5). |
| `surface_wetness` ($\omega_k$) | `DOUBLE` | $\text{dimensionless}$ | $[0.0, 1.0]$ | `ESTIMATED` / `DERIVED` | `USE_DECLARED_DEFAULT` ($0.0$ if dry) | Dry-model domain guard ($\omega_k > 0.05$ triggers wet warning). |

### 4.6 Schema 4 & Schema 5: `PhysicalCompoundCatalogue` and `EventRoleMapping`

The physical compound catalogue stores intrinsic properties of `C1`–`C5` for a specific `season_year` and `spec_version` (permitting mid-season construction changes, such as the 2023 Silverstone construction update, without altering compound codes).

| Table | Field Name | Type | Unit | Legal Range | Typical Status | Missing-Value Policy | Consuming Equation / Module |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `PhysicalCompoundCatalogue` | `season_year` (PK) | `INT` | year | $[2018, 2035]$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Compound specification version key. |
| `PhysicalCompoundCatalogue` | `spec_version` (PK) | `INT` | version | $\ge 1$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Mid-season construction revision key. |
| `PhysicalCompoundCatalogue` | `compound_id` (PK) | `ENUM` | category | `C1`, `C2`, `C3`, `C4`, `C5` | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Physical slick compound identifier. |
| `PhysicalCompoundCatalogue` | `hardness_rank` | `INT` | ordinal | $1$ (`C1` hardest) to $5$ (`C5` softest) | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Monotonicity verification in Section 7.2. |
| `PhysicalCompoundCatalogue` | `pace_offset_ref_s` ($\Delta T_{\mathrm{comp},0}$) | `DOUBLE` | $\mathrm{s}$ | $[-2.0, +2.0]$ ($C3 \equiv 0.0\,\mathrm{s}$) | `CALIBRATED` | `REJECT_SIMULATION` | Eq. (6.4) fresh-tyre compound pace offset. |
| `PhysicalCompoundCatalogue` | `base_wear_rate_per_lap` ($d_{0,c}$) | `DOUBLE` | $\mathrm{lap}^{-1}$ | $[0.005, 0.080]$ | `CALIBRATED` | `REJECT_SIMULATION` | Eq. (7.2) baseline wear rate $D_{\mathrm{base}}$. |
| `PhysicalCompoundCatalogue` | `linear_deg_coeff_s` ($\alpha_c$) | `DOUBLE` | $\mathrm{s}$ | $[0.5, 8.0]$ | `CALIBRATED` | `REJECT_SIMULATION` | Eq. (7.6) wear-to-lap-time-loss slope. |
| `PhysicalCompoundCatalogue` | `cliff_wear_threshold` ($w_{\mathrm{crit},c}$) | `DOUBLE` | $\text{dimensionless}$ | $[0.50, 1.00]$ | `CALIBRATED` / `ASSUMED` | `USE_DECLARED_DEFAULT` ($1.0$ = linear only) | Eq. (7.6) onset of nonlinear cliff penalty. |
| `PhysicalCompoundCatalogue` | `cliff_amplitude_s` ($\beta_c$) | `DOUBLE` | $\mathrm{s}$ | $[0.0, 10.0]$ | `CALIBRATED` / `ASSUMED` | `USE_DECLARED_DEFAULT` ($0.0\,\mathrm{s}$) | Eq. (7.6) nonlinear degradation amplitude. |
| `PhysicalCompoundCatalogue` | `cliff_exponent` ($\eta_c$) | `DOUBLE` | $\text{dimensionless}$ | $[1.5, 4.0]$ | `CALIBRATED` / `ASSUMED` | `USE_DECLARED_DEFAULT` ($2.0$) | Eq. (7.6) nonlinear power-law exponent. |
| `PhysicalCompoundCatalogue` | `thermal_deg_sens_per_c` ($\kappa_{\mathrm{trk},c}$) | `DOUBLE` | ${}^\circ\mathrm{C}^{-1}$ | $[0.0, 0.05]$ | `CALIBRATED` | `USE_DECLARED_DEFAULT` ($0.01\,{}^\circ\mathrm{C}^{-1}$) | Eq. (8.1) track temperature wear sensitivity. |
| `PhysicalCompoundCatalogue` | `opt_temp_window_min_c`, `max_c` | `DOUBLE NULL` | ${}^\circ\mathrm{C}$ | $[70.0, 145.0]$ | `ESTIMATED` | `DEGRADE_TO_LOWER_STAGE` | Stage M4 thermal window penalty Eq. (7.10). |
| `EventRoleMapping` | `event_id` (PK, FK) | `VARCHAR(64)` | ID | Valid FK | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Binds mapping to specific Grand Prix. |
| `EventRoleMapping` | `role_id` (PK) | `ENUM` | role | `SOFT`, `MEDIUM`, `HARD` | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Domain of mapping $m_e$. |
| `EventRoleMapping` | `compound_id` (FK) | `ENUM` | compound | `C1`, `C2`, `C3`, `C4`, `C5` | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Image $m_e(r) \in \mathcal{N}_e$; enforced unique per `event_id`. |

### 4.7 Schema 6, Schema 7, and Schema 8: Vehicle, Calibrated Parameters, and Historical Observations

| Table | Field Name | Type | Unit | Legal Range | Typical Status | Missing-Value Policy | Consuming Equation / Module |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `VehicleSpecification` | `vehicle_id` (PK) | `VARCHAR(64)` | ID | Non-empty | `PUBLISHED` / `ESTIMATED` | `REJECT_SIMULATION` | Identifies car/team configuration. |
| `VehicleSpecification` | `dry_mass_kg` ($M_{\mathrm{dry}}$) | `DOUBLE` | $\mathrm{kg}$ | $[795.0, 820.0]$ | `PUBLISHED_OBSERVED` (FIA min) | `USE_DECLARED_DEFAULT` ($798.0\,\mathrm{kg}$) | Total mass load calculation. |
| `VehicleSpecification` | `initial_fuel_load_kg` ($M_{\mathrm{fuel},0}$) | `DOUBLE` | $\mathrm{kg}$ | $[10.0, 110.0]$ | `ESTIMATED` | `USE_DECLARED_DEFAULT` ($100.0\,\mathrm{kg}$) | Eq. (6.2) fuel load trajectory $M_{\mathrm{fuel},k}$. |
| `VehicleSpecification` | `fuel_burn_rate_kg_per_lap` ($\mu_{\mathrm{fuel}}$) | `DOUBLE` | $\mathrm{kg\cdot lap}^{-1}$ | $[1.20, 2.50]$ | `ESTIMATED` / `DERIVED` | `USE_DECLARED_DEFAULT` ($M_{\mathrm{fuel},0}/L$) | Eq. (6.2) lap-by-lap fuel mass update. |
| `VehicleSpecification` | `front_weight_dist_frac` | `DOUBLE` | $\text{dimensionless}$ | $[0.44, 0.48]$ | `PUBLISHED_OBSERVED` (FIA reg) | `USE_DECLARED_DEFAULT` ($0.46$) | Eq. (7.3) front/rear axle load split. |
| `VehicleSpecification` | `tyre_energy_load_mult` ($\zeta_v$) | `DOUBLE` | $\text{dimensionless}$ | $[0.85, 1.15]$ | `CALIBRATED` / `ASSUMED` | `USE_DECLARED_DEFAULT` ($1.00$) | Car-specific tyre wear severity factor. |
| `CalibratedModelParameters` | `param_set_id` (PK) | `VARCHAR(64)` | ID | Non-empty | `CALIBRATED` | `REJECT_SIMULATION` | Versioned parameter snapshot ID. |
| `CalibratedModelParameters` | `track_id`, `layout_version`, `surface_version` | `FK` | ID | Valid FK | `CALIBRATED` | `REJECT_SIMULATION` | Binds circuit-specific calibrated constants. |
| `CalibratedModelParameters` | `ref_lap_time_s` ($T_{\mathrm{ref}}(r,v)$) | `DOUBLE` | $\mathrm{s}$ | $[53.0, 125.0]$ | `CALIBRATED` | `REJECT_SIMULATION` | Eq. (6.1) anchor pace at $M_{\mathrm{ref}}, c_{\mathrm{ref}}$. |
| `CalibratedModelParameters` | `fuel_lap_sens_s_per_kg` ($\gamma_{\mathrm{fuel}}$) | `DOUBLE` | $\mathrm{s\cdot kg}^{-1}$ | $[0.020, 0.045]$ | `CALIBRATED` / `ESTIMATED` | `USE_DECLARED_DEFAULT` ($0.032\,\mathrm{s\cdot kg}^{-1}$) | Eq. (6.2) fuel mass time penalty. |
| `CalibratedModelParameters` | `pit_loss_total_s` ($P_{\mathrm{ref}}(r)$) | `DOUBLE` | $\mathrm{s}$ | $[14.0, 30.0]$ | `CALIBRATED` | `REJECT_SIMULATION` | Eq. (9.2) net incremental pit-stop loss. |
| `CalibratedModelParameters` | `lap1_standing_start_loss_s` ($\Delta T_{\mathrm{start}}$) | `DOUBLE` | $\mathrm{s}$ | $[2.0, 6.0]$ | `CALIBRATED` | `USE_DECLARED_DEFAULT` ($3.5\,\mathrm{s}$) | Eq. (6.1) opening-lap standing-start offset. |
| `CalibratedModelParameters` | `outlap_warmup_loss_s` ($\Delta T_{\mathrm{out}}(c)$) | `DOUBLE` | $\mathrm{s}$ | $[0.0, 2.5]$ | `CALIBRATED` | `USE_DECLARED_DEFAULT` ($0.5\,\mathrm{s}$) | Eq. (9.2) out-lap tyre warm-up penalty. |
| `HistoricalLapDataset` | `session_id`, `driver_id`, `lap_number` (PK) | `VARCHAR`, `INT` | ID / $\mathrm{lap}$ | $\ge 1$ | `PUBLISHED_OBSERVED` | `REJECT_SIMULATION` | Historical timing record key. |
| `HistoricalLapDataset` | `lap_time_s`, `stint_id`, `compound_id`, `tyre_age_laps` | `DOUBLE`, `INT`, `ENUM` | $\mathrm{s}$, $\mathrm{laps}$ | Valid ranges | `PUBLISHED_OBSERVED` | Filter out row if missing | Consumed by Section 11 calibration pipeline. |
| `HistoricalLapDataset` | `is_clean_green_lap` | `BOOL` | boolean | `true` / `false` | `DERIVED` (filtered) | Exclude if `false` | Confounder exclusion mask (Section 11.2). |

### 4.8 Illustrative Database Records (Explicitly Labelled Synthetic Examples)

The JSON records below are **illustrative synthetic examples** constructed purely to demonstrate schema structure, foreign-key relationships, and provenance metadata formatting. They do not represent measured Pirelli proprietary curves or official FIA telemetry records.

```json
{
  "schema_illustration_notice": "SYNTHETIC_EXAMPLE_RECORDS_FOR_SCHEMA_DOCUMENTATION_ONLY",
  "TrackLayoutVersion": {
    "track_id": "silverstone_gp",
    "layout_version": 2011,
    "surface_version": 2019,
    "track_name": "Silverstone Circuit",
    "country": "GBR",
    "locality": "Silverstone",
    "latitude_deg": 52.0786,
    "longitude_deg": -1.0169,
    "altitude_msl_m": 153.0,
    "valid_from_date": "2019-07-01",
    "valid_to_date": null,
    "lap_length_m": 5891.0,
    "pit_lane_length_m": 418.0,
    "pit_speed_limit_kmh": 80.0,
    "race_direction": "CLOCKWISE",
    "corners_total_count": 18,
    "corners_left_count": 8,
    "corners_right_count": 10,
    "lateral_energy_index_norm": 1.35,
    "braking_energy_index_norm": 0.82,
    "traction_energy_index_norm": 1.05,
    "surface_abrasiveness_score": 3.8,
    "surface_grip_baseline_score": 4.0,
    "provenance": {
      "source_status": "ESTIMATED",
      "source_name": "Synthetic_Track_Profile_Illustration",
      "retrieved_at_utc": "2026-03-01T00:00:00Z",
      "derivation_method": "Normalized_From_Public_Circuit_Map_And_Qualitative_Preview",
      "uncertainty_std": 0.15,
      "confidence_tier": "MEDIUM",
      "missing_policy": "USE_DECLARED_DEFAULT"
    }
  },
  "PhysicalCompoundCatalogue_Sample": {
    "season_year": 2026,
    "spec_version": 1,
    "compound_id": "C2",
    "hardness_rank": 2,
    "pace_offset_ref_s": 0.45,
    "base_wear_rate_per_lap": 0.018,
    "linear_deg_coeff_s": 2.40,
    "cliff_wear_threshold": 0.80,
    "cliff_amplitude_s": 3.50,
    "cliff_exponent": 2.50,
    "thermal_deg_sens_per_c": 0.012,
    "provenance": {
      "source_status": "CALIBRATED",
      "source_name": "Synthetic_Calibration_Demonstration",
      "derivation_method": "Huber_Robust_Stint_Regression_Synthetic",
      "uncertainty_std": 0.06,
      "ci_lower_95": 2.28,
      "ci_upper_95": 2.52,
      "confidence_tier": "MEDIUM",
      "missing_policy": "REJECT_SIMULATION"
    }
  }
}
```

---

## 5. Parameter Availability and Provenance Matrix

Before deriving the mathematical equations, every model input is audited against realistic public data availability. Proprietary internal tyre measurements (such as real-time tread thickness, carcass cord tension, viscoelastic loss tangent $\tan\delta$, and internal bulk rubber temperatures) are **not publicly available** and must never be treated as observable inputs.

| Parameter Category & Name | Symbol & Unit | Realistic Public Availability | Primary Credible Source Category | Epistemological Status (`source_status`) | Expected Confidence & Coverage Caveats | Default / Fallback Policy | Required in M0–M1? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Official Lap Length & Sector Boundaries** | $L_{\mathrm{lap}}, L_{s}$ ($\mathrm{m}$) | Universal ($100\%$ of F1 circuits) | [FIA Event Notes \& Circuit Maps](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110) / [FastF1 Circuit Info](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) | `PUBLISHED_OBSERVED` / `DERIVED` | **High** ($\pm 2\,\mathrm{m}$); official length is exact, sector distances derived from telemetry distance integration. | Reject simulation if $L_{\mathrm{lap}}$ is missing; degrade to full-lap model if sector lengths missing. | **Yes** |
| **Corner Count, Direction & Speed Profile** | $N_{\mathrm{tot}}, N_L, N_R, f_{\mathrm{slow/med/high}}$ | High ($100\%$ for counts; $\sim 95\%$ post-2018 for speed bands) | [FIA Circuit Maps](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110) & [FastF1 Car Telemetry](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) | `PUBLISHED_OBSERVED` (counts) / `DERIVED` (fractions) | **High**; corner definition depends on speed/curvature threshold; speed bands depend on car generation. | Fall back to symmetric load ($\lambda_{r,i} = 0.25$) and neutral stress ($S_{\mathrm{str}} = 1.0$). | **Yes** (M1) |
| **Surface Abrasiveness, Grip & Bumpiness** | $A_{\mathrm{surf}}, G_{\mathrm{surf}}, B_{\mathrm{surf}}$ ($[1, 5]$) | Qualitative / Discrete Ordinal ($1–5$ scale published per race) | [Pirelli Motorsport Event Previews](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1) | `ESTIMATED` | **Medium-Low** ($\pm 0.5$ score units); ordinal integers are subjective engineering ratings, not profilometer laser scans. | Set to neutral midpoint ($3.0$) and absorb residual circuit variance into calibrated track multiplier. | **Yes** (M1) |
| **Event Nominated Compounds & Role Mapping** | $\mathcal{N}_e \subset \mathcal{C}$, $m_e : \mathcal{R} \hookrightarrow \mathcal{C}$ | Universal ($100\%$ of post-2019 events) | [Pirelli Motorsport Press Releases](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1) & [Formula1.com Tyre Guide](https://www.google.com/search?q=https%3A%2F%2Fwww.formula1.com%2Fen%2Flatest%2Farticle%2Fthe-beginners-guide-to-formula-1-tyres.61SvF0Kfg29UR2SPhakDqd) | `PUBLISHED_OBSERVED` | **High** (exact regulatory fact); role mapping per race is unambiguous, though compound assignments per individual lap require timing feed cross-check. | Reject simulation if mapping fails bijection check onto $\mathcal{N}_e$. | **Yes** (M0) |
| **Session Air Temp, Track Temp & Humidity** | $T_{\mathrm{air},k}, T_{\mathrm{trk},k}$ (${}^\circ\mathrm{C}$), $\phi_k$ ($\%$) | High for historical sessions ($1\,\mathrm{min}$ cadence); Forecast only pre-race | [FastF1 Weather Data](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1), [OpenF1 Weather Endpoint](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F), [Open-Meteo API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs) | `PUBLISHED_OBSERVED` (session) / `ESTIMATED` (pre-race) | **High** for FOM session feed; **Medium** ($\pm 3\text{--}6\,{}^\circ\mathrm{C}$) when $T_{\mathrm{trk}}$ is inferred from [Open-Meteo](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs) solar radiation $I_{\mathrm{sol}}$ and $T_{\mathrm{air}}$. | If $T_{\mathrm{trk}}$ is absent in forecast mode, infer via radiative balance Eq. (8.4) and flag `ESTIMATED`. | **Yes** (M0 constant, M2 dynamic) |
| **Surface Wetness & Water Film Depth** | $\omega_k \in [0, 1]$ | **Unavailable** as direct physical measurement; boolean `Rainfall` only | [FastF1 Weather Feed](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) (`Rainfall` boolean) / [Open-Meteo Precipitation](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs) | `ESTIMATED` / `UNKNOWN_UNAVAILABLE` | **Low**; weather-station rainfall rate does not measure standing water on the racing line or spray evaporation. | Default $\omega_k = 0.0$ in dry simulator; halt dry-model evaluation if $\omega_k > 0.05$. | **No** (M5 guard only) |
| **Lap Times, Stint Lengths & Pit In/Out Timestamps** | $T_k, n_j, a_k, P_j$ ($\mathrm{s}, \mathrm{laps}$) | High ($100\%$ of post-2018 sessions via FOM live timing archive) | [FastF1 Laps Data](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) & [OpenF1 Stints/Pit Endpoints](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F) | `PUBLISHED_OBSERVED` | **High** ($\pm 0.001\,\mathrm{s}$); occasional missing tyre age on red-flag restarts or unrecorded pre-used sets in FP1–FP3. | Exclude anomalous/incomplete stints during offline calibration; never interpolate missing pit stops. | **Yes** (M0 calibration) |
| **Vehicle Fuel Load & Burn Rate** | $M_{\mathrm{fuel},0}$ ($\mathrm{kg}$), $\mu_{\mathrm{fuel}}$ ($\mathrm{kg\cdot lap}^{-1}$) | **Unavailable** publicly (teams do not publish exact starting fuel) | [FIA Technical Regulations](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110) ($110\,\mathrm{kg}$ cap) + Engineering Estimates | `ESTIMATED` / `ASSUMED` | **Medium** ($\pm 5\,\mathrm{kg}$ initial load); linear burn rate $\mu_{\mathrm{fuel}} \approx M_{\mathrm{fuel},0}/L$ is well-approximated under green-flag running. | Set $M_{\mathrm{fuel},0} = 100\,\mathrm{kg}$ (or track-scaled estimate) and $\gamma_{\mathrm{fuel}} = 0.032\,\mathrm{s\cdot kg}^{-1}$. | **Yes** (M0 confounder control) |
| **Physical Compound Wear & Degradation Curves** | $d_{0,c}, \alpha_c, w_{\mathrm{crit},c}, \beta_c$ | **Unavailable** directly (proprietary Pirelli/team tyre models) | Calibrated from [FastF1](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) fuel-corrected clean race stints | `CALIBRATED` | **Medium**; only the composite product $\alpha_c d_{0,c}$ ($\mathrm{s\cdot lap}^{-1}$) is directly observable from lap times without destructive physical tread depth gauges. | Anchor $d_{0,c}$ to inverse maximum stint length $1/L_{\max,c}$ so $h_k$ is identifiable on $[0,1]$. | **Yes** (M0 linear, M3 cliff) |
| **Internal Tyre Bulk & Surface Temperatures** | $\theta_{k,i}$ (${}^\circ\mathrm{C}$) | **Unavailable** publicly (infrared & TPMS telemetry encrypted by teams) | Synthetic/Literature Thermal Models | `UNKNOWN_UNAVAILABLE` / `ASSUMED` | **Low**; unobservable in public feeds, creating severe parameter non-identifiability if fitted solely to lap times. | Defer dynamic $\theta_{k,i}$ to Stage M4; use observable $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ in M0–M3. | **No** (M4 extension) |

---

## 6. Baseline Lap-Time Mathematical Model and Derivation

### 6.1 Additive Lap-Time Decomposition

For a vehicle $v$ competing on circuit/layout/surface version $r$ during race lap $k \in \{1, \ldots, L\}$, using physical tyre compound $c = m_e(r_j) \in \mathcal{C}$ during stint $j$, the predicted undisturbed racing lap time $T_k$ (in seconds) is decomposed into orthogonal physical contributions:

$$T_k = T_{\mathrm{ref}}(r, v) + \Delta T_{\mathrm{fuel},k}(r, v) + \Delta T_{\mathrm{evol},k}(r) + \Delta T_{\mathrm{start},k}(r, v) + \Delta T_{\mathrm{comp}}(c, r) + \Delta T_{\mathrm{tyre},k}(c, \mathbf{z}_k, \boldsymbol{\theta}_k, r, \mathbf{w}_k) + \Delta T_{\mathrm{in/out},k}(c, r) + \Delta T_{\mathrm{other},k}$$

* **Equation Reference:** Equation (6.1) — Master Flying-Lap Time Decomposition.
* **Purpose:** Predicts the elapsed time (in seconds) required to complete lap $k$ on the racing track, excluding the stationary service and pit-lane speed-limit traversal time loss $P_j$ (which is accounted for separately in Section 9 so pit losses are never double-counted).
* **Variables & Units:**
  * $T_k \in \mathbb{R}^+$ ($\mathrm{s}$): Total flying-lap time on lap $k$.
  * $T_{\mathrm{ref}}(r, v) \in \mathbb{R}^+$ ($\mathrm{s}$): Baseline reference lap time for vehicle $v$ on circuit version $r$ evaluated at reference fuel mass $M_{\mathrm{ref}} \equiv 50.0\,\mathrm{kg}$, reference physical compound $c_{\mathrm{ref}} \equiv C3$, brand-new tyre health ($\mathbf{h}_k = \mathbf{1}$), optimal thermal window ($\boldsymbol{\theta}_k = \boldsymbol{\theta}_{\mathrm{opt}}$), mature track evolution ($k \to \infty$), and reference environment $\mathbf{w}_{\mathrm{ref}}$.
  * $\Delta T_{\mathrm{fuel},k}(r, v) \in \mathbb{R}$ ($\mathrm{s}$): Reversible lap-time offset induced by instantaneous carried fuel mass $M_{\mathrm{fuel},k}$ relative to $M_{\mathrm{ref}}$.
  * $\Delta T_{\mathrm{evol},k}(r) \in \mathbb{R}_{\ge 0}$ ($\mathrm{s}$): Track surface evolution (green-track rubbering-in) penalty on lap $k$ relative to a fully rubbered-in racing line.
  * $\Delta T_{\mathrm{start},k}(r, v) \in \mathbb{R}_{\ge 0}$ ($\mathrm{s}$): Standing-start acceleration and shortened-straight penalty on lap $k=1$ ($\Delta T_{\mathrm{start},k} = 0$ for all $k \ge 2$).
  * $\Delta T_{\mathrm{comp}}(c, r) \in \mathbb{R}$ ($\mathrm{s}$): Intrinsic fresh-tyre pace offset of physical compound $c$ relative to $c_{\mathrm{ref}}$ on circuit $r$.
  * $\Delta T_{\mathrm{tyre},k} \in \mathbb{R}_{\ge 0}$ ($\mathrm{s}$): Lap-time degradation penalty resulting from evolved tyre condition state $\mathbf{z}_k$ and thermal state $\boldsymbol{\theta}_k$.
  * $\Delta T_{\mathrm{in/out},k}(c, r) \in \mathbb{R}_{\ge 0}$ ($\mathrm{s}$): Out-lap cold-tyre warm-up penalty on the first lap of a new stint ($m=1, j \ge 2$) not included in pit-lane travel time $P_{j-1}$.
  * $\Delta T_{\mathrm{other},k} \in \mathbb{R}$ ($\mathrm{s}$): Placeholder for exogenous perturbations (traffic, yellow flags, driver pace offsets); fixed identically to $0.0\,\mathrm{s}$ in the baseline deterministic free-air simulator.
* **Domain:** $k \in \{1, \ldots, L\}$, $T_k \in [40.0, 180.0]\,\mathrm{s}$ for standard Formula 1 dry circuits.
* **Assumptions:** Additivity (superposition) of first-order lap-time perturbations around the reference operating point $(M_{\mathrm{ref}}, c_{\mathrm{ref}}, \mathbf{w}_{\mathrm{ref}})$. Second-order cross-derivatives between fuel mass and instantaneous compound offset are negligible compared to stint-level wear effects (typically $< 0.015\,\mathrm{s\cdot lap}^{-1}$), whereas the coupling between fuel mass and cumulative tyre wear rate is explicitly preserved in the state transition via $F_{\mathrm{usage}}(k)$.
* **Parameters:** $T_{\mathrm{ref}}(r, v)$ (`CALIBRATED` per track/vehicle); component parameters defined in Equations (6.2)–(6.4).
* **Calibration:** Estimated by two-stage regression on filtered green-flag race laps in `HistoricalLapDataset` after subtracting known fuel-burn slopes $\gamma_{\mathrm{fuel}}$.
* **Validation:** Dimensional homogeneity check (every term on the right-hand side has exact dimension of time $[\mathrm{T}] = \mathrm{s}$); residual distribution check across lap index $k$ to confirm zero systematic trend after fuel and wear correction.
* **Limitations:** Linear superposition breaks down under extreme off-nominal regimes (e.g., sudden rain transition $\omega_k > 0.05$, severe aerodynamic damage, or safety-car bunching).

### 6.2 Fuel-Mass Trajectory and Reversible Mass Penalty

During a dry race without refuelling (prohibited under [FIA Formula 1 Sporting Regulations](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110)), vehicle mass decreases monotonically from lap $1$ to lap $L$. Evaluated at the midpoint of lap $k$, the instantaneous carried fuel mass $M_{\mathrm{fuel},k}$ (in $\mathrm{kg}$) and its direct lap-time offset $\Delta T_{\mathrm{fuel},k}$ (in $\mathrm{s}$) are:

$$M_{\mathrm{fuel},k} = \max\left(0.0,\; M_{\mathrm{fuel},0} - \mu_{\mathrm{fuel}}(r, v)\left(k - \frac{1}{2}\right)\right)$$

$$\Delta T_{\mathrm{fuel},k}(r, v) = \gamma_{\mathrm{fuel}}(r) \left(M_{\mathrm{fuel},k} - M_{\mathrm{ref}}\right)$$

* **Equation Reference:** Equations (6.2a) and (6.2b) — Mid-Lap Fuel Mass and Reversible Fuel Pace Offset.
* **Purpose:** Separates the guaranteed lap-time improvement from burning fuel ($\approx 0.030\text{--}0.038\,\mathrm{s\cdot kg}^{-1}$, or $\approx 0.05\text{--}0.07\,\mathrm{s\cdot lap}^{-1}$) from genuine tyre wear so that historical stint slowdowns are not severely underestimated during calibration.
* **Variables & Units:**
  * $M_{\mathrm{fuel},k} \in [0, 115]\,\mathrm{kg}$: Fuel mass carried at the midpoint of lap $k$.
  * $M_{\mathrm{fuel},0} \in [10, 110]\,\mathrm{kg}$: Starting fuel load at race start ($k=1$).
  * $\mu_{\mathrm{fuel}}(r, v) \in [1.2, 2.5]\,\mathrm{kg\cdot lap}^{-1}$: Mean fuel consumption rate per racing lap.
  * $\gamma_{\mathrm{fuel}}(r) \in [0.020, 0.045]\,\mathrm{s\cdot kg}^{-1}$: Circuit-specific fuel mass sensitivity coefficient, which scales approximately with lap length and braking/acceleration frequency: $\gamma_{\mathrm{fuel}}(r) = \gamma_{\mathrm{fuel,0}} \frac{L_{\mathrm{lap}}(r)}{5000\,\mathrm{m}}$.
  * $M_{\mathrm{ref}} \equiv 50.0\,\mathrm{kg}$: Declared reference fuel load centering $T_{\mathrm{ref}}(r, v)$ near mid-race weight.
* **Domain:** $k \in \{1, \ldots, L\}$, $M_{\mathrm{fuel},0} \ge L \cdot \mu_{\mathrm{fuel}}(r, v)$ for legal race completion without lift-and-coast fuel saving.
* **Assumptions:** Constant green-flag fuel burn rate $\mu_{\mathrm{fuel}}$ per lap and linear sensitivity $\gamma_{\mathrm{fuel}}$ over the $110\,\mathrm{kg}$ fuel window (a $100\,\mathrm{kg}$ change represents $\approx 11\%$ of total car + driver mass $M_{\mathrm{dry}} \approx 798\,\mathrm{kg}$, over which linear Taylor expansion of traction/cornering acceleration time is accurate to within $\pm 0.04\,\mathrm{s}$).
* **Parameters:** $M_{\mathrm{fuel},0}, \mu_{\mathrm{fuel}}$ (`ESTIMATED`); $\gamma_{\mathrm{fuel}}(r)$ (`CALIBRATED` or `DERIVED` from circuit length and traction index).
* **Calibration:** Because fuel burn and tyre age $a_k$ are collinear within a single uninterrupted stint ($k$ and $a_k$ both increment by $+1$ per lap), $\gamma_{\mathrm{fuel}}(r)$ cannot be identified from a single stint in isolation. Instead, $\gamma_{\mathrm{fuel}}(r)$ is identified by comparing fresh-tyre lap times ($a_k \in \{1, 2\}$) on identical compounds across early-race stints ($k \approx 2$, high fuel) versus late-race stints ($k \approx 45$, low fuel) after controlling for track evolution.
* **Validation:** Dimensional check: $[\mathrm{s\cdot kg}^{-1}] \cdot [\mathrm{kg}] = [\mathrm{s}]$. Multi-stint fresh-tyre intercept comparison across pit stops.
* **Limitations:** Does not capture driver-initiated "lift and coast" fuel-saving modes or reduced fuel burn during Safety Car laps (extension in Stage M5).

### 6.3 Track Surface Evolution and Standing-Start Penalty

As cars deposit tread elastomer onto the racing line and sweep away dust, circuit grip improves rapidly during early laps and asymptotically saturates. Combined with the standing-start penalty on lap $1$:

$$\Delta T_{\mathrm{evol},k}(r) = A_{\mathrm{evol}}(r) \exp\left(-\frac{k - 1}{\tau_{\mathrm{evol}}(r)}\right), \qquad \Delta T_{\mathrm{start},k}(r, v) = \Delta T_{\mathrm{start}}(r, v) \cdot \mathbb{I}(k = 1)$$

* **Equation Reference:** Equation (6.3) — Exponential Track Rubbering Evolution and Standing-Start Offset.
* **Purpose:** Prevents early-race green-track slowness ($k \in [1, 15]$) from biasing the calibration of first-stint tyre wear rates, and accounts for the standing start on lap $1$.
* **Variables & Units:**
  * $A_{\mathrm{evol}}(r) \in [0.0, 2.5]\,\mathrm{s}$: Initial green-track time penalty at $k = 1$ relative to the asymptotic rubbered-in state ($k \gg \tau_{\mathrm{evol}}$).
  * $\tau_{\mathrm{evol}}(r) \in [5.0, 60.0]\,\mathrm{laps}$: Characteristic e-folding lap scale of track rubbering.
  * $\Delta T_{\mathrm{start}}(r, v) \in [2.0, 6.0]\,\mathrm{s}$: Standing-start acceleration and grid-offset time loss on lap $1$.
  * $\mathbb{I}(k = 1) \in \{0, 1\}$: Dimensionless indicator function equal to $1$ if $k = 1$ and $0$ otherwise.
* **Domain:** $k \in \{1, \ldots, L\}$, $\tau_{\mathrm{evol}}(r) > 0$.
* **Assumptions:** Monotonic dry track evolution; rain washes the track clean and resets the evolution state (handled by the wet-weather guard $\omega_k > 0.05$). Note that for single-car strategy optimisation under fixed environmental conditions, $\Delta T_{\mathrm{evol},k}$ and $\Delta T_{\mathrm{start},k}$ are independent of the chosen strategy $\pi$ and therefore add a constant offset to $T_{\mathrm{race}}(\pi)$, leaving the optimal strategy $\pi^*$ invariant. However, both terms are **essential during offline calibration** to obtain unbiased estimates of compound wear parameters from real race data.
* **Parameters:** $A_{\mathrm{evol}}(r), \tau_{\mathrm{evol}}(r)$ (`CALIBRATED` / `ESTIMATED`); $\Delta T_{\mathrm{start}}(r, v)$ (`CALIBRATED` from median $T_1 - T_2$ clean race starts).
* **Calibration:** Fitted jointly across field-wide stints or constrained via practice-to-race progression priors.
* **Validation:** Verify $\lim_{k \to \infty} \Delta T_{\mathrm{evol},k} = 0\,\mathrm{s}$ and verify that argument $-(k-1)/\tau_{\mathrm{evol}}$ is strictly dimensionless ($[\mathrm{laps}]/[\mathrm{laps}]$).
* **Limitations:** In extremely hot sessions, progressive accumulation of off-line "marbles" or thermal grease can slightly slow the track late in a race.

### 6.4 Intrinsic Compound Pace Scaling Across Circuits

Because a $5.891\,\mathrm{km}$ lap at Silverstone exposes the car to cornering grip for longer than a $4.309\,\mathrm{km}$ lap at Interlagos, the intrinsic fresh-tyre pace offset $\Delta T_{\mathrm{comp}}(c, r)$ of physical compound $c \in \mathcal{C}$ relative to reference compound $c_{\mathrm{ref}} \equiv C3$ scales with lap length and circuit cornering fraction:

$$\Delta T_{\mathrm{comp}}(c, r) = \Delta T_{\mathrm{comp},0}(c) \cdot \left(\frac{L_{\mathrm{lap}}(r)}{L_{\mathrm{ref}}}\right) \cdot \psi_{\mathrm{grip}}(r)$$

* **Equation Reference:** Equation (6.4) — Circuit-Scaled Fresh-Compound Pace Offset.
* **Purpose:** Maps the catalogue pace offset $\Delta T_{\mathrm{comp},0}(c)$ (defined at reference lap length $L_{\mathrm{ref}} \equiv 5000\,\mathrm{m}$) to the specific circuit $r$.
* **Variables & Units:**
  * $\Delta T_{\mathrm{comp},0}(c) \in [-2.0, +2.0]\,\mathrm{s}$: Catalogue fresh-tyre pace offset of physical compound $c$ at $L_{\mathrm{ref}} = 5000\,\mathrm{m}$, with $\Delta T_{\mathrm{comp},0}(C3) \equiv 0.0\,\mathrm{s}$ by definition, $\Delta T_{\mathrm{comp},0}(C1) > \Delta T_{\mathrm{comp},0}(C2) > 0 > \Delta T_{\mathrm{comp},0}(C4) > \Delta T_{\mathrm{comp},0}(C5)$.
  * $L_{\mathrm{lap}}(r) \in [3000, 8000]\,\mathrm{m}$: Official circuit lap length.
  * $L_{\mathrm{ref}} \equiv 5000.0\,\mathrm{m}$: Reference normalisation lap length.
  * $\psi_{\mathrm{grip}}(r) \in [0.7, 1.4]$ ($\text{dimensionless}$): Circuit mechanical grip sensitivity factor (equal to $1.0$ in Stage M0, or calibrated per circuit in Stage M1).
* **Domain:** $c \in \{C1, C2, C3, C4, C5\}$.
* **Assumptions:** Monotonic pace ordering from hardest ($C1$, slowest fresh pace, highest $\Delta T_{\mathrm{comp},0}$) to softest ($C5$, fastest fresh pace, most negative $\Delta T_{\mathrm{comp},0}$) when evaluated within each compound's operating temperature window.
* **Parameters:** $\Delta T_{\mathrm{comp},0}(c)$ (`CALIBRATED` in `PhysicalCompoundCatalogue`); $\psi_{\mathrm{grip}}(r)$ (`CALIBRATED` or `ASSUMED` $= 1.0$).
* **Calibration:** Estimated from fresh-tyre stint intercepts ($a_k \le 3$) across events where compound pairs $(c_a, c_b)$ co-occur.
* **Validation:** Dimensional check: $[\mathrm{s}] \cdot ([\mathrm{m}]/[\mathrm{m}]) \cdot [1] = [\mathrm{s}]$. Order constraint check: $\Delta T_{\mathrm{comp},0}(C1) > \Delta T_{\mathrm{comp},0}(C2) > \Delta T_{\mathrm{comp},0}(C3) = 0 > \Delta T_{\mathrm{comp},0}(C4) > \Delta T_{\mathrm{comp},0}(C5)$.
* **Limitations:** Very soft compounds ($C5$) on high-energy tracks can experience single-lap overheating in Sector 3, reducing their fresh-tyre pace advantage relative to linear length scaling.

---

## 7. Physical Compound Catalogue, Event Role Mapping, and Tyre-State Model

### 7.1 Distinction Between Physical Compounds and Event Roles

In modern Formula 1 (under the system introduced by [Pirelli Motorsport](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1) and explained in the [Formula 1 Tyre Guide](https://www.google.com/search?q=https%3A%2F%2Fwww.formula1.com%2Fen%2Flatest%2Farticle%2Fthe-beginners-guide-to-formula-1-tyres.61SvF0Kfg29UR2SPhakDqd)), the dry slick tyre catalogue consists of five physical rubber formulations ordered by decreasing shore hardness and increasing grip:

$$\mathcal{C} = \{C1, C2, C3, C4, C5\}, \qquad \text{with hardness rank } \rho(C1) = 1 < \rho(C2) = 2 < \rho(C3) = 3 < \rho(C4) = 4 < \rho(C5) = 5$$

At each Grand Prix event $e$, only three compounds from $\mathcal{C}$ are nominated for race weekend allocation, forming the event nominated subset $\mathcal{N}_e \subset \mathcal{C}$ with cardinality $\lvert\mathcal{N}_e\rvert = 3$. Trackside and in strategy graphics, those three nominated compounds are assigned to the event-level role set:

$$\mathcal{R} = \{S, M, H\} \equiv \{\mathrm{Soft}, \mathrm{Medium}, \mathrm{Hard}\}$$

Crucially, **Soft, Medium, and Hard are relative event roles, not physical compounds.** A compound designated as `Soft` ($S$) at a high-energy circuit such as Silverstone (`C3`) may be designated as `Hard` ($H$) at a low-energy street circuit such as Monaco or Montreal (`C3`). Consequently, all physical wear, pace, and thermal parameters reside exclusively in `PhysicalCompoundCatalogue` keyed by $c \in \mathcal{C}$, while the strategy enumerator and regulatory rule checker operate over $\mathcal{R}$ and resolve roles via $c = m_e(r)$ prior to evaluating any lap or stint equation.

### 7.2 Formal Event Role Mapping and Pre-Simulation Validation Rules

For any event $e$ with nominated subset $\mathcal{N}_e \subset \mathcal{C}$, the event role mapping is a function:

$$m_e : \mathcal{R} \hookrightarrow \mathcal{C}$$

Before the simulator executes a single lap calculation, the C++ configuration validator must enforce four mathematical invariants on $(m_e, \mathcal{N}_e)$:

1. **Domain Completeness & Codomain Validity:** Every role $r \in \{S, M, H\}$ is mapped to a defined element of the active season's `PhysicalCompoundCatalogue`:
   $$\forall r \in \mathcal{R}, \quad m_e(r) \in \mathcal{C}$$
2. **Injectivity (Distinctness of Assigned Compounds):** No two roles may point to the same physical compound:
   $$\forall r_a, r_b \in \mathcal{R}, \quad r_a \neq r_b \implies m_e(r_a) \neq m_e(r_b)$$
3. **Surjectivity onto the Nominated Subset (Bijection with $\mathcal{N}_e$):** The image of $\mathcal{R}$ under $m_e$ must equal the declared three-compound nominated set $\mathcal{N}_e$:
   $$m_e(\mathcal{R}) = \{m_e(S), m_e(M), m_e(H)\} = \mathcal{N}_e \quad \text{with } \lvert\mathcal{N}_e\rvert = 3$$
4. **Hardness Order Consistency (Standard Role Ordering):** For official FIA events (`is_hypothetical_override == false`), the hardness rank $\rho : \mathcal{C} \to \{1, 2, 3, 4, 5\}$ must strictly increase from Hard to Medium to Soft:
   $$\rho(m_e(H)) < \rho(m_e(M)) < \rho(m_e(S))$$
   *(Note: Non-contiguous nominations such as $\mathcal{N}_e = \{C2, C4, C5\}$ or $\{C1, C3, C4\}$ satisfy all four invariants without requiring the indices to be consecutive integers.)*

#### Labelled Hypothetical Illustration of Role Mapping Validation

Consider two contrasting **hypothetical event configurations** constructed to illustrate the validation rules:

* **Valid Event Configuration $e_1$ (Contiguous Mid-Range Nomination):**
  * Declared nominated set: $\mathcal{N}_{e_1} = \{C2, C3, C4\}$.
  * Configured mapping: $m_{e_1}(H) = C2$, $m_{e_1}(M) = C3$, $m_{e_1}(S) = C4$, with $C1, C5 \notin \mathcal{N}_{e_1}$.
  * Validation check: $|\mathcal{N}_{e_1}| = 3$; all three images are distinct; $\{C4, C3, C2\} = \mathcal{N}_{e_1}$; hardness ranks satisfy $\rho(C2)=2 < \rho(C3)=3 < \rho(C4)=4$. **Result: PASS.**
* **Valid Event Configuration $e_2$ (Step-Skip / Non-Contiguous Nomination):**
  * Declared nominated set: $\mathcal{N}_{e_2} = \{C2, C4, C5\}$.
  * Configured mapping: $m_{e_2}(H) = C2$, $m_{e_2}(M) = C4$, $m_{e_2}(S) = C5$, with $C1, C3 \notin \mathcal{N}_{e_2}$.
  * Validation check: $|\mathcal{N}_{e_2}| = 3$; distinct images; $\{C5, C4, C2\} = \mathcal{N}_{e_2}$; hardness ranks satisfy $\rho(C2)=2 < \rho(C4)=4 < \rho(C5)=5$. **Result: PASS.**
* **Invalid Configuration $e_3$ (Rejected by Validator):**
  * Declared nominated set: $\mathcal{N}_{e_3} = \{C1, C2, C3\}$, but user configures $m_{e_3}(H) = C1, m_{e_3}(M) = C2, m_{e_3}(S) = C4$.
  * Validation check: $m_{e_3}(S) = C4 \notin \mathcal{N}_{e_3}$. **Result: REJECT (`ERR_ROLE_MAPPING_MISMATCH_NOMINATED_SET`).**

### 7.3 Tyre Condition State Vector and Four-Corner Architecture

To prevent contradictory sign conventions, the simulator standardizes on **Normalized Tyre Health** $h_{k,i} \in [0, 1]$ as the primary internal state variable for corner $i \in \{\mathrm{FL}, \mathrm{FR}, \mathrm{RL}, \mathrm{RR}\}$ at the start of race lap $k$, and defines **Normalized Tyre Wear** $w_{k,i} \equiv 1 - h_{k,i} \in [0, 1]$ strictly as its exact algebraic complement:

$$\mathbf{h}_k = \begin{bmatrix} h_{k,\mathrm{FL}} \\ h_{k,\mathrm{FR}} \\ h_{k,\mathrm{RL}} \\ h_{k,\mathrm{RR}} \end{bmatrix} \in [0, 1]^4, \qquad w_{k,i} \equiv 1 - h_{k,i} \quad \forall i \in \{\mathrm{FL}, \mathrm{FR}, \mathrm{RL}, \mathrm{RR}\}$$

* **Boundary Semantics:**
  * $h_{k,i} = 1.0 \iff w_{k,i} = 0.0$: Brand-new, un-scrubbed tyre set at the start of its first lap.
  * $h_{k,i} = 0.0 \iff w_{k,i} = 1.0$: Tyre corner $i$ has reached its terminal usable tread/structural limit (structural canvas exposure or catastrophic delamination risk). Any strategy where $\min_i h_{k,i} < h_{\min}$ (default $h_{\min} = 0.05$) is flagged as physically infeasible.
* **Aggregate Set Simplification (Stages M0–M2) vs. Four-Corner Asymmetric Model (Stage M3):**
  Because public timing feeds ([FastF1](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) / [OpenF1](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F)) report only a single lap time $T_k$ per car rather than four independent wheel-force transducer channels, four separate corner wear rates cannot be uniquely identified from lap times alone without circuit geometry priors. Therefore:
  * In **Stages M0–M2**, all four corners evolve identically ($\lambda_{r,i} = 1.0$, so $h_{k,\mathrm{FL}} = h_{k,\mathrm{FR}} = h_{k,\mathrm{RL}} = h_{k,\mathrm{RR}} \equiv h_k$), and the state reduces to a single scalar set health $h_k \in [0, 1]$.
  * In **Stage M3**, asymmetric circuit loading differentiates the four corners via dimensionless corner stress multipliers $\lambda_{r,i}$ derived from left/right corner counts ($N_L, N_R$) and front/rear braking vs. traction energy shares, normalized such that the mean across the four corners equals unity: $\frac{1}{4}\sum_{i} \lambda_{r,i} = 1.0$.

### 7.4 Static Track Baseline Stress and Tyre Health State Transition

Across lap $k$, the health $h_{k,i}$ of corner $i$ on physical compound $c = m_e(r_j)$ transitions to $h_{k+1,i}$ at the start of lap $k+1$ (within the same stint) via the bounded recurrence:

$$h_{k+1,i} = \max\Big(0.0,\; h_{k,i} - \Delta h_{k,i}(c, r, \mathbf{w}_k, k)\Big)$$

$$\Delta h_{k,i}(c, r, \mathbf{w}_k, k) = D_{\mathrm{base}}(c, r, i) \cdot F_{\mathrm{env}}(c, \mathbf{w}_k) \cdot F_{\mathrm{usage}}(k, a_k)$$

where the static circuit-and-compound baseline wear rate $D_{\mathrm{base}}(c, r, i)$ (in $\mathrm{lap}^{-1}$) is given by:

$$D_{\mathrm{base}}(c, r, i) = d_{0,c} \cdot \left(\frac{L_{\mathrm{lap}}(r)}{L_{\mathrm{ref}}}\right) \cdot S_{\mathrm{str}}(r, i) \cdot \zeta_v$$

and the dimensionless circuit stress index $S_{\mathrm{str}}(r, i)$ is constructed without double-counting from the orthogonal lateral, braking, and traction energy exposures weighted by surface abrasiveness:

$$S_{\mathrm{str}}(r, i) = \lambda_{r,i} \cdot \left(1 + \omega_{\mathrm{abr}}\left(\frac{A_{\mathrm{surf}}(r) - 3.0}{2.0}\right)\right) \cdot \Big(\nu_{\mathrm{lat},i} E_{\mathrm{lat}}(r) + \nu_{\mathrm{brk},i} E_{\mathrm{brk}}(r) + \nu_{\mathrm{trc},i} E_{\mathrm{trc}}(r)\Big)$$

* **Equation Reference:** Equations (7.1)–(7.4) — Tyre Health State Transition and Static Circuit Stress Decomposition.
* **Purpose:** Updates remaining tyre health $h_{k,i}$ after each lap by combining the compound's intrinsic wear rate $d_{0,c}$ with circuit length, static circuit mechanical/surface stress $S_{\mathrm{str}}(r, i)$, dynamic environmental multiplier $F_{\mathrm{env}}$, and vehicle load multiplier $F_{\mathrm{usage}}$.
* **Variables & Units:**
  * $h_{k,i}, h_{k+1,i} \in [0, 1]$ ($\text{dimensionless}$): Normalized tyre health at the start of lap $k$ and lap $k+1$.
  * $\Delta h_{k,i} \in [0, 1]\,\mathrm{lap}^{-1}$: Health depleted during lap $k$.
  * $d_{0,c} \in [0.005, 0.080]\,\mathrm{lap}^{-1}$: Catalogue baseline wear rate of physical compound $c$ per $5000\,\mathrm{m}$ reference lap under neutral stress ($S_{\mathrm{str}} = 1, F_{\mathrm{env}} = 1, F_{\mathrm{usage}} = 1$).
  * $S_{\mathrm{str}}(r, i) \in [0.4, 2.5]$ ($\text{dimensionless}$): Static circuit stress index for circuit $r$ and corner $i$.
  * $\lambda_{r,i} \in [0.6, 1.4]$ ($\text{dimensionless}$): Corner asymmetry factor ($\lambda_{r,i} \equiv 1.0$ in aggregate M0–M2; in M3, outside tyres on predominantly clockwise tracks where $N_R > N_L$ carry $\lambda_{r,\mathrm{FL}}, \lambda_{r,\mathrm{RL}} > 1$).
  * $A_{\mathrm{surf}}(r) \in [1.0, 5.0]$ ($\text{dimensionless}$): Surface abrasiveness score ($3.0$ is neutral reference).
  * $\omega_{\mathrm{abr}} \in [0.0, 0.5]$ ($\text{dimensionless}$): Calibrated sensitivity of wear rate to surface abrasiveness score deviation.
  * $E_{\mathrm{lat}}(r), E_{\mathrm{brk}}(r), E_{\mathrm{trc}}(r) \in [0.4, 2.0]$ ($\text{dimensionless}$): Normalized lateral, braking, and traction energy exposure indices (each equal to $1.0$ on a reference average circuit).
  * $\nu_{\mathrm{lat},i}, \nu_{\mathrm{brk},i}, \nu_{\mathrm{trc},i} \in [0, 1]$ with $\nu_{\mathrm{lat},i} + \nu_{\mathrm{brk},i} + \nu_{\mathrm{trc},i} = 1$: Convex weights partitioning lateral, braking, and traction work (for aggregate model: $\nu_{\mathrm{lat}} = 0.50, \nu_{\mathrm{brk}} = 0.25, \nu_{\mathrm{trc}} = 0.25$; in M3, front axle has higher $\nu_{\mathrm{brk}}$ and rear axle has higher $\nu_{\mathrm{trc}}$).
  * $\zeta_v \in [0.85, 1.15]$ ($\text{dimensionless}$): Vehicle-specific tyre energy management factor.
* **Usage / Load Multiplier $F_{\mathrm{usage}}(k, a_k)$:** Accounts for the increased vertical tyre load when the car is heavy with fuel relative to reference fuel mass $M_{\mathrm{ref}}$:
  $$F_{\mathrm{usage}}(k) = 1 + \kappa_{\mathrm{load}} \left(\frac{M_{\mathrm{fuel},k} - M_{\mathrm{ref}}}{M_{\mathrm{dry}} + M_{\mathrm{ref}}}\right)$$
  where $\kappa_{\mathrm{load}} \in [0.0, 1.5]$ ($\text{dimensionless}$; default $0.8$) is the load-wear sensitivity exponent. At $M_{\mathrm{fuel},k} = M_{\mathrm{ref}}$, $F_{\mathrm{usage}}(k) = 1.0$.
* **Domain:** $h_{k,i} \in [0, 1]$, $\Delta h_{k,i} \ge 0$ (tyre health cannot spontaneously regenerate during a stint).
* **Assumptions:** Separability of baseline track stress $D_{\mathrm{base}}$, environmental multiplier $F_{\mathrm{env}}$, and mass-load multiplier $F_{\mathrm{usage}}$. Why multiplicative rather than additive? Physical archival wear Laws (such as Archard's wear equation and Grosch's viscoelastic elastomer abrasion theory) express rubber volume loss per sliding distance as proportional to normal contact pressure multiplied by the temperature-dependent friction/hysteresis factor; if baseline sliding work is zero ($D_{\mathrm{base}} = 0$), elevated track temperature alone produces zero mechanical abrasion. An additive environmental term would unphysically predict wear at zero distance or independent of compound softness $d_{0,c}$.
* **Calibration & Identifiability Anchor:** Because physical tread depth is unobservable in public timing data, $d_{0,c}$ is anchored so that $h = 0$ corresponds to the empirical maximum safe stint length $L_{\max,c}^{\mathrm{ref}}$ on a reference $5000\,\mathrm{m}$ circuit ($d_{0,c} \equiv 1 / L_{\max,c}^{\mathrm{ref}}$, e.g., $L_{\max,C1}^{\mathrm{ref}} \approx 62.5\,\mathrm{laps} \implies d_{0,C1} = 0.016\,\mathrm{lap}^{-1}$). Once $d_{0,c}$ is anchored to $[0, 1]$ via $L_{\max,c}^{\mathrm{ref}}$, the time-loss conversion coefficient $\alpha_c$ (in $\mathrm{s}$) in Equation (7.6) is uniquely identifiable from observed lap-time slopes!
* **Validation:** Dimensional check: $[\mathrm{lap}^{-1}] \cdot ([\mathrm{m}]/[\mathrm{m}]) \cdot [1] \cdot [1] \cdot [1] = [\mathrm{lap}^{-1}]$. Verify $S_{\mathrm{str}}(r_{\mathrm{ref}}, i) = 1.0$ when $A_{\mathrm{surf}} = 3.0$ and $E_{\mathrm{lat}} = E_{\mathrm{brk}} = E_{\mathrm{trc}} = 1.0$.
* **Limitations:** Assumes constant driver push level ($100\%$ pace management neutral); deliberate driver tyre management ("pace management") requires an active control input $u_k$ (Stage M5).

### 7.5 Mapping Tyre State to Lap-Time Loss (Separating Health from Pace Penalty)

A core modeling requirement is that **normalized health loss $w_{k,i} = 1 - h_{k,i}$ is a dimensionless state variable, NOT a percentage or direct number of seconds of lap-time loss.** Translating the four-corner wear vector $\mathbf{w}_k = \mathbf{1} - \mathbf{h}_k \in [0, 1]^4$ into the lap-time penalty $\Delta T_{\mathrm{tyre},k}$ (in seconds) requires an explicit, calibrated response function $g(c, \mathbf{z}_k, \boldsymbol{\theta}_k, r)$.

First, the four corner wear states are aggregated into an effective set wear state $\bar{w}_k \in [0, 1]$ using a convex combination of mean wear and the most-degraded (limiting) axle/corner wear:

$$\bar{w}_k = (1 - \xi_{\mathrm{lim}}) \left(\frac{1}{4}\sum_{i \in \{\mathrm{FL,FR,RL,RR}\}} w_{k,i}\right) + \xi_{\mathrm{lim}} \max_{i \in \{\mathrm{FL,FR,RL,RR}\}} w_{k,i}$$

where $\xi_{\mathrm{lim}} \in [0, 1]$ (default $\xi_{\mathrm{lim}} = 0.5$ in Stage M3; in aggregate Stages M0–M2, all $w_{k,i}$ are equal so $\bar{w}_k = w_k = 1 - h_k$ for any $\xi_{\mathrm{lim}}$).

Second, two candidate functional forms are compared for mapping effective wear $\bar{w}_k$ to lap-time loss $\Delta T_{\mathrm{tyre},k}$:

1. **Candidate Form 1 (Linear Baseline — Stages M0–M2):**
   $$\Delta T_{\mathrm{tyre},k}^{\mathrm{lin}}(c, \bar{w}_k, r) = \alpha_c \cdot \left(\frac{L_{\mathrm{lap}}(r)}{L_{\mathrm{ref}}}\right) \cdot \bar{w}_k$$
2. **Candidate Form 2 (Piecewise Linear-Plus-Power-Law "Cliff" Model — Stage M3):**
   $$\Delta T_{\mathrm{tyre},k}(c, \bar{w}_k, \boldsymbol{\theta}_k, r) = \left(\frac{L_{\mathrm{lap}}(r)}{L_{\mathrm{ref}}}\right) \left[ \alpha_c \bar{w}_k + \beta_c \left(\max\left(0.0,\; \frac{\bar{w}_k - w_{\mathrm{crit},c}}{1 - w_{\mathrm{crit},c}}\right)\right)^{\eta_c} \right] + \Delta T_{\mathrm{therm}}(\boldsymbol{\theta}_k, c)$$

* **Equation Reference:** Equations (7.5) and (7.6) — Effective Wear Aggregation and Piecewise Linear-Plus-Cliff Lap-Time Loss Mapping.
* **Purpose:** Converts dimensionless tyre wear $\bar{w}_k \in [0, 1]$ into a lap-time loss $\Delta T_{\mathrm{tyre},k}$ in seconds, capturing both steady linear grip decay ($\alpha_c \bar{w}_k$) during the main stint phase and the rapid nonlinear "tyre cliff" ($\beta_c (\dots)^{\eta_c}$) that occurs when tread thickness drops below critical threshold $w_{\mathrm{crit},c}$ ($h_k < 1 - w_{\mathrm{crit},c}$).
* **Justification of Piecewise Linear-Plus-Cliff Form:** Purely polynomial (e.g., globally quadratic) degradation curves distort early-stint slopes whenever late-stint cliff laps are present in the regression sample. By contrast, Equation (7.6) is strictly linear with slope $\alpha_c (L_{\mathrm{lap}}/L_{\mathrm{ref}})$ on $\bar{w}_k \in [0, w_{\mathrm{crit},c}]$, continuously differentiable ($C^1$ smooth) at the threshold $\bar{w}_k = w_{\mathrm{crit},c}$ for all $\eta_c > 1$, and reduces exactly to Candidate Form 1 when $\beta_c = 0$ or $w_{\mathrm{crit},c} = 1.0$.
* **Variables & Units:**
  * $\bar{w}_k \in [0, 1]$ ($\text{dimensionless}$): Effective normalized tyre wear at the start of lap $k$.
  * $\alpha_c \in [0.5, 8.0]\,\mathrm{s}$: Linear time-loss conversion coefficient for compound $c$ at reference length $L_{\mathrm{ref}} = 5000\,\mathrm{m}$ (seconds per unit of normalized wear $\bar{w}$). Note that on a reference circuit under neutral conditions where $\Delta w_k = d_{0,c}$, the early-stint per-lap degradation slope in seconds per lap is $\frac{\mathrm{d}\Delta T_{\mathrm{tyre}}}{\mathrm{d}k} = \alpha_c d_{0,c}$ (typically $0.03\text{--}0.15\,\mathrm{s\cdot lap}^{-1}$).
  * $w_{\mathrm{crit},c} \in [0.50, 1.00)$ ($\text{dimensionless}$): Critical wear fraction at which the nonlinear cliff begins (equivalent to critical health $h_{\mathrm{crit},c} = 1 - w_{\mathrm{crit},c}$, e.g., $w_{\mathrm{crit},c} = 0.75 \iff h_{\mathrm{crit},c} = 0.25$).
  * $\beta_c \in [0.0, 10.0]\,\mathrm{s}$: Additional seconds per lap lost at terminal wear $\bar{w}_k = 1.0$ beyond the linear projection.
  * $\eta_c \in [1.5, 4.0]$ ($\text{dimensionless}$): Cliff curvature exponent ($\eta_c > 1$ ensures zero derivative jump at $\bar{w}_k = w_{\mathrm{crit},c}$).
  * $\Delta T_{\mathrm{therm}}(\boldsymbol{\theta}_k, c) \in \mathbb{R}_{\ge 0}$ ($\mathrm{s}$): Optional Stage M4 thermal window penalty ( identically $0.0\,\mathrm{s}$ in Stages M0–M3).
* **Domain:** $\bar{w}_k \in [0, 1]$, $w_{\mathrm{crit},c} \in (0, 1)$.
* **Calibration:** In Stages M0–M2 ($\beta_c = 0$), $\alpha_c$ is fitted via Huber robust linear regression on fuel- and evolution-corrected stint lap times against cumulative predicted wear $\bar{w}_k$. In Stage M3, $(w_{\mathrm{crit},c}, \beta_c, \eta_c)$ are fitted only on stints exceeding $70\%$ of $L_{\max,c}$ using bounded nonlinear least squares with strong priors ($\eta_c = 2.0$ fixed if sample size is small).
* **Validation:**
  1. Boundary check at new tyre ($\bar{w}_k = 0 \iff h_k = 1$): $\Delta T_{\mathrm{tyre},k}(c, 0, \boldsymbol{\theta}_{\mathrm{opt}}, r) = 0.0\,\mathrm{s}$.
  2. Continuity of first derivative at $\bar{w}_k = w_{\mathrm{crit},c}$: $\lim_{\bar{w}\to w_{\mathrm{crit}}^-} \frac{\partial \Delta T_{\mathrm{tyre}}}{\partial \bar{w}} = \lim_{\bar{w}\to w_{\mathrm{crit}}^+} \frac{\partial \Delta T_{\mathrm{tyre}}}{\partial \bar{w}} = \alpha_c \frac{L_{\mathrm{lap}}}{L_{\mathrm{ref}}}$.
  3. Dimensional check: $[\mathrm{s}] \cdot [1] = [\mathrm{s}]$.
* **Limitations:** Does not explicitly model temporary early-stint "graining" (where lap times degrade over laps $4\text{--}10$ and then partially recover as grained rubber cleans off).

### 7.6 Staged Tyre Thermal State Model (Stage M4 Extension)

In Stages M0–M3, internal tyre temperature $\boldsymbol{\theta}_k$ is not evolved as a dynamic state variable because public timing data lacks tyre temperature sensors; instead, observable track and air temperatures enter through the empirical degradation multiplier $F_{\mathrm{env}}$ (Section 8). To ensure the schema and mathematical architecture support a full thermal upgrade without redesign, Stage M4 defines a discrete lap-to-lap bulk-surface thermal recurrence for each corner $i$:

$$\theta_{k+1,i} = \theta_{k,i} + \Delta t_{\mathrm{lap}} \left[ \dot{Q}_{\mathrm{gen}}(c, r, i, v) - h_{\mathrm{cond}}(r) \left(\theta_{k,i} - T_{\mathrm{trk},k}\right) - h_{\mathrm{conv}}\left(v_{\mathrm{car}}, v_{\mathrm{wind},k}\right) \left(\theta_{k,i} - T_{\mathrm{air},k}\right) \right]$$

$$\Delta T_{\mathrm{therm}}(\boldsymbol{\theta}_k, c) = \frac{1}{4}\sum_{i} \Big[ \gamma_{\mathrm{cold},c} \max\left(0.0,\; \theta_{\min,c} - \theta_{k,i}\right)^2 + \gamma_{\mathrm{hot},c} \max\left(0.0,\; \theta_{k,i} - \theta_{\max,c}\right)^2 \Big]$$

* **Equation Reference:** Equations (7.7) and (7.8) — Stage M4 Lumped-Parameter Tyre Thermal Recurrence and Operating-Window Pace Penalty.
* **Purpose:** Tracks effective tyre temperature $\theta_{k,i}$ (${}^\circ\mathrm{C}$) separately from both track temperature $T_{\mathrm{trk},k}$ and air temperature $T_{\mathrm{air},k}$, penalising lap time quadratically when $\theta_{k,i}$ falls below the compound's working window $[\theta_{\min,c}, \theta_{\max,c}]$ (cold-tyre under-grip/graining) or exceeds it (thermal blistering/overheating).
* **Variables & Units:** $\theta_{k,i}$ (${}^\circ\mathrm{C}$); nominal lap duration $\Delta t_{\mathrm{lap}} \approx T_{\mathrm{ref}}$ ($\mathrm{s}$); viscoelastic sliding/flexing heating rate $\dot{Q}_{\mathrm{gen}}$ (${}^\circ\mathrm{C\cdot s}^{-1}$); conductive contact-patch transfer coefficient $h_{\mathrm{cond}}$ ($\mathrm{s}^{-1}$); convective airflow cooling coefficient $h_{\mathrm{conv}}$ ($\mathrm{s}^{-1}$); operating window bounds $[\theta_{\min,c}, \theta_{\max,c}]$ (${}^\circ\mathrm{C}$); penalty curvatures $\gamma_{\mathrm{cold},c}, \gamma_{\mathrm{hot},c}$ ($\mathrm{s\cdot {}^\circ C}^{-2}$).
* **Observability Warning:** Without infrared tyre camera or TPMS data, $(\dot{Q}_{\mathrm{gen}}, h_{\mathrm{cond}}, h_{\mathrm{conv}})$ are **not simultaneously identifiable** from lap times alone. Therefore, Stage M4 must only be activated when synthetic sensitivity exploration is requested or auxiliary thermal telemetry is supplied.

---

## 8. Dynamic Environmental Model, Reference Conditions, and Humidity Treatment

### 8.1 Dynamic Environmental Degradation Multiplier $F_{\mathrm{env}}$

Environmental conditions on lap $k$ are represented by the state vector $\mathbf{w}_k = (T_{\mathrm{trk},k}, T_{\mathrm{air},k}, \phi_k, \omega_k, v_{\mathrm{wind},k}, I_{\mathrm{sol},k})^\top$. To combine static circuit baseline degradation $D_{\mathrm{base}}(c, r, i)$ with dynamic race-day weather without double-counting, the specification defines a declared **Reference Environment** $\mathbf{w}_{\mathrm{ref}}$:

$$T_{\mathrm{trk,ref}} \equiv 35.0\,{}^\circ\mathrm{C}, \qquad T_{\mathrm{air,ref}} \equiv 25.0\,{}^\circ\mathrm{C}, \qquad \phi_{\mathrm{ref}} \equiv 50.0\%, \qquad \omega_{\mathrm{ref}} \equiv 0.0$$

At $\mathbf{w}_k = \mathbf{w}_{\mathrm{ref}}$, the dimensionless environmental degradation multiplier $F_{\mathrm{env}}(c, \mathbf{w}_k)$ must equal $1.0$ identically. Comparing linear-additive vs. exponential-multiplicative candidate forms, the model adopts a **bounded log-linear (exponential) or clipped first-order Taylor multiplier** around $\mathbf{w}_{\mathrm{ref}}$:

$$F_{\mathrm{env}}(c, \mathbf{w}_k) = \operatorname{clip}\!\left(\exp\!\Big[ \kappa_{\mathrm{trk},c} \left(T_{\mathrm{trk},k} - T_{\mathrm{trk,ref}}\right) + \kappa_{\mathrm{air},c} \left(T_{\mathrm{air},k} - T_{\mathrm{air,ref}}\right) + \psi_{\mathrm{hum},c} \left(\frac{\phi_k - \phi_{\mathrm{ref}}}{100\%}\right) \Big],\; F_{\min},\; F_{\max}\right)$$

For small temperature excursions ($|\kappa_{\mathrm{trk},c} \Delta T_{\mathrm{trk}}| \ll 1$), its first-order linearised form (used in Stage M0–M2 hand calculations) is:

$$F_{\mathrm{env}}^{\mathrm{lin}}(c, \mathbf{w}_k) = \operatorname{clip}\!\left(1.0 + \kappa_{\mathrm{trk},c} \left(T_{\mathrm{trk},k} - T_{\mathrm{trk,ref}}\right) + \kappa_{\mathrm{air},c} \left(T_{\mathrm{air},k} - T_{\mathrm{air,ref}}\right) + \psi_{\mathrm{hum},c} \left(\frac{\phi_k - \phi_{\mathrm{ref}}}{100\%}\right),\; F_{\min},\; F_{\max}\right)$$

* **Equation Reference:** Equations (8.1a) and (8.1b) — Dynamic Environmental Degradation Multiplier (Exponential and First-Order Linearised Forms).
* **Purpose:** Modulates the per-lap tyre wear rate $\Delta h_{k,i}$ in response to constant or lap-varying track temperature $T_{\mathrm{trk},k}$, ambient air temperature $T_{\mathrm{air},k}$, and relative humidity $\phi_k$.
* **Variables & Units:**
  * $F_{\mathrm{env}}(c, \mathbf{w}_k) \in [F_{\min}, F_{\max}]$ ($\text{dimensionless}$, with safety bounds $F_{\min} \equiv 0.50, F_{\max} \equiv 2.50$).
  * $T_{\mathrm{trk},k}, T_{\mathrm{trk,ref}}$ (${}^\circ\mathrm{C}$): Current and reference ($35.0\,{}^\circ\mathrm{C}$) track surface temperature.
  * $T_{\mathrm{air},k}, T_{\mathrm{air,ref}}$ (${}^\circ\mathrm{C}$): Current and reference ($25.0\,{}^\circ\mathrm{C}$) ambient air temperature.
  * $\phi_k, \phi_{\mathrm{ref}}$ ($\%$): Current and reference ($50.0\%$) relative humidity.
  * $\kappa_{\mathrm{trk},c} \in [0.0, 0.05]\,{}^\circ\mathrm{C}^{-1}$: Compound-specific track temperature wear sensitivity (softer compounds $C4, C5$ exhibit larger $\kappa_{\mathrm{trk},c}$ than harder compounds $C1, C2$ due to lower glass-transition and thermal-softening thresholds).
  * $\kappa_{\mathrm{air},c} \in [-0.02, 0.03]\,{}^\circ\mathrm{C}^{-1}$: Ambient air convective cooling sensitivity.
  * $\psi_{\mathrm{hum},c} \in \mathbb{R}$ ($\text{dimensionless}$): Direct relative-humidity wear sensitivity coefficient (**fixed strictly to $\psi_{\mathrm{hum},c} \equiv 0.0$ in baseline**; see Section 8.2).
* **Domain:** Dry racing regime where $\omega_k \le 0.05$, $T_{\mathrm{trk},k} \in [10, 65]\,{}^\circ\mathrm{C}$, $T_{\mathrm{air},k} \in [5, 45]\,{}^\circ\mathrm{C}$.
* **Validation:**
  1. Neutral reference check: At $T_{\mathrm{trk},k} = 35.0\,{}^\circ\mathrm{C}$, $T_{\mathrm{air},k} = 25.0\,{}^\circ\mathrm{C}$, $\phi_k = 50.0\%$, both Equation (8.1a) and Equation (8.1b) evaluate to $F_{\mathrm{env}} = 1.000$ exactly.
  2. Dimensional homogeneity check: $\kappa_{\mathrm{trk},c} (T_{\mathrm{trk},k} - T_{\mathrm{trk,ref}})$ has units $[{}^\circ\mathrm{C}^{-1}] \cdot [{}^\circ\mathrm{C}] = [1]$ (dimensionless).

### 8.2 Evidence-Based Treatment of Relative Humidity ($\phi_k$)

Both governing specifications require `relative_humidity_pct` ($\phi_k$) to be retained as a first-class environmental input in `ScenarioEnvironmentSeries` while prohibiting the fabrication of an unsupported direct degradation effect. The physical and statistical analysis is explicit:

1. **Physical Mechanism Analysis:** In dry slick racing ($\omega_k = 0$), water vapour in ambient air does not chemically react with vulcanised carbon-black/silica tread compounds over a two-hour race timescale, nor does specific humidity alter the dry coefficient of friction between hot rubber ($100\,{}^\circ\mathrm{C}$) and dry asphalt by a measurable margin. Moist air has a slightly lower dry-air density (reducing aerodynamic downforce and drag by $< 0.5\%$ across $20\%\text{--}80\%$ RH at $25\,{}^\circ\mathrm{C}$) and a marginally higher specific heat capacity ($c_{p,\mathrm{air}}$ increases by $\sim 1.5\%$), both of which are an order of magnitude smaller than track temperature variance.
2. **Statistical Identifiability and Confounding with Temperature:** Across meteorological observations at Grand Prix venues, relative humidity $\phi_k$ is strongly negatively correlated with solar heating and track temperature $T_{\mathrm{trk},k}$ ($r \approx -0.65$ to $-0.80$, since warming air increases saturation vapour pressure $e_s(T_{\mathrm{air}})$ and lowers relative humidity while direct sunlight simultaneously heats the asphalt). If an unconstrained regression fits $\Delta h_k$ against $(T_{\mathrm{trk},k}, T_{\mathrm{air},k}, \phi_k)$, collinearity causes $\psi_{\mathrm{hum},c}$ to absorb noisy variance from $T_{\mathrm{trk},k}$.
3. **Simulator Rule for $\psi_{\mathrm{hum},c}$:** The schema stores $\phi_k$ on every lap, and Equation (8.1) includes the dimensionless term $\psi_{\mathrm{hum},c}(\phi_k - \phi_{\mathrm{ref}})/100\%$, but the baseline model parameter set fixes:
   $$\psi_{\mathrm{hum},c} \equiv 0.0 \quad (\text{status: } \texttt{ASSUMED\_NEUTRAL\_UNIDENTIFIED})$$
   Where relative humidity genuinely matters is as an **indirect predictor of precipitation onset / dew point** in wet-transition forecasting (Stage M5) and in radiative balance models of track temperature when direct track sensor measurements are absent.

### 8.3 Empirical Inference of Track Temperature When Only Weather Forecasts Are Available

When simulating a future race using a meteorological API such as the [Open-Meteo Weather Forecast API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs), the API provides $2\,\mathrm{m}$ air temperature $T_{\mathrm{air},k}$, shortwave solar radiation $I_{\mathrm{sol},k}$ ($\mathrm{W\cdot m^{-2}}$), wind speed $v_{\mathrm{wind},k}$ ($\mathrm{m\cdot s^{-1}}$), and relative humidity $\phi_k$, but **does not measure asphalt surface temperature $T_{\mathrm{trk},k}$**. Substituting $T_{\mathrm{air},k}$ directly for $T_{\mathrm{trk},k}$ introduces a systematic downward bias of $8\text{--}22\,{}^\circ\mathrm{C}$ on sunny afternoons. When `track_temperature_c` is missing in forecast mode, the ingestion adapter estimates $\widehat{T}_{\mathrm{trk},k}$ via a steady-state surface energy balance approximation:

$$\widehat{T}_{\mathrm{trk},k} = T_{\mathrm{air},k} + \frac{(1 - \alpha_{\mathrm{alb}}(r)) \, I_{\mathrm{sol},k}}{h_{\mathrm{rad,0}} + h_{\mathrm{wind}} \, v_{\mathrm{wind},k}}$$

* **Equation Reference:** Equation (8.2) — Quasi-Steady Asphalt Surface Temperature Estimation from Meteorological Inputs.
* **Variables & Units:** Asphalt solar albedo $\alpha_{\mathrm{alb}}(r) \in [0.05, 0.20]$ (aged dark asphalt $\approx 0.10$); baseline radiative/natural-convection conductance $h_{\mathrm{rad,0}} \approx 18.0\,\mathrm{W\cdot m^{-2}\cdot {}^\circ C^{-1}}$; forced wind convection coefficient $h_{\mathrm{wind}} \approx 3.5\,\mathrm{W\cdot s\cdot m^{-3}\cdot {}^\circ C^{-1}}$.
* **Provenance Tagging:** Any $\widehat{T}_{\mathrm{trk},k}$ computed via Equation (8.2) is automatically tagged `source_status =DERIVED`, `measurement_semantics = FORECAST`, with an enlarged uncertainty $\sigma_{T_{\mathrm{trk}}} = \pm 4.5\,{}^\circ\mathrm{C}$ propagated into the strategy sensitivity analysis.

---

## 9. Stint, Pit-Stop Accounting Convention, Feasibility Constraints, and Total Race-Time Equations

### 9.1 Strategy Representation and Stint-to-Lap Indexing

A candidate race strategy $\pi$ over an $L$-lap race consists of $J \in \mathbb{Z}^+$ contiguous stints:

$$\pi = \big((r_1, n_1, \sigma_1),\; (r_2, n_2, \sigma_2),\; \ldots,\; (r_J, n_J, \sigma_J)\big)$$

where $r_j \in \mathcal{R} = \{S, M, H\}$ is the event role for stint $j$, resolved to physical compound $c_j = m_e(r_j) \in \mathcal{N}_e$; $n_j \in \mathbb{Z}^+$ is the number of racing laps completed in stint $j$; and $\sigma_j$ identifies the physical tyre set (with initial health $\mathbf{h}_{j,\mathrm{init}} = \mathbf{1}$ for a brand-new set, or $\mathbf{h}_{j,\mathrm{init}} \in (0, 1)^4$ for a pre-scrubbed qualifying/practice set).

Define the cumulative lap index at the end of stint $j$ as $L_j = \sum_{\ell=1}^j n_\ell$, with $L_0 \equiv 0$ and $L_J = L$. Thus stint $j$ spans race laps:

$$k \in \mathcal{K}_j = \{L_{j-1} + 1,\; L_{j-1} + 2,\; \ldots,\; L_j\}, \qquad |\mathcal{K}_j| = n_j, \qquad \sum_{j=1}^J n_j = L$$

Within stint $j$, on race lap $k = L_{j-1} + m$ (for $m \in \{1, 2, \ldots, n_j\}$), the tyre age in completed laps at the start of lap $k$ is $a_k = a_{j,0} + m - 1$ (where $a_{j,0} = 0$ for a new set), and the tyre health state evolves recursively via Equation (7.1) starting from $\mathbf{h}_{L_{j-1}+1} = \mathbf{h}_{j,\mathrm{init}}$.

### 9.2 Non-Overlapping Pit-Stop Time-Loss Convention

A frequent bug in race strategy simulators is double-counting pit-entry braking, pit-lane travel, or out-lap warm-up between the lap-time equation $T_k$ and the discrete pit-stop penalty $P_j$. To eliminate ambiguity, this framework establishes the **Incremental Main-Straight Replacement Convention**:

1. **Definition of $T_k$ on In-Laps and Out-Laps:** For every lap $k \in \{1, \ldots, L\}$—including in-laps ($k = L_j$ for $j < J$) and out-laps ($k = L_j + 1$ for $j < J$)—the lap-time term $T_k$ in Equation (6.1) calculates the full uninterrupted racing lap time along the normal main-straight racing line, plus only the **out-lap cold-tyre warm-up grip penalty** $\Delta T_{\mathrm{in/out},k} = \Delta T_{\mathrm{out}}(c_{j+1}, r) \cdot \mathbb{I}(k = L_j + 1 \text{ and } j \ge 1)$ experienced in the racing sectors *after* crossing the pit-exit timing loop.
2. **Definition of Net Incremental Pit Loss $P_j$:** For each inter-stint pit stop $j \in \{1, \ldots, J-1\}$ occurring at the boundary between lap $L_j$ (in-lap) and lap $L_j + 1$ (out-lap), $P_j$ (in seconds) is defined strictly as the **net extra elapsed time** required to diverge from the pit-entry commitment point, traverse the pit lane at speed limit $v_{\mathrm{pit}}$, remain stationary in the pit box for tyre change $\tau_{\mathrm{stat},j}$, and accelerate from the pit-exit line back to racing speed, **minus** the time $T_{\mathrm{main-bypass}}(r, v)$ the car would have taken to cover the parallel main-straight segment between pit entry and pit exit at racing speed:

$$P_j = \underbrace{\left(\frac{L_{\mathrm{pit}}(r)}{v_{\mathrm{pit}}(r) / 3.6} - T_{\mathrm{main-bypass}}(r, v)\right)}_{\Delta T_{\mathrm{pit-lane}}(r, v)} + \tau_{\mathrm{stat},j} + \Delta T_{\mathrm{pit-accel}}(r, v)$$

* **Equation Reference:** Equation (9.1) — Net Incremental Pit-Stop Time-Loss Decomposition.
* **Purpose:** Computes the exact additive time loss $P_j$ (in seconds) of executing pit stop $j$ relative to uninterrupted racing laps $T_{L_j}$ and $T_{L_j+1}$.
* **Variables & Units:**
  * $P_j \in [14.0, 30.0]\,\mathrm{s}$: Net incremental time lost at pit stop $j$.
  * $L_{\mathrm{pit}}(r) \in [200, 600]\,\mathrm{m}$: Pit-lane speed-restricted length between the pit-entry line and pit-exit line.
  * $v_{\mathrm{pit}}(r) \in [60.0, 80.0]\,\mathrm{km\cdot h^{-1}}$: Official FIA pit-lane speed limit (divided by $3.6$ to convert to $\mathrm{m\cdot s^{-1}}$, e.g., $80\,\mathrm{km\cdot h^{-1}} = 22.222\,\mathrm{m\cdot s^{-1}}$).
  * $T_{\mathrm{main-bypass}}(r, v) \in [3.0, 12.0]\,\mathrm{s}$: Time taken by a non-pitting car on the racing line to travel between the pit-entry timing loop and pit-exit timing loop.
  * $\tau_{\mathrm{stat},j} \in [1.8, 15.0]\,\mathrm{s}$: Stationary wheel-change service time in the pit box (nominal dry baseline $\tau_{\mathrm{stat}} \approx 2.40\,\mathrm{s}$; increased by $+5.0\,\mathrm{s}$ if serving a time penalty or $+8.0\,\mathrm{s}$ for a front-wing replacement).
  * $\Delta T_{\mathrm{pit-accel}}(r, v) \in [1.5, 4.5]\,\mathrm{s}$: Braking deceleration loss prior to the pit-entry line plus acceleration loss from $v_{\mathrm{pit}}$ to racing speed after the pit-exit line.
* **Calibration:** In practice, when historical timing data from [FastF1](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) is available, the composite reference pit loss $P_{\mathrm{ref}}(r) = \Delta T_{\mathrm{pit-lane}}(r, v) + \tau_{\mathrm{stat,ref}} + \Delta T_{\mathrm{pit-accel}}(r, v)$ is calibrated directly from observed clean pit stops as:
  $$P_{\mathrm{ref}}(r) = \operatorname{median}_{\text{clean stops}}\!\Big[ \left(T_{L_j}^{\mathrm{obs}} - \widehat{T}_{L_j}^{\mathrm{flying}}\right) + \left(T_{L_j+1}^{\mathrm{obs}} - \widehat{T}_{L_j+1}^{\mathrm{flying}}\right) \Big]$$
  where $\widehat{T}_{L_j}^{\mathrm{flying}}$ and $\widehat{T}_{L_j+1}^{\mathrm{flying}}$(including $\Delta T_{\mathrm{out}}$) are the model's flying-lap predictions. Any scenario override to stationary time $\tau_{\mathrm{stat},j}$ then adjusts $P_j = P_{\mathrm{ref}}(r) + (\tau_{\mathrm{stat},j} - \tau_{\mathrm{stat,ref}})$.
* **Validation:** Dimensional check: $([\mathrm{m}]/[\mathrm{m\cdot s}^{-1}] - [\mathrm{s}]) + [\mathrm{s}] + [\mathrm{s}] = [\mathrm{s}]$. Verify that pit-lane travel time is counted once in $P_j$ and zero times in $T_{L_j}$ or $T_{L_j+1}$.

### 9.3 Total Elapsed Race-Time Objective Function

Combining the stint-partitioned flying-lap times $T_k$ and the $J - 1$ inter-stint pit-stop losses $P_j$ yields the total elapsed race time $T_{\mathrm{race}}(\pi)$ (in seconds):

$$T_{\mathrm{race}}(\pi) = \sum_{j=1}^{J} \underbrace{\left( \sum_{k = L_{j-1}+1}^{L_j} T_k\big(c_j, \mathbf{z}_k, \boldsymbol{\theta}_k, r, v, \mathbf{w}_k\big) \right)}_{C_{\mathrm{stint}}\big(L_{j-1},\; L_j,\; r_j,\; \sigma_j\big)} + \sum_{j=1}^{J-1} P_j(L_j, r)$$

* **Equation Reference:** Equation (9.2) — Total Elapsed Race-Time Objective Function.
* **Variables & Units:** $T_{\mathrm{race}}(\pi) \in \mathbb{R}^+$ ($\mathrm{s}$); $C_{\mathrm{stint}}(L_{j-1}, L_j, r_j, \sigma_j) \in \mathbb{R}^+$ ($\mathrm{s}$) denotes the cumulative flying-lap time of stint $j$ starting after lap $L_{j-1}$ and ending at the completion of lap $L_j$ on role $r_j$ (compound $c_j = m_e(r_j)$) using tyre set $\sigma_j$.

### 9.4 Strategy Feasibility Constraints ($\Pi_{\mathrm{feasible}}$)

A strategy $\pi = ((r_1, n_1, \sigma_1), \ldots, (r_J, n_J, \sigma_J))$ belongs to the feasible strategy space $\Pi_{\mathrm{feasible}}$ if and only if it satisfies all seven configurable event constraints:

1. **Integer Stint Positivity and Exact Race Distance Partition:**
   $$J \in \{P_{\min} + 1, \ldots, P_{\max} + 1\}, \qquad n_j \in \mathbb{Z}^+ \quad \forall j \in \{1, \ldots, J\}, \qquad \sum_{j=1}^{J} n_j = L$$
2. **Valid Role Assignment and Bijective Event Nomination Resolution:**
   $$\forall j \in \{1, \ldots, J\}, \quad r_j \in \mathcal{R} = \{S, M, H\} \quad \text{and} \quad c_j = m_e(r_j) \in \mathcal{N}_e$$
3. **Mandatory Compound Diversity Rule ([FIA Formula 1 Sporting Regulations](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110) Article B6 / Dry Race Rule):**
   In a dry race (unless specifically overridden in `EventConfiguration`), the driver must use at least $K_{\min} = \texttt{min\_distinct\_dry\_compounds}$ (standard FIA rule: $K_{\min} = 2$) distinct physical specifications during the race:
   $$\big|\{r_1, r_2, \ldots, r_J\}\big| \ge K_{\min}$$
4. **Configurable Stint-Length Bounds:**
   $$\forall j \in \{1, \ldots, J\}, \qquad n_{\min} \le n_j \le n_{\max}(r_j)$$
5. **Physical Tyre-Set Inventory / Allocation Constraint:**
   If the driver has a finite pool of allocated physical tyre sets $\mathcal{S}_{\mathrm{alloc}}(r)$ for each role $r \in \{S, M, H\}$ (with cardinality $A_r = |\mathcal{S}_{\mathrm{alloc}}(r)|$), each stint must draw a distinct physical set $\sigma_j \in \mathcal{S}_{\mathrm{alloc}}(r_j)$:
   $$\forall j_1 \neq j_2, \quad \sigma_{j_1} \neq \sigma_{j_2}, \qquad \text{and} \qquad \sum_{j=1}^J \mathbb{I}(r_j = r) \le A_r \quad \forall r \in \{S, M, H\}$$
6. **Minimum Remaining Tyre Health (Structural Safety Bound):**
   At the completion of every lap $k \in \{1, \ldots, L\}$, the remaining tyre health on all four corners must remain at or above the configured terminal safety floor $h_{\min} \in [0, 0.20]$ (default $h_{\min} = 0.05$):
   $$\forall k \in \{1, \ldots, L\}, \quad \forall i \in \{\mathrm{FL, FR, RL, RR}\}, \qquad h_{k+1,i} \ge h_{\min}$$

---

## 10. Strategy Optimiser Formulation and Computational Complexity Comparison

### 10.1 Formal Optimisation Problem Statement

The strategy optimisation problem is the constrained discrete-continuous mixed-state mathematical programme:

$$\pi^* = \arg\min_{\pi \in \Pi_{\mathrm{feasible}}} T_{\mathrm{race}}(\pi)$$

Because every model is a simplification of reality, **$\pi^*$ is strictly the exact global optimum of the declared mathematical model and parameter set**, not a guarantee of the real-world race-winning strategy under unmodelled stochastic perturbations. Consequently, the optimiser must always return the top-$M$ ranked feasible strategies $\big(\pi^{(1)}, \pi^{(2)}, \ldots, \pi^{(M)}\big)$ (default $M = 5$), their time deltas $\Delta T^{(m)} = T_{\mathrm{race}}(\pi^{(m)}) - T_{\mathrm{race}}(\pi^*)$, and their sensitivity crossover bounds.

### 10.2 Optimal Substructure Analysis: When Does Tyre State Reset vs. Carry Across Stints?

Whether the optimisation problem admits a compact dynamic programming (DP) or Directed Acyclic Graph (DAG) representation depends critically on **whether the continuous tyre state $\mathbf{z}_k = (\mathbf{h}_k, \boldsymbol{\theta}_k)$ carries across stint boundaries**:

1. **Case A — Brand-New or Fixed-Inventory Tyre Sets per Stint (Exact Stint-Boundary State Reset):**
   Whenever a pit stop at lap $L_j$ replaces the tyres with a new set ($\mathbf{h}_{L_j+1} = \mathbf{1}$) or a specific pre-scrubbed set $\sigma_{j+1}$ whose initial state $\mathbf{h}_{j+1,\mathrm{init}}$ is known a priori, the continuous intra-stint trajectory $\{\mathbf{z}_k\}_{k=L_{j-1}+1}^{L_j}$ is **completely encapsulated inside the stint cost function** $C_{\mathrm{stint}}(L_{j-1}, L_j, r_j, \sigma_j)$! Even when nonlinear wear cliffs (Stage M3), four-corner asymmetric wear (Stage M3), lap-varying weather $\mathbf{w}_k$ (Stage M2), and intra-stint thermal recurrences (Stage M4) are active, integrating the recurrence over the $n_j = L_j - L_{j-1}$ laps from $\ell_{\mathrm{start}} = L_{j-1}$ to $\ell_{\mathrm{end}} = L_j$ yields a deterministic scalar edge weight $C_{\mathrm{stint}}(\ell_{\mathrm{start}}, \ell_{\mathrm{end}}, r, \sigma)$. Thus, **exact Bellman optimal substructure holds at stint boundaries without discretising the continuous tyre health $\mathbf{h}_k$!**
2. **Case B — Path-Dependent Inter-Stint State (e.g., Refitting the Same Set Later or Battery/Fuel-Saving Coupling):**
   If a strategy were permitted to remove a tyre set at pit stop $1$ and refit that *same* set at pit stop $2$, or if the driver dynamically traded fuel burn against tyre wear across stints, then the state at a pit stop would depend on the continuous wear accumulated in an earlier stint. Under FIA dry-race operations, teams never refit a race-used tyre set in a subsequent race stint. Furthermore, if finite used sets from qualifying have fixed pre-race health $\mathbf{h}_{\sigma,\mathrm{init}}$, tracking which specific sets have been consumed requires only a small discrete inventory bitmask or tuple.

### 10.3 Algorithmic Comparison: Exhaustive Enumeration vs. Stint-Boundary DAG vs. Lap-by-Lap Continuous-State DP

| Optimisation Method | State / Node Representation | Exactness Under Model Assumptions | Asymptotic Time Complexity | Concrete Operation Count ($L=60$, $P_{\max}=3$, $\lvert\mathcal{R}\rvert=3$) | Recommended Role in Simulator |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Exhaustive Stint Enumeration** | Enumerates all compositions $(n_1, \ldots, n_J)$ of $L$ into $J$ parts and all role sequences $(r_1, \ldots, r_J) \in \mathcal{R}^J$. | **Exact** for all intra-stint models (M0–M4). | $\mathcal{O}\!\left(\sum_{J=1}^{P_{\max}+1} \lvert\mathcal{R}\rvert^J \binom{L-1}{J-1} L\right)$ (or $\mathcal{O}(\dots)$ without prefix reuse). | For $J \le 4$: $\sum_{J=1}^4 3^J \binom{59}{J-1} = 3 + 9(59) + 27(1711) + 81(32509) \approx 2.68 \times 10^6$ strategies ($\sim 1.6 \times 10^8$ lap evals if un-cached). | **Verification Oracle** for $J \le 3$ and small $L \le 25$ unit tests; too slow for interactive $J=4$ sensitivity sweeps without caching. |
| **2. Stint-Boundary Layered DAG / Dynamic Programming (Recommended)** | Node $u = (\ell, j, \beta_{\mathrm{used}}, \mathbf{a}_{\mathrm{rem}})$ where $\ell \in \{0,\ldots,L\}$ is completed lap index, $j \in \{0,\ldots,P_{\max}+1\}$ is stint count, $\beta_{\mathrm{used}} \in \{0,\ldots,7\}$ is the 3-bit mask of used roles $\{S,M,H\}$, and $\mathbf{a}_{\mathrm{rem}}$ tracks remaining used/new sets. | **Exact** for all M0–M4 models whenever each stint uses a distinct tyre set with known initial state $\mathbf{h}_{\sigma,\mathrm{init}}$. | Precompute stint cost table: $\mathcal{O}\!\left(\lvert\mathcal{R}\rvert \cdot L^2\right)$ lap steps (via forward roll-out from each start lap $\ell_1$). Graph search: $\mathcal{O}\!\left(P_{\max} \cdot 2^{\lvert\mathcal{R}\rvert} \cdot \lvert\mathcal{R}\rvert \cdot L^2\right)$. | Stint table roll-out: $3 \times \frac{60 \times 61}{2} = 5{,}490$ lap steps! DAG relaxation ($P_{\max}=3$, $8$ bitmasks): $4 \times 8 \times 3 \times 1{,}830 \approx 1.75 \times 10^5$ edge relaxations ($< 2\,\mathrm{ms}$ in C++). | **Primary Production Optimiser (M0–M4):** $30{,}000\times$ faster than naive enumeration while remaining $100\%$ mathematically exact. |
| **3. Lap-by-Lap Discretised Continuous-State DP** | State at every lap $k$: $(k, r, j, \beta_{\mathrm{used}}, \hat{\mathbf{h}}_k, \hat{\boldsymbol{\theta}}_k)$ on a quantized grid of $G_h$ health bins and $G_\theta$ temperature bins. | **Approximate** (introduces grid quantization error $\mathcal{O}(1/G_h)$ and artificial numerical diffusion). | $\mathcal{O}\!\left(L \cdot P_{\max} \cdot 2^{\lvert\mathcal{R}\rvert} \cdot \lvert\mathcal{R}\rvert \cdot G_h^4 \cdot G_\theta^4\right)$ (curse of dimensionality for 4 corners). | Even with $G_h = 50$ on 4 corners, $50^4 = 6.25 \times 10^6$ states per lap ($\sim 10^{10}$ ops). | **Not Recommended for M0–M4:** Unnecessary discretisation error when Stint-Boundary DAG solves the continuous intra-stint recurrence exactly. Reserved only if M5 introduces continuous intra-lap pace control $u_k$. |

### 10.4 Exact Stint-Boundary DAG / Dynamic Programming Formulation and Recurrence

Let all allocated tyre sets for role $r \in \{S, M, H\}$ be brand-new sets ($\mathbf{h}_{\mathrm{init}} = \mathbf{1}$; if pre-used sets exist, they are ordered by highest initial health first).

#### Step 1: Forward Roll-Out Precomputation of the Stint Cost Matrix
For every possible stint start lap $\ell_0 \in \{0, 1, \ldots, L-1\}$ and every role $r \in \{S, M, H\}$ (resolved to physical compound $c = m_e(r)$), initialise $\mathbf{h}_{\ell_0+1} = \mathbf{1}$ at the start of lap $k = \ell_0 + 1$ and step forward lap-by-lap through $k = \ell_0 + 1, \ell_0 + 2, \ldots, L$ using Equations (6.1) and (7.1):

$$C_{\mathrm{stint}}(\ell_0, \ell_1, r) = \begin{cases} \displaystyle \sum_{k = \ell_0 + 1}^{\ell_1} T_k(c, \mathbf{h}_k, \boldsymbol{\theta}_k, r_{\mathrm{trk}}, v, \mathbf{w}_k), & \text{if } n_{\min} \le \ell_1 - \ell_0 \le n_{\max}(r) \text{ and } \min_{k \in [\ell_0+1, \ell_1],\, i} h_{k+1,i} \ge h_{\min}, \\[10pt] +\infty, & \text{otherwise (infeasible stint).} \end{cases}$$

Because $\mathbf{h}_{k+1}$ depends only on $\mathbf{h}_k$ and lap $k$'s environment $\mathbf{w}_k$, computing $C_{\mathrm{stint}}(\ell_0, \ell_1, r)$ for a fixed $(\ell_0, r)$ across all end laps $\ell_1 \in \{\ell_0 + 1, \ldots, L\}$ requires a single forward pass of $L - \ell_0$ lap evaluations. Summing over $\ell_0 \in \{0, \ldots, L-1\}$ and $r \in \{S, M, H\}$ requires exactly $3 \frac{L(L+1)}{2}$ lap evaluations.

#### Step 2: Bellman Dynamic Programming Recurrence over Stint Boundaries
Define the discrete state at the end of a stint as $(\ell, j, \beta, a_S, a_M, a_H)$, where:
* $\ell \in \{0, 1, \ldots, L\}$ is the completed race lap index;
* $j \in \{0, 1, \ldots, P_{\max} + 1\}$ is the number of stints completed so far;
* $\beta \in \{0, 1, \ldots, 7\}$ is the 3-bit integer bitmask recording which roles have been used at least once ($\mathrm{bit}_0 = S \equiv 1$, $\mathrm{bit}_1 = M \equiv 2$, $\mathrm{bit}_2 = H \equiv 4$);
* $(a_S, a_M, a_H) \in \mathbb{Z}_{\ge 0}^3$ counts the number of sets of each role used so far ($a_r \le A_r$).

Let $V(\ell, j, \beta, a_S, a_M, a_H)$ denote the minimum cumulative elapsed time to reach lap $\ell$ using $j$ stints with role-usage mask $\beta$ and set counts $(a_S, a_M, a_H)$. The base case at race start ($\ell = 0$) is:

$$V(0, 0, 0, 0, 0, 0) = 0.0\,\mathrm{s}, \qquad \text{and } V(\ell, 0, \beta, a_S, a_M, a_H) = +\infty \text{ for all other } (\ell, \beta, \mathbf{a}) \neq (0, 0, \mathbf{0})$$

For stint index $j = 1, 2, \ldots, P_{\max} + 1$ and end lap $\ell_1 \in \{j, \ldots, L\}$, the forward transition from previous stint end lap $\ell_0 \in \{j-1, \ldots, \ell_1 - 1\}$ using role $r \in \{S, M, H\}$ (with bit value $b(r) \in \{1, 2, 4\}$ and unit vector $\mathbf{e}_r$) is:

$$V\big(\ell_1, j, \beta \mathbin{|} b(r), \mathbf{a} + \mathbf{e}_r\big) = \min_{\ell_0, \beta, \mathbf{a}, r} \Big\{ V(\ell_0, j-1, \beta, \mathbf{a}) + C_{\mathrm{stint}}(\ell_0, \ell_1, r) + \mathbb{I}(j \ge 2) \, P_{j-1}(\ell_0, r_{\mathrm{trk}}) \Big\}$$

subject to $a_r + 1 \le A_r$. At $\ell_1 = L$, the optimal race time is the minimum over all terminal states satisfying the stop-count and compound-diversity rules:

$$T_{\mathrm{race}}(\pi^*) = \min_{\substack{j \in [P_{\min}+1,\; P_{\max}+1] \\ \operatorname{popcount}(\beta) \ge K_{\min} \\ \mathbf{a} \le \mathbf{A}}} V(L, j, \beta, \mathbf{a})$$

To extract the top-$M$ ranked feasible strategies $\pi^{(1)}, \ldots, \pi^{(M)}$ (rather than only $\pi^*$), each DP table entry stores a bounded min-heap of the top-$M$ incoming parent pointers (`std::array<CandidatePath, M>`), increasing complexity by a small constant factor $M = 5$.

---

## 11. Calibration Plan, Parameter Identifiability, Confounder Control, and Uncertainty Analysis

### 11.1 Data Splitting Protocol and Anti-Leakage Architecture

To prevent overfitting and circular validation ("data leakage"), historical timing and weather observations ingested into `HistoricalLapDataset` must be partitioned chronologically and at the **event level** before fitting any parameter:

1. **Calibration (Training) Set ($\mathcal{D}_{\mathrm{cal}}$):** Historical dry race and Free Practice 2 (FP2 long-run) sessions up to event $e - 1$ (e.g., prior season plus completed rounds of the current season). Used to estimate compound catalogue parameters $(\Delta T_{\mathrm{comp},0}, d_{0,c}, \alpha_c, \kappa_{\mathrm{trk},c})$ and circuit baseline parameters $(T_{\mathrm{ref}}, \gamma_{\mathrm{fuel}}, P_{\mathrm{ref}})$.
2. **Validation Set ($\mathcal{D}_{\mathrm{val}}$):** Held-out complete race events (or held-out cars/teams within prior events) used to tune structural hyperparameters (e.g., whether Stage M3 cliff parameters $\beta_c > 0$ improve out-of-sample stint RMSE over Stage M1 linear wear, or selecting Huber loss threshold $\delta$).
3. **Final Out-of-Sample Test Set ($\mathcal{D}_{\mathrm{test}}$):** Completely unseen future race events evaluated ** strictly without post-hoc refitting** of wear or pace slopes. If pre-race practice (FP1/FP2) from target event $e^*$ is used to anchor $T_{\mathrm{ref}}(r, v)$, that anchor is logged as a `PRE_RACE_PRACTICE_ANCHOR`, and race laps from $e^*$ are never touched prior to test evaluation.

### 11.2 Confounder Analysis and Mandatory Data-Filtering Rules

A raw increase in observed lap time $T_k^{\mathrm{obs}}$ across a stint is the superposition of tyre wear, fuel burn, track rubbering, traffic, yellow flags, driver management, and pit in/out anomalies. Fitting tyre wear directly to unfiltered lap times produces severely biased coefficients (often even predicting *negative* tyre degradation when fuel burn outweighs mild wear on the `C1`/`C2` Hard tyre!).

| Confounder | Direction of Bias if Ignored | Mandatory Mitigation / Control Rule in Calibration Pipeline |
| :--- | :--- | :--- |
| **1. Fuel Mass Burn-Off** | Makes car $\sim 0.05\text{--}0.07\,\mathrm{s\cdot lap}^{-1}$ faster each lap, masking tyre wear and biasing $\alpha_c$ downward by $40\%\text{--}80\%$. | **Deterministic Pre-Subtraction:** Subtract $\Delta T_{\mathrm{fuel},k} = \gamma_{\mathrm{fuel}}(r)(M_{\mathrm{fuel},k} - M_{\mathrm{ref}})$ from every observed lap time *before* estimating tyre degradation parameters: $\widetilde{T}_k^{\mathrm{obs}} = T_k^{\mathrm{obs}} - \Delta T_{\mathrm{fuel},k}$. |
| **2. Pit In-Laps and Out-Laps** | Pit entry braking and pit-lane speed limit add $15\text{--}28\,\mathrm{s}$, corrupting stint endpoints. | **Hard Filter Exclusion:** Exclude all laps where `PitInTime != NULL` or `PitOutTime != NULL` from the flying-lap regression sample; use them exclusively to calibrate $P_{\mathrm{ref}}(r)$ and $\Delta T_{\mathrm{out}}$. |
| **3. Opening Lap ($k=1$) & Standing Start** | Standing start and Turn-1 pack congestion add $3\text{--}7\,\mathrm{s}$ on lap $1$. | **Separate Offset Estimation:** Exclude $k=1$ from tyre slope estimation; estimate $\Delta T_{\mathrm{start}}(r, v)$ directly from clean $T_1^{\mathrm{obs}} - T_2^{\mathrm{obs}}$ differences. |
| **4. Safety Car (SC), VSC & Yellow Flags** | Track status codes (`4 = SC`, `6 = VSC`, `2 = Yellow`) slow laps by $1\text{--}45\,\mathrm{s}$ while cooling tyres. | **Track Status Masking:** Require `TrackStatus == "1"` (all-clear green) across the entire lap and the preceding lap (to avoid post-SC cold-tyre restart distortion). |
| **5. Traffic / Dirty Air & DRS Trains** | Following within $< 1.5\,\mathrm{s}$ increases aerodynamic wake loss ($\sim 0.3\text{--}0.8\,\mathrm{s}$) while DRS gains $\sim 0.3\text{--}0.5\,\mathrm{s}$. | **Interval Headway Filter + Robust Loss:** Filter out laps where headway to the car ahead is $< 1.2\,\mathrm{s}$ (when interval channel is available) and fit remaining laps with **Huber M-estimation** ($\delta = 0.40\,\mathrm{s}$) or asymmetric quantile filtering to reject traffic spikes. |
| **6. Track Surface Evolution** | Early-race green track makes Stint 1 look faster-improving than Stint 3, biasing Soft/Medium wear rates. | **Joint Mixed-Effects / Demeaned Stage Fit:** Estimate $\Delta T_{\mathrm{evol},k}$ from cross-compound field medians or include lap index $k$ exponential decay jointly across all cars on track. |

### 11.3 Parameter Identifiability Analysis

A mathematical model is only useful if its free parameters can be uniquely identified from available observations. The analysis below audits three structural identifiability bottlenecks and enforce canonical re-parameterisations:

1. **Collinearity of Wear Rate $d_{0,c}$ and Pace Penalty Slope $\alpha_c$ in Linear Wear (Stages M0–M2):**
   In the linear baseline where $\bar{w}_k = a_k \cdot D_{\mathrm{base}} F_{\mathrm{env}}$ and $\Delta T_{\mathrm{tyre},k} = \alpha_c (L_{\mathrm{lap}}/L_{\mathrm{ref}}) \bar{w}_k$, the predicted lap-time degradation per lap is proportional to the **product** $\Theta_c \equiv \alpha_c d_{0,c}$ (in $\mathrm{s\cdot lap}^{-1}$). Scaling $d_{0,c} \to 2 d_{0,c}$ and $\alpha_c \to \frac{1}{2}\alpha_c$ leaves all linear lap times unchanged!
   * **Identifiability Resolution:** Anchor $d_{0,c}$ externally to the compound's reference maximum structural stint length $L_{\max,c}^{\mathrm{ref}}$ via $d_{0,c} \equiv \frac{1}{L_{\max,c}^{\mathrm{ref}}}$ (so that $h_k \to 0$ at $a_k = L_{\max,c}^{\mathrm{ref}}$). With $d_{0,c}$ fixed by this physical scale definition, $\alpha_c = \Theta_c / d_{0,c}$ is **globally and uniquely identifiable** from the observed fuel-corrected lap-time slope $\Theta_c$.
2. **Collinearity of Fuel Sensitivity $\gamma_{\mathrm{fuel}}$, Track Evolution $A_{\mathrm{evol}}$, and Linear Tyre Wear $\Theta_c$ Within a Single Stint:**
   Within any single stint $j$, race lap $k$ and tyre age $a_k$ satisfy $k = L_{j-1} + a_k + 1$, meaning $\frac{\partial k}{\partial a_k} = 1$ (perfect collinearity within one stint).
   * **Identifiability Resolution:** Identify $\gamma_{\mathrm{fuel}}$ and $A_{\mathrm{evol}}$ across **stint boundaries** (comparing lap $L_j$ on old tyres at age $a_{L_j}$ immediately before a pit stop with lap $L_j + 2$ on new tyres at age $a_{L_j+2} = 1$ immediately after the pit stop, where fuel load $M_{\mathrm{fuel}}$ and track evolution $\Delta T_{\mathrm{evol}}$ are virtually identical across the 2-lap window, isolating the pure tyre age jump $\Delta T_{\mathrm{tyre}}(a_{L_j}) - \Delta T_{\mathrm{tyre}}(1)$!).
3. **Non-Identifiability of Four-Corner Wear Rates Without Corner-Force Priors:**
   A single scalar timing loop per lap (or three sector loops) cannot identify four independent corner wear rates $(d_{\mathrm{FL}}, d_{\mathrm{FR}}, d_{\mathrm{RL}}, d_{\mathrm{RR}})$ as free regression parameters.
   * **Identifiability Resolution:** In Stage M3, the four corner multipliers $\lambda_{r,i}$ are **fixed deterministic functions of observed circuit geometry** ($N_L, N_R, E_{\mathrm{lat}}, E_{\mathrm{brk}}, E_{\mathrm{trc}}$) rather than free regression degrees of freedom.

### 11.4 Calibration Estimation Procedure (Step-by-Step)

1. **Step 1 — Clean Dataset Construction:** Filter `HistoricalLapDataset` using the masks in Section 11.2 (`is_clean_green_lap == true`).
2. **Step 2 — Pit-Stop Loss & Out-Lap Warm-Up Estimation:** Compute $P_{\mathrm{ref}}(r)$ from median in-lap + out-lap residuals relative to adjacent flying laps ($L_j - 1$ and $L_j + 2$).
3. **Step 3 — Fuel & Evolution Detrending:** Subtract $\Delta T_{\mathrm{fuel},k} = \gamma_{\mathrm{fuel}}(r)(M_{\mathrm{fuel},k} - M_{\mathrm{ref}})$ and $\Delta T_{\mathrm{evol},k}(r)$ from $T_k^{\mathrm{obs}}$ to form fuel-corrected lap times $\widetilde{T}_k^{\mathrm{obs}}$.
4. **Step 4 — Robust Mixed-Effects Stint Regression:** Fit vehicle/circuit baseline $T_{\mathrm{ref}}(r, v)$, compound offsets $\Delta T_{\mathrm{comp},0}(c)$, and wear slopes $\alpha_c$ by minimising the Huber loss $\sum \rho_\delta\big(\widetilde{T}_k^{\mathrm{obs}} - \widehat{T}_k(\boldsymbol{\Phi})\big)$ via L-BFGS-B subject to monotonicity constraints:
   $$\Delta T_{\mathrm{comp},0}(C1) > \Delta T_{\mathrm{comp},0}(C2) > 0 = \Delta T_{\mathrm{comp},0}(C3) > \Delta T_{\mathrm{comp},0}(C4) > \Delta T_{\mathrm{comp},0}(C5)$$
   $$d_{0,C1} < d_{0,C2} < d_{0,C3} < d_{0,C4} < d_{0,C5}, \qquad \alpha_c > 0$$
5. **Step 5 — Uncertainty Quantification:** Compute sandwich / block-bootstrap covariance matrix $\boldsymbol{\Sigma}_{\boldsymbol{\Phi}}$ (blocking by driver-stint to account for serial autocorrelation of lap times within a stint) and write $95\%$ confidence intervals into `CalibratedModelParameters`.

### 11.5 Uncertainty Propagation and Strategy Robustness / Sensitivity Analysis

Two orthogonal sources of uncertainty are distinguished:
* **Parameter Uncertainty ($\boldsymbol{\Phi} \sim \mathcal{N}(\hat{\boldsymbol{\Phi}}, \boldsymbol{\Sigma}_{\boldsymbol{\Phi}})$):** Imperfect knowledge of calibrated coefficients $(\alpha_c, \Delta T_{\mathrm{comp},0}, P_{\mathrm{ref}})$.
* **Scenario Uncertainty ($\mathbf{w} \in \mathcal{W}$):** Imperfect forecast of race-day track temperature $T_{\mathrm{trk},k} \pm \Delta T_{\mathrm{trk}}$ or pit-stop execution time $\tau_{\mathrm{stat}} \pm \Delta \tau$.

For any two top candidate strategies $\pi^{(1)} = \pi^*$ (e.g., optimal 1-stop) and $\pi^{(2)}$ (e.g., best 2-stop), define the **Strategy Decision Margin**:

$$\Delta T_{1,2}(\boldsymbol{\Phi}, \mathbf{w}) \equiv T_{\mathrm{race}}\big(\pi^{(2)}; \boldsymbol{\Phi}, \mathbf{w}\big) - T_{\mathrm{race}}\big(\pi^{(1)}; \boldsymbol{\Phi}, \mathbf{w}\big) \ge 0$$

The simulator outputs three robustness diagnostics alongside $\pi^*$:

1. **First-Order Local Sensitivity Gradients:** Partial derivatives $\frac{\partial \Delta T_{1,2}}{\partial \alpha_c}$, $\frac{\partial \Delta T_{1,2}}{\partial P_{\mathrm{ref}}}$, and $\frac{\partial \Delta T_{1,2}}{\partial T_{\mathrm{trk}}}$ computed via exact analytical differentiation or central finite differences. Note immediately that for comparing an $(S_{\mathrm{stops}}=1)$-stop strategy $\pi^{(1)}$ against an $(S_{\mathrm{stops}}=2)$-stop strategy $\pi^{(2)}$, the sensitivity to pit-lane loss is exactly $\frac{\partial \Delta T_{1,2}}{\partial P_{\mathrm{ref}}} = +1.0\,\mathrm{s/s}$!
2. **Exact Parameter Crossover Thresholds:** The critical value of degradation multiplier or pit loss at which $\Delta T_{1,2} = 0$ (where the optimal strategy switches from 1-stop to 2-stop).
3. **Bounded Scenario Ensemble Evaluation:** Evaluate the top-$M$ strategies across a $3 \times 3$ deterministic grid of $\big(T_{\mathrm{trk}} \in \{T_{\mathrm{nom}}-5{}^\circ\mathrm{C}, T_{\mathrm{nom}}, T_{\mathrm{nom}}+5{}^\circ\mathrm{C}\}\big) \times \big(P_j \in \{P_{\mathrm{ref}}-1.5\,\mathrm{s}, P_{\mathrm{ref}}, P_{\mathrm{ref}}+2.5\,\mathrm{s}\}\big)$ and report the **Minimax Regret Strategy**:
   $$\pi_{\mathrm{robust}} = \arg\min_{\pi \in \Pi_{\mathrm{feasible}}} \max_{\mathbf{s} \in \mathcal{S}_{\mathrm{grid}}} \Big( T_{\mathrm{race}}(\pi; \mathbf{s}) - \min_{\pi' \in \Pi_{\mathrm{feasible}}} T_{\mathrm{race}}(\pi'; \mathbf{s}) \Big)$$

---

## 12. Hand-Checkable Synthetic Examples and Verification Test Suite

**Important Notice:** All parameters and race scenarios in Section 12 are **constructed synthetic examples** designed so that every lap time, tyre health update, pit-stop cost, and optimal strategy choice can be verified by hand or unit test to full floating-point precision.

### 12.1 Synthetic Worked Example 1: Event Role Mapping, Single-Lap & Stint Calculation (10-Lap Toy Race)

#### Declared Synthetic Scenario Parameters
* **Circuit & Race Setup (`track_id = "synth_circuit_alpha"`):**
  * Race distance: $L = 10\,\mathrm{laps}$.
  * Lap length: $L_{\mathrm{lap}} = 5000.0\,\mathrm{m} = L_{\mathrm{ref}}$ (so length ratio $L_{\mathrm{lap}}/L_{\mathrm{ref}} = 1.000$).
  * Static stress index: $S_{\mathrm{str}}(r, \mathrm{agg}) = 1.000$, vehicle factor $\zeta_v = 1.000$, grip scaling $\psi_{\mathrm{grip}} = 1.000$.
  * Reference lap time: $T_{\mathrm{ref}}(r, v) = 90.000\,\mathrm{s}$.
  * Fuel parameters: To keep hand arithmetic transparent, set fuel effect neutral for Example 1 ($\gamma_{\mathrm{fuel}} = 0.000\,\mathrm{s\cdot kg}^{-1}$) and track evolution neutral ($A_{\mathrm{evol}} = 0.000\,\mathrm{s}$), standing-start loss $\Delta T_{\mathrm{start}} = 0.000\,\mathrm{s}$, out-lap warm-up loss $\Delta T_{\mathrm{out}} = 0.000\,\mathrm{s}$. (Example 2 below tests non-zero fuel, temperature, and cliff effects!)
  * Net incremental pit-stop loss: $P_1 = P_{\mathrm{ref}} = 20.000\,\mathrm{s}$.
  * Regulatory rules: Mandatory minimum distinct dry compounds $K_{\min} = 2$ (so at least 1 pit stop is mandatory: $J \ge 2$); minimum stint length $n_{\min} = 1$; minimum tyre health floor $h_{\min} = 0.050$.
* **Event Compound Nomination and Role Mapping ($m_e$):**
  * Nominated subset: $\mathcal{N}_e = \{C2, C3, C4\} \subset \{C1, C2, C3, C4, C5\}$.
  * Configured injective mapping:
    $$m_e(H) = C2, \qquad m_e(M) = C3, \qquad m_e(S) = C4$$
  * Synthetic physical compound parameters for $C2, C3, C4$ (linear baseline $\beta_c = 0$):
    * **$C2$ (mapped to Hard $H$):** $\Delta T_{\mathrm{comp},0}(C2) = +0.600\,\mathrm{s}$, $d_{0,C2} = 0.020\,\mathrm{lap}^{-1}$, $\alpha_{C2} = 5.000\,\mathrm{s}$ (so under reference conditions $\Delta h = 0.020\,\mathrm{lap}^{-1}$ and time loss increases by $\alpha_{C2} d_{0,C2} = 5.000 \times 0.020 = 0.100\,\mathrm{s\cdot lap}^{-1}$).
    * **$C3$ (mapped to Medium $M$):** $\Delta T_{\mathrm{comp},0}(C3) = 0.000\,\mathrm{s}$, $d_{0,C3} = 0.040\,\mathrm{lap}^{-1}$, $\alpha_{C3} = 5.000\,\mathrm{s}$ (so under reference conditions $\Delta h = 0.040\,\mathrm{lap}^{-1}$ and time loss increases by $\alpha_{C3} d_{0,C3} = 5.000 \times 0.040 = 0.200\,\mathrm{s\cdot lap}^{-1}$).
    * **$C4$ (mapped to Soft $S$):** $\Delta T_{\mathrm{comp},0}(C4) = -0.600\,\mathrm{s}$, $d_{0,C4} = 0.080\,\mathrm{lap}^{-1}$, $\alpha_{C4} = 5.000\,\mathrm{s}$ (so under reference conditions $\Delta h = 0.080\,\mathrm{lap}^{-1}$ and time loss increases by $\alpha_{C4} d_{0,C4} = 5.000 \times 0.080 = 0.400\,\mathrm{s\cdot lap}^{-1}$).
* **Environment Scenario (Case 1A — Reference Environment):**
  * Constant conditions: $T_{\mathrm{trk},k} = 35.0\,{}^\circ\mathrm{C} = T_{\mathrm{trk,ref}}$, $T_{\mathrm{air},k} = 25.0\,{}^\circ\mathrm{C} = T_{\mathrm{air,ref}}$, $\phi_k = 50.0\% = \phi_{\mathrm{ref}}$.
  * By Equation (8.1), $F_{\mathrm{env}}(c, \mathbf{w}_k) = 1.000$ for all compounds.

#### Step-by-Step Hand Calculation of Strategy $\pi_A = ((S, 4), (M, 6))$
Under strategy $\pi_A$, Stint 1 runs $n_1 = 4\,\mathrm{laps}$ ($k = 1, 2, 3, 4$) on role $S \mapsto C4$, followed by a pit stop at the end of lap $4$ ($P_1 = 20.000\,\mathrm{s}$), and Stint 2 runs $n_2 = 6\,\mathrm{laps}$ ($k = 5, 6, 7, 8, 9, 10$) on role $M \mapsto C3$. Notice that for $m \in \{1, \ldots, n_j\}$ on a brand-new set ($h_{\mathrm{init}} = 1.000$, $w_{\mathrm{init}} = 0.000$) with constant per-lap wear increment $\Delta w_c = d_{0,c}$, the wear at the start of the $m$-th stint lap is $w_m = (m - 1)\Delta w_c$, and the closed-form sum of $n_j$ laps in stint $j$ is:

$$C_{\mathrm{stint}}(n_j, c) = n_j \Big(T_{\mathrm{ref}} + \Delta T_{\mathrm{comp},0}(c)\Big) + \alpha_c \Delta w_c \sum_{m=1}^{n_j} (m - 1) = n_j \Big(T_{\mathrm{ref}} + \Delta T_{\mathrm{comp},0}(c)\Big) + \alpha_c \Delta w_c \frac{n_j (n_j - 1)}{2}$$

The table below verifies every individual lap of $\pi_A = ((S, 4), (M, 6))$ in detail:

| Race Lap $k$ | Stint $j$ | Role $r_j \mapsto c_j$ | Stint Lap $m$ | Tyre Age $a_k$ ($\mathrm{laps}$) | Start Health $h_k$ | Start Wear $w_k = 1-h_k$ | Base + Comp Pace $T_{\mathrm{ref}}+\Delta T_{\mathrm{comp}}$ ($\mathrm{s}$) | Tyre Time Penalty $\Delta T_{\mathrm{tyre},k} = \alpha_c w_k$ ($\mathrm{s}$) | Flying Lap Time $T_k$ ($\mathrm{s}$) | End Health $h_{k+1}$ |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| $1$ | $1$ | $S \mapsto C4$ | $1$ | $0$ | $1.000$ | $0.000$ | $89.400$ | $0.000$ | $89.400$ | $0.920$ |
| $2$ | $1$ | $S \mapsto C4$ | $2$ | $1$ | $0.920$ | $0.080$ | $89.400$ | $0.400$ | $89.800$ | $0.840$ |
| $3$ | $1$ | $S \mapsto C4$ | $3$ | $2$ | $0.840$ | $0.160$ | $89.400$ | $0.800$ | $90.200$ | $0.760$ |
| $4$ | $1$ | $S \mapsto C4$ | $4$ | $3$ | $0.760$ | $0.240$ | $89.400$ | $1.200$ | $90.600$ | $0.680$ |
| **Pit $1$** | — | **Pit $S \to M$** | — | — | — | — | — | — | **$P_1 = 20.000$** | Reset $1.000$ |
| $5$ | $2$ | $M \mapsto C3$ | $1$ | $0$ | $1.000$ | $0.000$ | $90.000$ | $0.000$ | $90.000$ | $0.960$ |
| $6$ | $2$ | $M \mapsto C3$ | $2$ | $1$ | $0.960$ | $0.040$ | $90.000$ | $0.200$ | $90.200$ | $0.920$ |
| $7$ | $2$ | $M \mapsto C3$ | $3$ | $2$ | $0.920$ | $0.080$ | $90.000$ | $0.400$ | $90.400$ | $0.880$ |
| $8$ | $2$ | $M \mapsto C3$ | $4$ | $3$ | $0.880$ | $0.120$ | $90.000$ | $0.600$ | $90.600$ | $0.840$ |
| $9$ | $2$ | $M \mapsto C3$ | $5$ | $4$ | $0.840$ | $0.160$ | $90.000$ | $0.800$ | $90.800$ | $0.800$ |
| $10$ | $2$ | $M \mapsto C3$ | $6$ | $5$ | $0.800$ | $0.200$ | $90.000$ | $1.000$ | $91.000$ | $0.760$ |

* **Stint 1 Total ($C_{\mathrm{stint}}(0, 4, S)$):** $89.400 + 89.800 + 90.200 + 90.600 = 360.000\,\mathrm{s}$ (Closed-form check: $4(89.400) + 0.400 \frac{4 \times 3}{2} = 357.600 + 2.400 = 360.000\,\mathrm{s}$).
* **Stint 2 Total ($C_{\mathrm{stint}}(4, 10, M)$):** $90.000 + 90.200 + 90.400 + 90.600 + 90.800 + 91.000 = 543.000\,\mathrm{s}$ (Closed-form check: $6(90.000) + 0.200 \frac{6 \times 5}{2} = 540.000 + 3.000 = 543.000\,\mathrm{s}$).
* **Pit Loss ($P_1$):** $20.000\,\mathrm{s}$.
* **Total Race Time $T_{\mathrm{race}}(\pi_A)$:** $360.000 + 543.000 + 20.000 = \mathbf{923.000\,\mathrm{s}}$.

#### Global Optimisation Across All Feasible Strategies in Example 1A
Why is $n_1 = 4, n_2 = 6$ optimal for the $(S, M)$ compound pair? Let $n_1 \in \{1, \ldots, 9\}$ and $n_2 = 10 - n_1$. Using the closed-form stint cost, total race time for $((S, n_1), (M, 10 - n_1))$ is:

$$T_{\mathrm{race}}(n_1) = 89.4 n_1 + 0.2 n_1(n_1 - 1) + 90.0(10 - n_1) + 0.1(10 - n_1)(9 - n_1) + 20.0 = 0.3 n_1^2 - 2.7 n_1 + 929.0\,\mathrm{s}$$

Taking the discrete first difference $\Delta T(n_1) = T_{\mathrm{race}}(n_1 + 1) - T_{\mathrm{race}}(n_1) = 0.6 n_1 - 2.4$:
* For $n_1 = 3$: $T_{\mathrm{race}}(3) = 0.3(9) - 2.7(3) + 929.0 = 923.600\,\mathrm{s}$.
* For $n_1 = 4$: $\Delta T(4) = 0.6(4) - 2.4 = 0.000\,\mathrm{s}$, so **both $n_1 = 4$ and $n_1 = 5$ achieve the exact minimum $T_{\mathrm{race}}(4) = T_{\mathrm{race}}(5) = 923.000\,\mathrm{s}$!**
* What about the $(S, H) \mapsto (C4, C2)$ and $(M, H) \mapsto (C3, C2)$ compound pairs, or 2-stop strategies ($J = 3$)? The table below compares the best stint splits across every compound combination in Example 1A:

| Rank | Strategy $\pi$ | Stint 1 Cost ($\mathrm{s}$) | Stint 2 Cost ($\mathrm{s}$) | Stint 3 Cost ($\mathrm{s}$) | Total Pit Loss ($\mathrm{s}$) | Total Race Time $T_{\mathrm{race}}(\pi)$ ($\mathrm{s}$) | Gap to $\pi^*$ ($\mathrm{s}$) |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **$1$ (tie)** | **$\pi_1^* = ((S, 4), (M, 6))$** | $360.000$ | $543.000$ | — | $20.000$ | **$923.000$** | **$0.000$** |
| **$1$ (tie)** | **$\pi_2^* = ((S, 5), (M, 5))$** | $451.000$ | $452.000$ | — | $20.000$ | **$923.000$** | **$0.000$** |
| $3$ | $\pi_3 = ((S, 3), (M, 7))$ | $269.400$ | $634.200$ | — | $20.000$ | $923.600$ | $+0.600$ |
| $4$ | $\pi_4 = ((S, 6), (M, 4))$ | $542.400$ | $361.200$ | — | $20.000$ | $923.600$ | $+0.600$ |
| $5$ (tie) | $\pi_5 = ((S, 4), (H, 6))$ | $360.000$ | $545.100$ | — | $20.000$ | $925.100$ | $+2.100$ |
| $5$ (tie) | $\pi_6 = ((S, 5), (H, 5))$ | $451.000$ | $454.000$ | — | $20.000$ | $925.000$ | $+2.000$ |
| $7$ | $\pi_7 = ((M, 6), (H, 4))$ | $543.000$ | $363.000$ | — | $20.000$ | $926.000$ | $+3.000$ |
| $8$ (Best 2-Stop) | $\pi_8 = ((S, 4), (S, 3), (M, 3))$ | $360.000$ | $269.400$ | $270.600$ | $40.000$ | $940.000$ | $+17.000$ |

*(Note: Because the environment is constant and fuel effects are zero in Example 1A, reversing stint order $((M, 6), (S, 4))$ yields the exact same total time $923.000\,\mathrm{s}$ as $((S, 4), (M, 6))$. In Example 2 below, fuel mass and dynamic temperature break this symmetry!)*

### 12.2 Synthetic Worked Example 2: Dynamic Track Temperature, Fuel Load Coupling, and Nonlinear Cliff Crossover

Now consider **Example 2**, which activates:
1. **Elevated Track Temperature ($T_{\mathrm{trk},k} = 50.0\,{}^\circ\mathrm{C}$ vs. $T_{\mathrm{trk,ref}} = 35.0\,{}^\circ\mathrm{C}$, so $\Delta T_{\mathrm{trk}} = +15.0\,{}^\circ\mathrm{C}$)** with compound-specific thermal wear sensitivities in Equation (8.1b):
   * Soft ($S \mapsto C4$): $\kappa_{\mathrm{trk},C4} = 0.040\,{}^\circ\mathrm{C}^{-1} \implies F_{\mathrm{env}}^{\mathrm{lin}}(C4) = 1.0 + 0.040(15.0) = \mathbf{1.600}$.
   * Medium ($M \mapsto C3$): $\kappa_{\mathrm{trk},C3} = 0.020\,{}^\circ\mathrm{C}^{-1} \implies F_{\mathrm{env}}^{\mathrm{lin}}(C3) = 1.0 + 0.020(15.0) = \mathbf{1.300}$.
   * Hard ($H \mapsto C2$): $\kappa_{\mathrm{trk},C2} = 0.010\,{}^\circ\mathrm{C}^{-1} \implies F_{\mathrm{env}}^{\mathrm{lin}}(C2) = 1.0 + 0.010(15.0) = \mathbf{1.150}$.
2. **Nonlinear Cliff on Soft ($C4$):** Critical wear threshold $w_{\mathrm{crit},C4} = 0.250$ ($h_{\mathrm{crit},C4} = 0.750$), cliff amplitude $\beta_{C4} = 4.500\,\mathrm{s}$, cliff exponent $\eta_{C4} = 2.000$.
3. **Hand-Check of Soft ($S \mapsto C4$) Degradation Under Hot Conditions ($T_{\mathrm{trk}} = 50.0\,{}^\circ\mathrm{C}$):**
   * Per-lap wear rate of $S \mapsto C4$: $\Delta w_{C4} = d_{0,C4} \cdot F_{\mathrm{env}}^{\mathrm{lin}}(C4) = 0.080 \times 1.600 = \mathbf{0.128\,\mathrm{lap}^{-1}}$.
   * Lap $1$ ($m=1$): $w_1 = 0.000 \le 0.250 \implies \Delta T_{\mathrm{tyre},1} = 5.000(0.000) = 0.000\,\mathrm{s} \implies T_1 = 89.400\,\mathrm{s}$.
   * Lap $2$ ($m=2$): $w_2 = 0.128 \le 0.250 \implies \Delta T_{\mathrm{tyre},2} = 5.000(0.128) = 0.640\,\mathrm{s} \implies T_2 = 90.040\,\mathrm{s}$.
   * Lap $3$ ($m=3$): $w_3 = 0.256 > w_{\mathrm{crit},C4} = 0.250$! The tyre crosses the cliff threshold by $\bar{w}_3 - w_{\mathrm{crit}} = 0.006$, with denominator $1 - w_{\mathrm{crit}} = 0.750$, so normalized cliff excursion is $\frac{0.006}{0.750} = 0.008$:
     $$\Delta T_{\mathrm{tyre},3} = 5.000(0.256) + 4.500(0.008)^2 = 1.280 + 0.000288 = 1.280288\,\mathrm{s} \implies T_3 = 90.680288\,\mathrm{s}$$
   * Lap $4$ ($m=4$): $w_4 = 0.384 > 0.250$, so normalized cliff excursion is $\frac{0.384 - 0.250}{0.750} = \frac{0.134}{0.750} = 0.178\overline{6}$:
     $$\Delta T_{\mathrm{tyre},4} = 5.000(0.384) + 4.500\left(\frac{0.134}{0.750}\right)^2 = 1.920 + 4.500(0.0319218) = 1.920 + 0.143648 = 2.063648\,\mathrm{s} \implies T_4 = 91.463648\,\mathrm{s}$$
   * Thus the 4-lap Soft stint cost jumps from $360.000\,\mathrm{s}$ (at $35\,{}^\circ\mathrm{C}$) to $89.400 + 90.040 + 90.680288 + 91.463648 = \mathbf{361.583936\,\mathrm{s}}$ (at $50\,{}^\circ\mathrm{C}$). Meanwhile, shortening the Soft stint to $n_1 = 3\,\mathrm{laps}$ and extending Medium ($M \mapsto C3$, where $\Delta w_{C3} = 0.040 \times 1.300 = 0.052\,\mathrm{lap}^{-1}$ and $\alpha_{C3}\Delta w_{C3} = 0.260\,\mathrm{s\cdot lap}^{-1}$) to $n_2 = 7\,\mathrm{laps}$ gives:
     * Stint 1 ($S$, $3\,\mathrm{laps}$): $89.400 + 90.040 + 90.680288 = 270.120288\,\mathrm{s}$.
     * Stint 2 ($M$, $7\,\mathrm{laps}$): $7(90.000) + 0.260 \frac{7 \times 6}{2} = 630.000 + 5.460 = 635.460000\,\mathrm{s}$.
     * Total for $((S, 3), (M, 7))$ at $50\,{}^\circ\mathrm{C}$: $270.120288 + 635.460 + 20.000 = \mathbf{925.580288\,\mathrm{s}}$, whereas $((S, 4), (M, 6))$ costs $361.583936 + \left(6(90.000) + 0.260\frac{6 \times 5}{2}\right) + 20.000 = 361.583936 + 543.900 + 20.000 = \mathbf{925.483936\,\mathrm{s}}$, and $((S, 5), (M, 5))$ degrades sharply across the cliff on lap $5$ ($w_5 = 0.512 \implies \Delta T_{\mathrm{tyre},5} = 2.560 + 4.500(0.262/0.750)^2 = 3.109152\,\mathrm{s} \implies T_5 = 92.509152\,\mathrm{s}$), raising $((S, 5), (M, 5))$ to $454.093088 + 452.600 + 20.000 = \mathbf{926.693088\,\mathrm{s}}$!
   * **Demonstration of Strategy Shift:** At $35\,{}^\circ\mathrm{C}$ (Example 1A), $((S, 4), (M, 6))$ and $((S, 5), (M, 5))$ were tied at $923.000\,\mathrm{s}$. At $50\,{}^\circ\mathrm{C}$ with the Soft cliff (Example 2), $((S, 5), (M, 5))$ loses $+1.209\,\mathrm{s}$ relative to $((S, 4), (M, 6))$, shifting the optimal pit window earlier by a full lap!

### 12.3 Mandatory Mathematical and Software Verification Test Suite

Every C++ build of the simulator must pass the following ten automated verification tests before any empirical calibration or strategy study is trusted:

| Test ID | Verification Test Name | Input Setup / Boundary Condition | Exact Expected Mathematical Outcome |
| :--- | :--- | :--- | :--- |
| **`TEST_01`** | **Zero-Degradation & Equal-Pace Limiting Case** | Set $\Delta T_{\mathrm{comp},0}(c) = 0, d_{0,c} = 0, \gamma_{\mathrm{fuel}} = 0, A_{\mathrm{evol}} = 0, \Delta T_{\mathrm{start}} = 0, \Delta T_{\mathrm{out}} = 0, P_j = 20.0\,\mathrm{s}$. | Every lap $T_k = T_{\mathrm{ref}}$ exactly; every 1-stop strategy ($J=2$) has $T_{\mathrm{race}} = L \cdot T_{\mathrm{ref}} + 20.0\,\mathrm{s}$ regardless of pit lap $n_1 \in \{1, \ldots, L-1\}$; every 2-stop strategy costs $+20.0\,\mathrm{s}$ more. |
| **`TEST_02`** | **Zero Pit-Loss Limiting Case ($P_j = 0, \Delta T_{\mathrm{out}} = 0$)** | Set $P_j = 0.0\,\mathrm{s}$, sufficient tyre sets $A_r \ge L$, positive wear $\alpha_c d_{0,c} > 0$, and $K_{\min} = 2$. | Optimiser pits after every single lap ($n_j = 1$) to keep $w_k = 0.0$, selecting $L - 1$ laps on $S$ ($0$-age) and $1$ lap on $M$ ($0$-age). |
| **`TEST_03`** | **Role Mapping Bijection & Rejection Guard** | Pass duplicate mapping $m_e(S)=C4, m_e(M)=C4, m_e(H)=C2$ or un-nominated $m_e(S)=C5 \notin \mathcal{N}_e$. | Pre-simulation validator throws `InvalidRoleMappingException` before any lap evaluation executes. |
| **`TEST_04`** | **Non-Universal Role Mapping Invariance** | Evaluate $\pi = ((S, 4), (M, 6))$ on Event A where $(S,M)\mapsto(C4,C3)$ vs. $\pi' = ((M, 4), (H, 6))$ on Event B where $(M,H)\mapsto(C4,C3)$ under identical circuit/weather. | Predicted race times $T_{\mathrm{race}}(\pi; A)$ and $T_{\mathrm{race}}(\pi'; B)$ agree to machine precision ($< 10^{-12}\,\mathrm{s}$), proving role labels do not hardcode physics. |
| **`TEST_05`** | **Reference Environment Identity ($F_{\mathrm{env}} = 1.0$)** | Set $T_{\mathrm{trk},k} = 35.0\,{}^\circ\mathrm{C}, T_{\mathrm{air},k} = 25.0\,{}^\circ\mathrm{C}, \phi_k = 50.0\%$. | $F_{\mathrm{env}}(c, \mathbf{w}_{\mathrm{ref}}) = 1.000000000000$ for all $c \in \{C1..C5\}$. |
| **`TEST_06`** | **Humidity Neutrality Verification** | Vary $\phi_k$ from $10\%$ to $95\%$ while holding $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ constant under baseline parameters ($\psi_{\mathrm{hum},c} = 0$). | $\Delta h_{k,i}$ and $T_{\mathrm{race}}(\pi)$ remain completely invariant ($0.0\,\mathrm{s}$ change). |
| **`TEST_07`** | **Pit-Loss Non-Double-Counting Audit** | Compare 0-stop hypothetical ($J=1, K_{\min}=1$) vs. 1-stop ($J=2$) at zero tyre wear ($d_{0,c}=0$) and equal compound pace ($\Delta T_{\mathrm{comp}}=0$). | $T_{\mathrm{race}}(J=2) - T_{\mathrm{race}}(J=1) = P_1 + \Delta T_{\mathrm{out}}$ exactly (never $2 P_1$). |
| **`TEST_08`** | **State Isolation Across Strategy Evaluations** | Evaluate a high-wear strategy $\pi_{\mathrm{heavy}}$, then immediately evaluate $\pi_A$ on the same engine instance. | $T_{\mathrm{race}}(\pi_A)$ is identical to a fresh engine instance (zero state leakage across candidate evaluations). |
| **`TEST_09`** | **DAG Optimiser vs. Exhaustive Enumeration Oracle** | Run both Exhaustive Enumeration and Stint-Boundary DAG on $L = 15$, $P_{\max} = 3$ across 20 random parameter seeds (including Example 1A & Example 2). | Top-5 strategies $\big(\pi^{(1)}, \ldots, \pi^{(5)}\big)$ and their total times $T_{\mathrm{race}}$ match $100\%$ between DAG and Exhaustive Enumeration. |
| **`TEST_10`** | **C1 Continuity at Nonlinear Cliff Threshold** | Evaluate $\Delta T_{\mathrm{tyre}}(c, \bar{w})$ and numerical derivative at $\bar{w} = w_{\mathrm{crit},c} \pm 10^{-7}$ for $\eta_c = 2.0$. | Value jump $< 10^{-13}\,\mathrm{s}$ and first-derivative jump $< 10^{-6}\,\mathrm{s}$. |

---

## 13. Phased Implementation Roadmap and C++ Module Boundaries

### 13.1 Six-Stage Progression (M0 Through M5)

Development proceeds through six self-contained stages, where each stage is gated by the verification tests in Section 12.3 before advancing to the next:

| Stage | Milestone Title | Mathematical & Data Features Activated | Required Verification & Calibration Gate |
| :---: | :--- | :--- | :--- |
| **M0** | **Transparent Deterministic Baseline** | Reference pace $T_{\mathrm{ref}}(r, v)$, fuel mass penalty $\Delta T_{\mathrm{fuel},k}$, physical catalogue `C1`–`C5` with injective event role mapping $m_e$, aggregate scalar health $h_k \in [0, 1]$ with linear wear ($d_{0,c}$) and linear time penalty ($\alpha_c$), constant environment ($F_{\mathrm{env}} = 1$), exact pit loss $P_j$, and both Exhaustive Enumeration and Stint-Boundary DAG optimisers. | Pass `TEST_01`–`TEST_09` and reproduce Synthetic Example 1A ($923.000\,\mathrm{s}$) exactly. |
| **M1** | **Track-Aware Calibrated Dry Model** | Versioned `TrackLayoutVersion` and `TrackSurfaceProfile` schema; static stress index $S_{\mathrm{str}}(r)$ from lateral/braking/traction energies and surface abrasiveness; track evolution $\Delta T_{\mathrm{evol},k}$; offline calibration pipeline with confounder filtering on historical [FastF1](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) stints. | Out-of-sample clean-lap RMSE $< 0.35\,\mathrm{s}$ on held-out validation stints; zero negative wear slope estimates. |
| **M2** | **Dynamic Environmental Adjustment** | Lap-indexed `ScenarioEnvironmentSeries` ($\mathbf{w}_k$); bounded environmental multiplier $F_{\mathrm{env}}(c, \mathbf{w}_k)$ driven by $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ with neutral humidity ($\psi_{\mathrm{hum}} = 0$); radiative track-temperature estimator Eq. (8.2) for weather forecasts; $3\times 3$ scenario sensitivity grid. | Pass `TEST_05`, `TEST_06`, and reproduce Synthetic Example 2 ($925.483936\,\mathrm{s}$ for $((S,4),(M,6))$ at $50\,{}^\circ\mathrm{C}$). |
| **M3** | **Nonlinear Cliff & Four-Corner Tyre State** | Piecewise linear-plus-power-law cliff Eq. (7.6) ($\beta_c, w_{\mathrm{crit},c}, \eta_c$); independent four-corner health vector $\mathbf{h}_k \in [0, 1]^4$ (`FL`, `FR`, `RL`, `RR`) driven by clockwise/anticlockwise corner load share $\lambda_{r,i}$ and limiting-corner aggregation $\xi_{\mathrm{lim}}$. | Pass `TEST_10`; verify asymmetric wear on high-asymmetry circuits (e.g., Silverstone `FL`, Qatar `FL`). |
| **M4** | **Dynamic Tyre Thermal State** | Lumped-parameter bulk/surface tyre temperature recurrence $\boldsymbol{\theta}_{k+1}$ Eq. (7.7) and quadratic thermal working-window pace penalty Eq. (7.8); replaces empirical $F_{\mathrm{env}}$ temperature terms to prevent double-counting. | Bounded thermal stability test under extreme $T_{\mathrm{trk}} \in [15, 60]\,{}^\circ\mathrm{C}$; out-lap warm-up curve validation. |
| **M5** | **Advanced Race Dynamics Extensions** | Wet/Intermediate tyre compounds and surface water film $\omega_k$ dynamics; probabilistic Safety Car / VSC pit-loss discount states; multi-car traffic/overtaking delay model; active driver pace-management control $u_k$. | Stochastic Monte Carlo backtesting against historical mixed-weather and SC-interrupted Grands Prix. |

### 13.2 Recommended C++20 Module and Header Architecture

To keep data structures, mathematical evaluation, and optimisation cleanly decoupled without virtual-dispatch overhead in the inner lap loop, the C++20 implementation should be structured into six headers/modules:

1. `f1sim::schema` (`schema_types.hpp`): Immutable value structs `TrackLayoutVersion`, `TrackSurfaceProfile`, `PhysicalCompoundSpec`, `EventConfiguration`, `ScenarioEnvironmentSeries`, `VehicleSpecification`, `CalibratedModelParameters`, and `ProvenanceMetadata`, using `std::optional<double>` for nullable fields.
2. `f1sim::validation` (`validator.hpp`): Pure validation functions `validate_event_role_mapping(event_cfg, catalogue)` (enforcing the 4 invariants of Section 7.2), `validate_track_schema(track)`, and `validate_environment_series(env, L)`.
3. `f1sim::physics` (`tyre_and_lap_engine.hpp`): Stateless, deterministic pure functions `step_tyre_state(z_k, c, track, env_k, fuel_kg, params)` and `evaluate_flying_lap(k, c, z_k, theta_k, track, vehicle, env_k, params)`, plus `evaluate_stint_rollout(l_start, r_role, init_state, ...)`.
4. `f1sim::optimiser` (`strategy_optimiser.hpp`): Implements `precompute_stint_cost_table(...)`, `solve_dag_top_m_strategies(M)`, and `solve_exhaustive_oracle(...)`.
5. `f1sim::sensitivity` (`sensitivity_analyzer.hpp`): Computes local finite-difference partial derivatives $\nabla_{\boldsymbol{\Phi}} \Delta T_{1,2}$, crossover thresholds, and minimax regret across the scenario ensemble grid.
6. `f1sim::io` (`snapshot_io.hpp`): Offline JSON/SQLite snapshot reader/writer guaranteeing 100% offline reproducibility without live API calls during simulation.

#### Clarifying Pseudocode for the Stint-Boundary DAG Optimiser (`f1sim::optimiser`)

```text
ALGORITHM: SolveOptimalStrategyDAG
INPUT: EventConfig e, Track r, Vehicle v, EnvironmentSeries w[1..L], Catalogue C[C1..C5], Params P, TopCount M
OUTPUT: Ranked list of top M feasible strategies (pi^(1) .. pi^(M)) with lap-by-lap traces

1. VALIDATE m_e : {S, M, H} >--> N_e subset of {C1..C5} (Section 7.2 Invariants 1-4).
   IF invalid THEN RAISE InvalidRoleMappingException.

2. // Step 1: Forward Roll-Out Precomputation of Stint Cost Table in O(|R| * L^2) lap steps
   INITIALISE C_stint[0..L-1][1..L][S..H] := +infinity
   FOR each role r_role IN {S, M, H} DO
       c := m_e(r_role)
       FOR l_start := 0 TO L - 1 DO
           h := [1.0, 1.0, 1.0, 1.0]   // Brand-new tyre set at stint start
           cum_time := 0.0
           FOR l_end := l_start + 1 TO L DO
               k := l_end
               T_k := EvaluateFlyingLap(k, l_start, c, h, r, v, w[k], P)
               cum_time := cum_time + T_k
               h := StepTyreHealth(h, c, r, v, w[k], k, P)
               stint_len := l_end - l_start
               IF min(h) >= e.h_min AND e.n_min <= stint_len <= e.n_max(r_role) THEN
                   C_stint[l_start][l_end][r_role] := cum_time
               ELSE IF min(h) < e.h_min THEN
                   BREAK // Monotonic wear: longer stints from l_start are also infeasible

3. // Step 2: Dynamic Programming Relaxation over (lap l, stint_count j, role_mask beta)
   INITIALISE DP[0..L][0..P_max+1][0..7] as empty Top-M Min-Heaps
   INSERT (cost = 0.0, parent = NULL, counts = (0,0,0)) INTO DP[0][0][0]

   FOR j := 1 TO e.P_max + 1 DO
       FOR l_end := j TO L DO
           FOR l_start := j - 1 TO l_end - 1 DO
               pit_cost := (j >= 2) ? EvaluatePitLoss(l_start, r, P) : 0.0
               FOR each role r_role IN {S, M, H} DO
                   stint_cost := C_stint[l_start][l_end][r_role]
                   IF stint_cost == +infinity THEN CONTINUE
                   FOR mask := 0 TO 7 DO
                       FOR each candidate path p IN DP[l_start][j - 1][mask] DO
                           IF p.counts[r_role] + 1 > e.available_sets[r_role] THEN CONTINUE
                           new_mask := mask BITWISE_OR BitValue(r_role)
                           new_cost := p.cost + stint_cost + pit_cost
                           INSERT_TOP_M(DP[l_end][j][new_mask], new_cost, parent=(l_start, r_role, p))

4. // Step 3: Collect Feasible Terminal States at l_end = L
   INITIALISE GlobalTopM as Top-M Min-Heap
   FOR j := e.P_min + 1 TO e.P_max + 1 DO
       FOR mask := 0 TO 7 DO
           IF PopCount(mask) >= e.min_distinct_dry_compounds THEN
               MERGE DP[L][j][mask] INTO GlobalTopM (keeping top M)

5. BACKTRACK pointers in GlobalTopM to reconstruct full stint sequences and lap traces.
   RETURN GlobalTopM
```

---

## 14. Explicit Register of Assumptions, Unresolved Questions, and Model Risks

Every structural assumption and unresolved project question is logged below with its recommended default and falsification criteria:

| ID | Category | Explicit Assumption or Unresolved Question | Recommended Baseline Default | Risk / Condition Under Which Assumption Breaks Down |
| :--- | :--- | :--- | :--- | :--- |
| **`ASM_01`** | **Physics / Pace** | First-order additive separability of fuel load ($\Delta T_{\mathrm{fuel},k}$), track evolution ($\Delta T_{\mathrm{evol},k}$), compound pace ($\Delta T_{\mathrm{comp}}$), and tyre wear penalty ($\Delta T_{\mathrm{tyre},k}$). | Additive decomposition in Eq. (6.1), with fuel-wear interaction captured in $F_{\mathrm{usage}}(k)$. | High fuel load at extreme track temperatures can trigger nonlinear thermal blistering earlier than multiplicative $F_{\mathrm{usage}}$ predicts. |
| **`ASM_02`** | **Tyre Wear Scale** | Because physical tread depth in $\mathrm{mm}$ is unobservable in public data, normalized health $h \in [0, 1]$ is anchored by $d_{0,c} \equiv 1 / L_{\max,c}^{\mathrm{ref}}$. | Anchor $L_{\max}^{\mathrm{ref}}$ per compound ($C1: 62.5, C2: 50, C3: 35, C4: 25, C5: 18\,\mathrm{laps}$ at $5\,\mathrm{km}$). | If a circuit experiences severe graining rather than abrasive wear, lap time degrades without proportional tread-depth exhaustion. |
| **`ASM_03`** | **Environment / Humidity** | Relative humidity $\phi_k$ has no statistically identifiable direct causal effect on dry slick rubber abrasion once $T_{\mathrm{trk},k}$ and $T_{\mathrm{air},k}$ are controlled for. | Retain $\phi_k$ in schema; fix $\psi_{\mathrm{hum},c} \equiv 0.0$ (`ASSUMED_NEUTRAL`). | Indirectly affects dew-point condensation and rain onset; must be activated in Stage M5 wet-transition models. |
| **`ASM_04`** | **Race Operations** | Single-car undisturbed free-air simulation ($\Delta T_{\mathrm{other},k} = 0$). | Free-air optimisation in M0–M4, reporting top-$5$ strategies so human strategists can inspect traffic windows. | On narrow, high-aero-wake circuits (Monaco, Singapore, Hungary, Imola), track position and pit-exit traffic override pure free-air pace; pitting into a DRS train can cost $1.0\text{--}2.5\,\mathrm{s\cdot lap}^{-1}$. |
| **`ASM_05`** | **Race Operations** | Deterministic green-flag pit-stop cost $P_j$ without Safety Car / VSC arrivals. | Deterministic $P_j = P_{\mathrm{ref}}(r)$ with sensitivity grid over $P_j - 8.0\,\mathrm{s}$ (VSC discount). | A Virtual Safety Car or Safety Car reduces net pit loss by $8\text{--}12\,\mathrm{s}$, which can flip a 1-stop race into an opportunistic 2-stop race. |
| **`UNR_01`** | **Unresolved Question** | Should the C++ simulator support pre-scrubbed qualifying/FP tyre sets ($\mathbf{h}_{\mathrm{init}} < \mathbf{1}$) in Stage M0 or defer used-set inventory to Stage M1? | **Recommend including `initial_tyre_age_laps` and `initial_health` on each `AllocatedTyreSet` struct in M0**, as it requires zero change to the DAG complexity when sets are ordered by health. | If omitted in M0, simulating Q3 qualifiers who start the race or fit a 3-lap-old Soft set in Stint 3 will underestimate wear by 3 laps. |
| **`UNR_02`** | **Unresolved Question** | How should circuit-specific baseline pace $T_{\mathrm{ref}}(r, v)$ be initialised when simulating a brand-new season regulation change (e.g., 2026 active-aero regulations) before race 1? | **Recommend storing a dimensionless vehicle pace multiplier $\eta_{\mathrm{reg}}$** applied to prior-season $T_{\mathrm{ref}}$, updated immediately from pre-season testing / FP2 long runs. | Historical $T_{\mathrm{ref}}$ from 2022–2025 ground-effect cars will not match 2026 cornering/straight speed profiles without re-anchoring. |

---

## 15. External Data Sources, API Ingestion Architecture, and Coverage Caveats

To guarantee that the C++ strategy simulator never depends on live network calls during strategy evaluation, external data sources are accessed exclusively by an offline ingestion pipeline that normalizes, validates, and snapshots records into the local schema (Section 4).

### 15.1 Primary Data Sources: What They Provide and What They Do Not Provide

1. **[FastF1 Python Timing \& Telemetry Library](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) (and [FastF1 PyPI Package](https://www.google.com/search?q=https%3A%2F%2Fpypi.org%2Fproject%2Ffastf1%2F)):**
   * **What it provides:** Historical access (2018–present for full telemetry and weather; earlier seasons via Ergast legacy results) to official F1 live-timing streams, including lap times ($\pm 0.001\,\mathrm{s}$), sector times, pit-in and pit-out timestamps, stint numbers, event role labels (`SOFT`, `MEDIUM`, `HARD`, `INTERMEDIATE`, `WET`), reported tyre age (`TyreLife`), track status flags (`TrackStatus`), car telemetry channels ($v_{\mathrm{car}}$, throttle, brake, RPM, gear, DRS, $(X,Y,Z)$ track coordinates at $\sim 3.7\text{--}4\,\mathrm{Hz}$), and session-level weather sensors (`AirTemp`, `TrackTemp`, `Humidity`, `Pressure`, `WindSpeed`, `WindDirection`, `Rainfall` at $1\,\mathrm{min}$ intervals).
   * **What it does NOT provide:** It does **not** directly output the physical `C1`–`C5` compound code on each lap (only the event role `SOFT`/`MEDIUM`/`HARD`, which must be joined with Pirelli's event nomination table $\mathcal{N}_e$), does **not** provide car fuel loads or fuel flow rates, does **not** provide internal or surface tyre temperatures, and does **not** provide physical rubber wear depth or asphalt roughness measurements.
2. **[OpenF1 REST \& CSV API](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F) ([openf1.org](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2F)):**
   * **What it provides:** Open-source JSON/CSV endpoints (2023–present) for `laps`, `stints` (including `compound` role and `tyre_age_at_start`), `pit` (`pit_duration` in pit lane), `intervals` (real-time gap to leader and interval to car ahead, invaluable for filtering traffic-affected laps in Section 11.2!), `weather`, and `race_control` messages.
   * **What it does NOT provide:** Pre-2023 historical archives, physical `C1`–`C5` specifications without event-level mapping, fuel mass, or internal tyre states.
3. **[Open-Meteo Weather Forecast \& Historical ERA5 API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs) ([Historical Weather API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs%2Fhistorical-weather-api) & [Historical Forecast API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs%2Fhistorical-forecast-api)):**
   * **What it provides:** Global gridded historical reanalysis (ERA5 / IFS at $1\text{--}9\,\mathrm{km}$ spatial resolution, hourly or $15\,\mathrm{min}$ cadence) and multi-model ensembles for $2\,\mathrm{m}$ air temperature (`temperature_2m`), relative humidity (`relative_humidity_2m`), surface pressure (`surface_pressure`), $10\,\mathrm{m}$ wind speed/direction, cloud cover, and shortwave solar irradiance (`shortwave_radiation`).
   * **What it does NOT provide:** Direct circuit asphalt temperature $T_{\mathrm{trk}}$ or local micro-topography racing-line wetness $\omega_k$. Any track temperature derived from Open-Meteo must use Equation (8.2) and carry `measurement_semantics = FORECAST` or `REANALYSIS_GRID`.
4. **[FIA Formula 1 Sporting and Technical Regulations](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110) and [Pirelli Motorsport Formula 1 Catalogue](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1):**
   * **What they provide:** Official race lap counts $L$, circuit lap lengths $L_{\mathrm{lap}}$, pit-lane speed limits $v_{\mathrm{pit}}$, dry/sprint tyre set allocations ($13$ dry sets: $2H, 3M, 8S$ on standard weekends; $12$ dry sets: $2H, 4M, 6S$ on Sprint weekends), mandatory 2-compound dry race rules, and pre-event `C1`–`C5` nominations $\mathcal{N}_e$ plus qualitative $1\text{--}5$ circuit severity scores.
   * **What they do NOT provide:** Quantitative wear coefficients $d_{0,c}$, pace offsets $\Delta T_{\mathrm{comp},0}(c)$ across all circuits, or thermal degradation slopes $\kappa_{\mathrm{trk},c}$.

### 15.2 Offline Snapshot Normalisation and Reproducibility Contract

Every simulation run serialises a single immutable `SimulationInputSnapshot` bundle containing the exact versioned keys (`track_id`, `layout_version`, `surface_version`, `season_year`, `spec_version`, `event_id`, `scenario_id`, `vehicle_id`, `param_set_id`, and `simulator_math_version = "1.0.0"`). Under identical snapshot inputs, the C++ engine is guaranteed to be $100\%$ bitwise deterministic across platforms.

---

## 16. Reference Sources

* **FastF1 Python Package & Official Documentation:** [theOehrly/Fast-F1 GitHub Repository](https://www.google.com/search?q=https%3A%2F%2Fgithub.com%2FtheOehrly%2FFast-F1) and [FastF1 PyPI Package](https://www.google.com/search?q=https%3A%2F%2Fpypi.org%2Fproject%2Ffastf1%2F) — Primary source category for historical F1 timing, stint records, car telemetry, and trackside weather sensor streams.
* **OpenF1 Open-Source Formula 1 API:** [OpenF1 Portal](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2F) and [OpenF1 API Documentation](https://www.google.com/search?q=https%3A%2F%2Fopenf1.org%2Fdocs%2F) — Primary source category for interval headways, pit-lane durations, and JSON/CSV stint records (2023–present).
* **Open-Meteo Weather Forecast and Reanalysis APIs:** [Open-Meteo Weather Forecast API Documentation](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs), [Open-Meteo Historical Weather API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs%2Fhistorical-weather-api), and [Open-Meteo Historical Forecast API](https://www.google.com/search?q=https%3A%2F%2Fopen-meteo.com%2Fen%2Fdocs%2Fhistorical-forecast-api) — Primary source category for historical reanalysis and pre-race gridded meteorological forecasts.
* **FIA Sporting and Technical Regulations:** [FIA Formula One World Championship Regulations Repository](https://www.google.com/search?q=https%3A%2F%2Fwww.fia.com%2Fregulation%2Fcategory%2F110), [FIA 2025 Formula 1 Sporting Regulations](https://www.google.com/search?q=https%3A%2F%2Fapi.fia.com%2Fsystem%2Ffiles%2Fdocuments%2Ffia_2025_formula_1_sporting_regulations_-_issue_5_-_2025-04-30.pdf), and [FIA 2026 Formula 1 Sporting Regulations (Section B)](https://www.google.com/search?q=https%3A%2F%2Fapi.fia.com%2Fsystem%2Ffiles%2Fdocuments%2Ffia_2026_f1_regulations_-_section_b_sporting_-_iss03_-_2025-07-31.pdf) — Governing regulatory source for tyre allocations, mandatory compound usage, vehicle mass limits, and pit-lane rules.
* **Pirelli Motorsport F1 Tyre Compound Specifications:** [Pirelli Motorsport Formula 1 Compound Catalogue](https://www.google.com/search?q=https%3A%2F%2Fwww.pirelli.com%2Ftyres%2Fen-au%2Fmotorsport%2Fcar%2Fformula-1) and [Formula 1 Beginner's Guide to F1 Tyres](https://www.google.com/search?q=https%3A%2F%2Fwww.formula1.com%2Fen%2Flatest%2Farticle%2Fthe-beginners-guide-to-formula-1-tyres.61SvF0Kfg29UR2SPhakDqd) — Primary source for the `C1`–`C5` physical compound hierarchy, event nomination conventions, and weekend tyre allocation rules.
