# F1_Sim — Formula 1 Race Strategy Simulator

`F1_Sim` is a deterministic, data-driven C++17 Formula 1 single-car race strategy simulator combining a read-only local Data Access Layer (`f1_sim::data_access`), a stateful/stateless race simulation engine (`f1_sim::simulation`), and an interactive menu-driven command-line application (`f1_sim::cli`).

## Architecture & Documentation

* **Mathematical Framework:** `docs/f1_simulator_mathematical_framework.md`

* **Data Access Layer (DAL) Guide:** `docs/DAL_Usage_and_Architecture.md`

* **Simulation Engine Design Decisions:** `docs/Simulation_Engine_Implementation_and_Decisions.md`

* **Program-Wide Reference Blueprint:** `docs/architecture/program_blueprint.md`

## Building & Running (Windows PowerShell — MSYS2 UCRT64 GCC)

From the repository root in Windows PowerShell using `C:\msys64\ucrt64\bin\g++.exe`:

```
# 1. Configure build-ucrt64 (if not already configured) and build all targets
cmake -S . -B build-ucrt64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe"
cmake --build build-ucrt64

# 2. Run all automated CTest verification suites (DAL + Simulation Engine + Interactive CLI)
ctest --test-dir build-ucrt64 --output-on-failure

# 3. Display CLI usage instructions and exit codes
.\build-ucrt64\f1_sim.exe --help

# 4. Inspect raw local dataset and calibration readiness without running a race
.\build-ucrt64\f1_sim.exe --data-root data --check-only

# 5. Execute the deterministic 2026 British GP (Silverstone, 52 laps, Soft 18 / Hard 34) smoke test
.\build-ucrt64\f1_sim.exe --data-root data --smoke-test

# 6. Launch the interactive menu-driven CLI application
.\build-ucrt64\f1_sim.exe

```

## Building & Running (GNU Make / CMake on Linux & macOS)

```
make clean && make all
make test
./build/f1_sim --help
./build/f1_sim --data-root data --check-only
./build/f1_sim --data-root data --smoke-test
./build/f1_sim --data-root data

```

## Interactive CLI User Guide

### 1. Command-Line Arguments & Initial Preselection

When launched without `--smoke-test`, `--check-only`, or `--help`, `f1_sim` opens the interactive menu-driven CLI. Any configuration flags passed on the command line are applied as initial session settings:

| Option | Description | Behaviour Without `--smoke-test` | 
| ----- | ----- | ----- | 
| `--help`, `-h` | Display CLI usage information and exit codes. | Prints help and exits (`0`). | 
| `--check-only` | Load dataset via DAL, report raw calibration readiness and parameter gaps, and exit without running a race. | Prints audit report and exits (`0` or `2`). | 
| `--smoke-test` | Run the deterministic Silverstone 52-lap (`Soft 18 / Hard 34`) smoke-test scenario using declared lower-fidelity (`ESTIMATED`) parameters. | Runs race non-interactively and exits (`0`, or `3` with `--strict-calibrated`). | 
| `--data-root <path>` | Specify the local read-only runtime dataset directory (`data/`). | Sets the initial DAL `data_root`. | 
| `--event <event_id>` | Preselect the initial event identifier (`default: 2026_british_gp`). | Preloads `<event_id>` into `RaceSession`. | 
| `--mode <preplanned|interactive>` | Preselect the simulation operating mode (`default: preplanned`). | Sets the initial `RaceMode` in the interactive CLI. | 
| `--stage <m0|m1|m2|m3>` | Preselect the mathematical model stage (`default: m1`). | Sets the initial `ModelStage` in the interactive CLI. | 
| `--driver-mass <kg>` | Configure driver mass in `kg` within `[40.0, 160.0]` (`default: 75.0`). | Sets initial driver mass in `VehicleConfig`. | 
| `--strict-calibrated` | Require strictly `CALIBRATED` parameters (`allow_estimated_or_assumed_parameters = false`). | Starts interactive CLI with the strict calibration gate enabled. | 
| `--verbose-laps` | Print all lap trace entries instead of milestone laps. | Enables full lap-by-lap trace output in result reports. | 

