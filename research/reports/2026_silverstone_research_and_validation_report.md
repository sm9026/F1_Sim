# 2026 Silverstone Circuit (British Grand Prix) — Track & Event Data-Mining Report

## 1. Scope Completed and Target Event Verification

* **Target Circuit:** Silverstone Circuit (`track_id = "silverstone_gp"`), Towcester / Silverstone, Northamptonshire, Great Britain (`GBR`).
* **Target Season & Official Event:** `2026` FIA Formula One World Championship, Round 9 — **FORMULA 1 PIRELLI BRITISH GRAND PRIX 2026** (`event_id = "2026_british_gp"`), held **03–05 July 2026** ([Formula1.com Race Result](https://www.formula1.com/en/results/2026/races/1289/great-britain/race-result); [Pirelli Preview](https://press.pirelli.com/the-challenges-of-silverstone-the-cradle-of-motorsport/)).
* **Competition Format Discovery:** Unlike standard Grand Prix weekends, the 2026 British Grand Prix was officially contested under the **Alternative Format Competition (Sprint)** schedule ([Pirelli Nomination Release](https://press.pirelli.com/the-full-pirelli-range-for-spielberg-and-silverstone/); [Formula1.com Sprint Results](https://www.formula1.com/en/results/2026/races/1289/great-britain/sprint-results)). Under [FIA 2026 Sporting Regulations Section B](https://www.fia.com/system/files/documents/fia_2026_f1_regulations_-_section_b_sporting_-_iss_05_-_2026-02-27.pdf) Article B6.2.4, this alters the per-driver dry-weather tyre allocation from the standard 13 sets (`2H, 3M, 8S`) to **12 sets (`2 Hard, 4 Medium, 6 Soft`)**, verified directly against [Formula1.com's 2026 British GP Tyre Allocation Article](https://www.formula1.com/en/latest/article/what-tyres-will-the-teams-and-drivers-have-for-the-2026-british-grand-prix.3qD9d5o8X4x3se0F7Zg5i1).
* **Task Boundary Enforcement:** In accordance with the task scope, no historical lap-by-lap timing or car telemetry was mined, no `HistoricalLapDataset` was generated, and no empirical model parameters were calibrated.

## 2. Repository Inspection Status and Fallback Path Plan

* **Repository URL Inspected:** `https://github.com/sm9026/F1_Sim.git` (`https://github.com/sm9026/F1_Sim`).
* **Inspection Outcome:** **FAILED / INACCESSIBLE (`UNVERIFIED`).** Read-only inspection was attempted via `google:search` (0 indexed results for `sm9026/F1_Sim`) and `google:browse` on `https://github.com/sm9026/F1_Sim` (which returned an access/unavailability error, indicating the repository is either private, not yet pushed, or restricted). Outbound `git clone` from the local VM sandbox is disabled by environment policy.
* **Compatibility & Delivery Consequence:** C++ struct/loader path compatibility is explicitly marked **`UNVERIFIED`**. No local edits, commits, or pushes to the user's repository were performed or claimed. All artifacts are organised under the documented fallback directory hierarchy specified in `mining_guidlines.md` Section 10.3:
  * `data/tracks/silverstone_gp_layout_v2011.json` (`CREATED`)
  * `data/tracks/silverstone_gp_surface_v2019.json` (`CREATED`)
  * `data/events/2026_british_gp_config.json` (`CREATED`)
  * `data/events/2026_british_gp_role_mapping.json` (`CREATED`)
  * `data/compounds/2026_pirelli_slick_catalogue_v1.json` (`CREATED`)
  * `data/sources.md` (`CREATED`)
  * `data/reports/2026_silverstone_research_and_validation_report.md` (`CREATED`)
  * `data/batch_manifest.json` (`CREATED`)

## 3. Specification Conflict & Schema Separation Resolution

* **Identified Specification Nuance:** In `f1_simulator_mathematical_framework.md`, Section 4.2 defines `TrackLayoutVersion` (1) $\longrightarrow$ (M) `TrackSurfaceProfile` as separate entities, whereas Table 4.3 and the synthetic example in Section 4.8 display layout and surface fields inside a single combined table/object. Meanwhile, the task prompt and `mining_guidlines.md` Section 7.1 & 10.2 mandate delivering `TrackLayoutVersion` and `TrackSurfaceProfile` as separate versioned objects so resurfacing epochs do not duplicate or overwrite layout geometry.
* **Explicit Resolution:** The dataset delivers two separate, foreign-key-linked JSON files (`silverstone_gp_layout_v2011.json` and `silverstone_gp_surface_v2019.json`) using the exact field names, types, units, and legal ranges of Table 4.3, linked via `("track_id": "silverstone_gp", "layout_version": 2011)`.

## 4. Field-by-Field Data Status Summary

| Schema Object | Field(s) | Stored Value & Unit | Epistemological Status (`source_status`) | Confidence Tier | Primary Source ID |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `TrackLayoutVersion` | `track_id`, `layout_version`, `track_name`, `country`, `locality` | `"silverstone_gp"`, `2011`, `"Silverstone Circuit"`, `"GBR"` | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_F1_CIRCUIT_GUIDE_2026`, `SRC_PIRELLI_GB26_PREVIEW` |
| `TrackLayoutVersion` | `latitude_deg`, `longitude_deg`, `altitude_msl_m` | `52.0711` deg, `-1.0161` deg, `153.0` m | `PUBLISHED_OBSERVED` | `MEDIUM` | `SRC_WIKI_SILVERSTONE_HELIPORT_AIP` |
| `TrackLayoutVersion` | `lap_length_m`, `race_direction` | `5891.0` m, `"CLOCKWISE"` | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_PIRELLI_GB26_PREVIEW`, `SRC_F1_CIRCUIT_GUIDE_2026` |
| `TrackLayoutVersion` | `corners_total_count`, `corners_left_count`, `corners_right_count` | `18` total (`8` left, `10` right) | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_PIRELLI_GB26_PREVIEW`, `SRC_F1_2026_GB_TYRES_ARTICLE` |
| `TrackLayoutVersion` | `pit_speed_limit_kmh` | `80.0` km/h | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_FIA_2026_SPORTING_B_ISS05` (Art. B1.6.3a) |
| `TrackLayoutVersion` | `pit_lane_length_m` | `457.0` m ($\pm 25.0$ m) | `ESTIMATED` | `LOW` | `SRC_SECONDARY_PIT_LENGTH` |
| `TrackLayoutVersion` | `sector1_length_m`, `sector2_length_m`, `sector3_length_m` | `null` | `UNKNOWN_UNAVAILABLE` | `PROVISIONAL` | Deferred to FastF1 telemetry task (`DEGRADE_TO_LOWER_STAGE`) |
| `TrackLayoutVersion` | `frac_lap_straight`, `frac_lap_corner_slow`, `med`, `high` | `null` | `UNKNOWN_UNAVAILABLE` | `PROVISIONAL` | Deferred to FastF1 telemetry task (`DEGRADE_TO_LOWER_STAGE`) |
| `TrackLayoutVersion` | `lateral_energy_index_norm`, `braking_energy_index_norm`, `traction_energy_index_norm` | `1.50`, `0.75`, `1.00` (dimensionless) | `ESTIMATED` | `LOW` | `SRC_PIRELLI_SCORECARD_2026` |
| `TrackSurfaceProfile` | `surface_version`, `valid_from_date`, `valid_to_date` | `2019`, `"2019-08-22"`, `null` | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_SILVERSTONE_RESURFACING_2019` |
| `TrackSurfaceProfile` | `surface_abrasiveness_score`, `surface_grip_baseline_score` | `2.0` ($[1,5]$), `4.0` ($[1,5]$) | `ESTIMATED` | `MEDIUM` | `SRC_PIRELLI_GB26_PREVIEW`, `SRC_PIRELLI_SCORECARD_2026` |
| `TrackSurfaceProfile` | `bumpiness_index_score` | `null` | `UNKNOWN_UNAVAILABLE` | `PROVISIONAL` | Not scored numerically by Pirelli (`USE_DECLARED_DEFAULT`) |
| `TrackSurfaceProfile` | `track_evolution_amplitude_s`, `track_evolution_decay_laps` | `0.0` s, `20.0` laps | `ASSUMED` | `PROVISIONAL` | `SRC_FRAMEWORK_DEFAULT_M0` (awaiting calibration) |
| `EventConfiguration` | `event_id`, `season_year`, `race_distance_laps` | `"2026_british_gp"`, `2026`, `52` laps | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_F1_2026_GB_RACE_RESULT` |
| `EventConfiguration` | `nominated_compounds_set`, `min_distinct_dry_compounds` | `["C1", "C2", "C3"]`, `2` | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_PIRELLI_AT_GB_NOMINATIONS_2026`, `SRC_FIA_2026_SPORTING_B_ISS05` |
| `EventConfiguration` | `available_sets_hard`, `available_sets_med`, `available_sets_soft` | `2` Hard, `4` Medium, `6` Soft (Sprint allocation) | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_FIA_2026_SPORTING_B_ISS05` (Art. B6.2.4), `SRC_F1_2026_GB_TYRES_ARTICLE` |
| `EventRoleMapping` | `HARD`, `MEDIUM`, `SOFT` $\mapsto$ `compound_id` | `HARD->C1`, `MEDIUM->C2`, `SOFT->C3` | `PUBLISHED_OBSERVED` | `HIGH` | `SRC_F1_2026_GB_TYRES_ARTICLE`, `SRC_PIRELLI_GB26_PREVIEW` |
| `PhysicalCompoundCatalogue` | `C1`..`C5` `hardness_rank` ($1..5$), `C3` `pace_offset_ref_s` ($0.0$ s) | Ranks `1..5`, `C3` offset `0.0` s; other fits `null` | `PUBLISHED_OBSERVED` / `UNKNOWN_UNAVAILABLE` | `HIGH` (IDs) / `PROVISIONAL` (fits) | `SRC_PIRELLI_2026_RANGE_REVEAL` |

## 5. Missing Parameters, Resolution Attempts, and Fallback Decisions

1. **Sector Lengths (`sector1_length_m`, `sector2_length_m`, `sector3_length_m`) and Lap Speed-Band Fractions (`frac_lap_*`):**
   * *Sources Checked:* [Formula1.com 2026 Silverstone Circuit Guide](https://www.formula1.com/en/latest/article/circuit-guide-everything-you-need-to-know-about-silverstone-2026.5Sl0O8g393enBWVIkjRzOr), [Pirelli 2026 Silverstone Preview](https://press.pirelli.com/the-challenges-of-silverstone-the-cradle-of-motorsport/), and [FIA 2026 Sporting Regulations](https://www.fia.com/regulation/category/110).
   * *Why Unresolved:* Official text documents report total circuit length ($5891.0\,\mathrm{m}$) and corner counts ($18$), but do not publish timing-loop sector lengths in metres or telemetry speed-band fractions. Because mining historical telemetry is out of scope for this task, both groups are preserved as `null` (`UNKNOWN_UNAVAILABLE`) with `missing_policy = "DEGRADE_TO_LOWER_STAGE"`.
   * *Downstream Impact:* **Does not block Stages M0–M2** (which consume full-lap length $L_{\mathrm{lap}} = 5891.0\,\mathrm{m}$ and static stress indices).
2. **Pit-Lane Speed-Restricted Length (`pit_lane_length_m`):**
   * *Sources Checked:* FIA Regulations repository and secondary circuit guides. The event-specific FIA Race Director's Event Notes (`Doc 7 - Circuit Map and Pit Lane Drawing`) for the 2026 British GP was not indexed as a downloadable PDF in search results.
   * *Fallback Decision:* Because `pit_lane_length_m` is typed as non-null `DOUBLE` in Table 4.3 with `missing_policy = "USE_DECLARED_DEFAULT"` (use calibrated $P_j$), the secondary measurement `457.0` $\mathrm{m}$ ([Formulapedia](https://formulapedia.com/pit-lane-in-f1/)) is stored with `source_status = "ESTIMATED"`, `confidence_tier = "LOW"`, and $\sigma = 25.0\,\mathrm{m}$, alongside Pirelli's published net pit-loss figure of $pprox 20.0\,\mathrm{s}$ ([CCM Strategy Report](https://coffeecornermotorsport.com/f1-british-grand-prix-2026-tyre-strategy/)).
3. **Uncalibrated Compound Catalogue Coefficients (`pace_offset_ref_s`, `base_wear_rate_per_lap`, `linear_deg_coeff_s`) and `CalibratedModelParameters`:**
   * *Why Unresolved:* The task prompt explicitly instructs: *"Do NOT mine/process detailed historical lap-by-lap timing, full telemetry, or create a historical lap dataset; do NOT calibrate model parameters. Leave deep historical data analysis and calibration for a separate later task."*
   * *Fallback Decision:* In `2026_pirelli_slick_catalogue_v1.json`, `pace_offset_ref_s` (for $c 
eq C3$), `base_wear_rate_per_lap`, and `linear_deg_coeff_s` are preserved as `null` (`UNKNOWN_UNAVAILABLE`). The simulator can immediately execute configuration/schema validation and synthetic test suites, while real-world 2026 Silverstone lap-time prediction awaits the next task (historical timing extraction and parameter calibration).

## 6. Source Conflicts Identified and Resolutions

1. **Surface Abrasiveness Score (`2.0` Real Pirelli Evidence vs. `3.8` Framework Synthetic Example):**
   * *Conflict:* Section 4.8 of `f1_simulator_mathematical_framework.md` used `"surface_abrasiveness_score": 3.8` in an explicitly labelled synthetic JSON illustration, whereas [Pirelli's 30 June 2026 British GP Preview](https://press.pirelli.com/the-challenges-of-silverstone-the-cradle-of-motorsport/) states: *"The track surface is not abrasive and has relatively low roughness"*, corresponding to `2.0` ($2/5$) on Pirelli's preview graphic ([Formula1 Report](https://note.com/formula1report/n/n8d9bf26545da?hl=en)).
   * *Resolution:* Following `mining_guidlines.md` Rule 2.3 and Section 7.2 (*"Do not treat synthetic values in the mathematical framework as real-world data"*), the real Pirelli assessment (`2.0`) is stored. Silverstone's high tyre demand comes from high-speed lateral load ($>5g$, rated $5/5$), not surface roughness!
2. **Layout Version Year (`2010` Arena Loop Introduction vs. `2011` Wing Start/Finish & Pit Configuration):**
   * *Conflict:* [Pirelli's 2026 Preview](https://press.pirelli.com/the-challenges-of-silverstone-the-cradle-of-motorsport/) states that the last major layout revision occurred in `2010` (when the Arena section between Abbey and Brooklands was introduced at $5.901\,\mathrm{km}$), whereas [Wikipedia](https://en.wikipedia.org/wiki/Silverstone_Circuit) and FIA records show the current `5.891 km` (`5891.0 m`) layout with the Wing pit lane and Hamilton Straight start/finish line debuted in May `2011`.
   * *Resolution:* Because start/finish line location, sector boundaries, pit-lane geometry, and exact lap length ($5891.0\,\mathrm{m}$ vs. $5901.0\,\mathrm{m}$) changed in 2011, `layout_version = 2011` (`valid_from_date = "2011-05-17"`) is selected as the canonical version key.
3. **Weekend Tyre Set Allocation (`12` Sets Sprint Format vs. `13` Sets Standard Format vs. Sunday Remaining Sets):**
   * *Conflict:* Standard weekends allocate `13` sets (`2H, 3M, 8S`), whereas the 2026 British GP was an Alternative Format (Sprint) weekend allocating `12` sets (`2H, 4M, 6S`) under [FIA Section B Art. B6.2.4](https://www.fia.com/system/files/documents/fia_2026_f1_regulations_-_section_b_sporting_-_iss_05_-_2026-02-27.pdf). Furthermore, by Sunday morning (05 July 2026), the top-7 qualifiers had consumed sets in FP1, SQ1–SQ3, Sprint, and Q1–Q3, leaving `1 new Hard, 1 new Medium, and 3 used Softs` ([CCM Strategy Report](https://coffeecornermotorsport.com/f1-british-grand-prix-2026-tyre-strategy/)).
   * *Resolution:* `EventConfiguration` stores the official FIA Sprint weekend allocation (`available_sets_hard = 2`, `available_sets_med = 4`, `available_sets_soft = 6`) and records the exact pre-Sunday remaining allocation (`1 new H, 1 new M, 3 used S`) in `typical_pre_race_sunday_remaining_sets_note` so both full-weekend and Sunday-only simulations are supported.

## 7. Validation Performed vs. Not Run

* **Executed Programmatic Checks (`PASS`):**
  1. Strict JSON parsing (`json.loads`) across all 6 JSON files (`5` data files + `batch_manifest.json`).
  2. Required schema keys, data types, and permitted nullability against `f1_simulator_mathematical_framework.md` Section 4.
  3. Unit and numerical range validation (`lap_length_m = 5891.0` $\in [3000, 8000]$, `race_distance_laps = 52` $\in [1, 100]$, `pit_speed_limit_kmh = 80.0` $\in [60, 80]$, `corners_left_count + corners_right_count == corners_total_count` $\implies 8 + 10 = 18$).
  4. Cross-file foreign-key and version consistency (`track_id = "silverstone_gp"`, `layout_version = 2011`, `surface_version = 2019`, `event_id = "2026_british_gp"`, `season_year = 2026`, `spec_version = 1`).
  5. Mathematical validation of `EventRoleMapping` invariants (Section 7.2): exact bijection between `{"HARD", "MEDIUM", "SOFT"}` and `nominated_compounds_set = ["C1", "C2", "C3"]`, with strict hardness monotonicity $ho(C1)=1 < ho(C2)=2 < ho(C3)=3$.
  6. Strict separation of `TrackLayoutVersion` and `TrackSurfaceProfile` into distinct versioned files, with zero fixed lap times or degradation curves inside track data.
  7. Zero-imputation audit confirming no unknown field was silently set to `0.0`.
* **Checks Marked `NOT RUN`:**
  1. Compilation and ingestion test against the C++ loader in `https://github.com/sm9026/F1_Sim.git` (`NOT RUN` — repository inaccessible).
  2. Sector-length sum check $\sum_{s=1}^3 L_s = L_{\mathrm{lap}} \pm 5\,\mathrm{m}$ (`NOT RUN` — sector lengths preserved as `null` until telemetry extraction).

## 8. Sensitivity Priorities and Recommended Next Task

* **Key Uncertainties Affecting Optimal Strategy at Silverstone:**
  1. **Compound Degradation Slopes ($lpha_c d_{0,c}$) and C2–C3 Pace Delta ($\Delta T_{\mathrm{comp}}$):** In 2026 Friday practice at Silverstone, Pirelli observed a Medium-to-Soft equal-fuel pace delta of $pprox 0.4\,\mathrm{s\cdot lap}^{-1}$ and lower-than-expected thermal degradation on `C2` Medium during Saturday's 17-lap Sprint, making the 1-stop (`Medium -> Hard` stopping laps 24–30) $pprox 13\,\mathrm{s}$ faster than a 2-stop in undisturbed dry conditions ([CCM Strategy Report](https://coffeecornermotorsport.com/f1-british-grand-prix-2026-tyre-strategy/)).
  2. **Net Pit-Stop Time Loss ($P_{\mathrm{ref}} pprox 20.0\,\mathrm{s}$) and Safety Car Probability ($78\%$):** Because Silverstone's net pit loss is relatively short ($pprox 20.0\,\mathrm{s}$), a Safety Car or Virtual Safety Car discount ($pprox 8	ext{--}10\,\mathrm{s}$) shifts the 1-stop vs. opportunistic 2-stop crossover margin dramatically.
* **Recommended Next Task:** Proceed to **Task 2 — Historical Timing/Telemetry Extraction and Model Calibration** using FastF1/OpenF1 data from the 2026 British Grand Prix (FP1, Sprint, and Race) to:
  1. Extract telemetry-derived `sector1_length_m`–`sector3_length_m` and 2026 Straight-Mode speed-band fractions `frac_lap_*`;
  2. Calibrate `2026_pirelli_slick_catalogue_v1.json` (`pace_offset_ref_s`, `base_wear_rate_per_lap`, `linear_deg_coeff_s` for `C1`, `C2`, `C3`) and `CalibratedModelParameters` (`ref_lap_time_s`, `pit_loss_total_s`) for Silverstone.
