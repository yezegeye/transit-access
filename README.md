# transit-access

A C++ public transit router with Python bindings, used to compute
transit accessibility metrics (e.g. jobs reachable within N minutes) from GTFS data.

- `cpp/` — routing core (static library `router`) and pybind11 bindings (`transit_router`)
- `cpp/tests/` — Catch2 unit tests
- `python/tests/` — pytest tests against the compiled module
- `data/raw/` — downloaded GTFS feeds (git-ignored)

## Setup

Requires CMake ≥ 3.18 and a C++17 compiler.

```sh
conda env create -f environment.yml
conda activate transit
```

## Build

```sh
cmake -S . -B build \
  -DPython_EXECUTABLE="$(which python)" \
  -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build build -j
```

The Python module is written to `build/`; the root `conftest.py` puts that
directory on `sys.path` for tests.

## Test

```sh
ctest --test-dir build --output-on-failure   # C++
python -m pytest                             # Python (run inside the transit env)
```
