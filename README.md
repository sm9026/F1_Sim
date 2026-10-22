# F1_Sim — Formula 1 Race Strategy Simulator

`F1_Sim` is a deterministic, data-driven C++17 Formula 1 single-car race strategy simulator combining a read-only local Data Access Layer (`f1_sim::data_access`) and a stateful/stateless race simulation engine (`f1_sim::simulation`).

## Architecture & Documentation

- **Mathematical Framework:** `docs/f1_simulator_mathematical_framework.md`
- **Data Access Layer (DAL) Guide:** `docs/DAL_Usage_and_Architecture.md`
- **Simulation Engine Design Decisions:** `docs/Simulation_Engine_Implementation_and_Decisions.md`
- **Program-Wide Reference Blueprint:** `docs/architecture/program_blueprint.md`

## Building & Running (CMake)

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the CLI entry point (`f1_sim`):

```bash
# Display CLI usage and exit codes
./build/f1_sim --help

# Run the default 2026 British Grand Prix (Silverstone, 52 laps) smoke-test scenario
./build/f1_sim

# Run with an explicit local dataset directory
./build/f1_sim --data-root data

# Inspect raw dataset and capability readiness without running a simulation
./build/f1_sim --data-root data --check-only
```

## Building & Running (GNU Make)

On Linux/macOS environments with `make` and `g++`:

```bash
make clean && make all
make test
./build/f1_sim --help
./build/f1_sim --data-root data
```
