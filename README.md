# transit-access

A C++ public transit router with Python bindings, used to compute
transit accessibility metrics (e.g. jobs reachable within N minutes) from GTFS data.

## Status

**Works now**

- GTFS loading from `stops.txt` and `stop_times.txt`: header-based column
  lookup, quoted fields, UTF-8 BOM, CRLF line endings, times past 24:00:00.
- One-to-all earliest-arrival routing with the
  [Connection Scan Algorithm](https://arxiv.org/abs/1703.05997): given a source
  stop and a departure time, it returns the earliest arrival at every reachable stop.
- Python bindings, with Catch2 and pytest suites run against a tiny hand-made
  feed (`data/fixtures/tiny`), on every push via GitHub Actions.

**Current limitations**

- Transfers are instantaneous and there are no walking links between stops
  (`transfers.txt` and footpaths are ignored).
- No service calendar: every trip in `stop_times.txt` is assumed to run.
- Stop times without an arrival/departure time (non-timepoints) are rejected
  rather than interpolated.

**Next**

1. Job-access counts per census tract: jobs reachable within N minutes of
   each tract (LEHD LODES workplace data), routing from stops near each tract.
2. Income regression: how access relates to tract median household income
   (ACS).
3. Maps of access by tract.

Routing pieces needed first: walking access/egress and transfers, `calendar.txt`
service days, and routing from many origins at many departure times.

## Layout

- `cpp/` — routing core (static library `router`) and pybind11 bindings (`transit_router`)
- `cpp/tests/` — Catch2 unit tests
- `python/tests/` — pytest tests against the compiled module
- `data/fixtures/` — small hand-made GTFS feeds used by the tests
- `data/raw/` — downloaded GTFS feeds (git-ignored)

## Usage

```python
import transit_router as tr

tt = tr.Timetable.from_gtfs_dir("data/fixtures/tiny")
tr.earliest_arrival(tt, "A", tr.parse_gtfs_time("08:00:00"))
# {'A': 28800, 'B': 29400, 'C': 30000, 'D': 30300}  (seconds after midnight)
```

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