### 2. Main Menu & Navigation

At the top of the Main Menu, `f1_sim` displays a live session configuration summary (data root, selected event and circuit, role mapping, 7-set tyre inventory ages and starting health, committed strategy, race settings, and readiness gate):

1. **Select event / track:** Discovers events genuinely available in `data/events/` via `DataAccessLayer::listAvailableEventIds()` and `loadEventBundle()`, reports the exact number of available valid events, displays circuit/lap/format/role-mapping details, and asks for user confirmation (`y/n`) before switching events because `RaceSession::selectEvent()` resets all 7 tyre sets' configured ages to `0`.

2. **Configure tyre inventory:** Displays the 7 physical tyre sets (`S1, S2, S3, M1, M2, H1, H2`), their event-mapped Pirelli compound (`C1`–`C5`), configured prior age (`[0, 100]` laps), dynamically derived starting health, and readiness status. Users may edit prior age or reset all sets to `0`, but **can never directly enter or override tyre health**. Unavailable health is displayed as `UNAVAILABLE (uncalibrated wear rate)`—never as `0%`.

3. **Build or edit race strategy:** Create a multi-stint strategy from scratch, or append, edit, remove, and clear stints. For each stint, specify the tyre role (`Soft`, `Medium`, `Hard`), physical set ID (`S1`–`H2` or `AUTO` deterministic allocation), and target laps. Previews mapped compounds, starting health, and pit-lap boundaries, and validates via `StrategyAndReadinessValidator::validateInitialStrategy`.

4. **Configure race settings:** Configure `PREPLANNED` vs. `INTERACTIVE` mode, model stage (`M0`–`M3`), driver mass (`[40.0, 160.0] kg`), strict vs. lower-fidelity calibration policy, and verbose lap output.

5. **Inspect data readiness and model assumptions:** Shows raw on-disk dataset readiness alongside active session fidelity, listing all uncalibrated (`null`) fields and declared `ESTIMATED` parameter supplements.

6. **Start simulation:** Shows a concise pre-race configuration summary and requests explicit user confirmation (`y/n`) whenever execution is permitted only at lower fidelity (`ALLOWED_LOWER_FIDELITY`). Supports both **Automatic progression** and **Step-by-step / Pauseable lap control** (`advanceLap`, `pause`, `resume`, live `SessionSnapshot` inspection, and `cancel`).

7. **Exit.**

### 3. Interactive Race Decisions & Repeated Simulations

* **Genuine Interactive Decisions (`SessionState::DecisionRequired`):** In `INTERACTIVE` mode, when the engine pauses at a planned stint boundary or mandatory tyre-health stop (`mean < 30%` or `min_corner < 12%`), the CLI displays current lap, stint, tyre health, fuel, elapsed time, and remaining stints, and offers:

  1. **Keep original plan (`KeepOriginalPlan`)**

  2. **Extend current stint (`ExtendCurrentStint`):** Offered only when `extension_permitted == true` (never during a mandatory health stop). Prompts for **additional laps** (`> 0`) and revised remaining stints without silently subtracting extension laps from future stints.

  3. **Pit now with a revised plan (`PitNowWithRevisedPlan`):** Prompts for revised remaining stints and eligible unused tyre sets summing to `remaining_race_laps` (no silent shortfall cascade on mandatory stops in Interactive mode).

  4. **Cancel active race (`cancel()`)**

* **Post-Race Repeated Simulations (A/B Comparison):** After each race report, choose to (1) run another simulation immediately with the same event, configured tyre ages, and strategy, (2) edit strategy, (3) edit tyre ages, (4) change event, (5) return to main menu, or (6) exit. Runtime tyre wear and consumption flags automatically reset after every run while configured pre-race ages persist.