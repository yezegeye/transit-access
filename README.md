# transit-access

A C++ public transit router with Python bindings, used to compute
transit accessibility metrics (e.g. jobs reachable within N minutes) from GTFS data.

## Status

The router is complete for the accessibility analysis.

**Works now**

- GTFS loading from `stops.txt` and `stop_times.txt`: header-based column
  lookup, quoted fields, UTF-8 BOM, CRLF line endings, times past 24:00:00,
  stop coordinates.
- Service days: load only the trips running on a given date, from
  `trips.txt`, `calendar.txt` and `calendar_dates.txt` (weekday patterns,
  date ranges, added/removed exceptions such as holidays). On the CTA feed a
  weekday keeps about 1.2M of the 5.8M connections; routing over every trip
  instead overstated the stops reachable within 45 minutes at 8 AM by ~70%.
- Walking transfers: `generate_footpaths` links every pair of stops within a
  straight-line distance (400 m gives 144K footpaths on CTA, in ~20 ms, via a
  lat/lon grid index). The router allows one walk after each ride. Across 200
  random CTA stops, the median number of stops reachable within 45 minutes of
  8 AM rises from 82 without walking to 3,305 with it: a bus rider could not
  otherwise change to a crossing route, whose stops have their own stop_ids.
- Earliest-arrival routing with the
  [Connection Scan Algorithm](https://arxiv.org/abs/1703.05997), from one
  stop or from several stops with their own start times. A randomized test
  checks it against a slower fixpoint reference.
- Door-to-door travel time matrices between arbitrary points (e.g. tract
  centroids) for many departure times: walk to nearby stops, ride, walk to the
  destination. Multithreaded; 800 points x 25 departure times on CTA takes
  ~2 s on 8 cores. Returned to Python as a NumPy array.
- Python bindings, with Catch2 and pytest suites run against small hand-made
  feeds, on every push via GitHub Actions.

**Current limitations**

- Walking distances are straight lines, not along streets, so walks are
  somewhat optimistic (real walks are typically 20-40% longer).
- Walks are not chained: one walk after each ride, plus the walks to the
  first stop and from the last. `transfers.txt` is ignored.
- Transfers have no minimum time: a rider can step off one vehicle and onto
  another departing the same second.
- A service day includes its own trips that run past midnight (times after
  24:00:00) but not the previous day's late-night trips.
- Stop times without an arrival/departure time (non-timepoints) are rejected
  rather than interpolated.

**Next (Python analysis)**

1. Data prep: census tract shapes and centroids (TIGER), jobs per tract (LEHD
   LODES workplace data), median household income (ACS).
2. Job access per tract: jobs reachable within N minutes, using the median
   travel time over a 7-9 AM window.
3. Income regression: how access relates to tract median household income.
4. Maps of access by tract.

## Layout

- `cpp/` — routing core (static library `router`: `router.*` for GTFS loading
  and routing, `geo.*` for distances and the spatial grid) and pybind11
  bindings (`transit_router`)
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

# Walking transfers between stops within 1200 m (the tiny feed's stops are
# about 1.1 km apart): ride to B, then walk to D instead of waiting for T2.
tt.generate_footpaths(max_distance_m=1200, walk_speed_mps=1.3)
tr.earliest_arrival(tt, "A", tr.parse_gtfs_time("08:00:00"))["D"]
# 30256  (08:24:16, vs 08:25:00 by bus)

# Only the trips running on one service day (a datetime.date or "YYYYMMDD"):
import datetime
saturday = tr.Timetable.from_gtfs_dir("data/fixtures/tiny", date=datetime.date(2026, 10, 17))
tr.earliest_arrival(saturday, "A", tr.parse_gtfs_time("08:00:00"))
# {'A': 28800, 'D': 31200}

# Door-to-door travel times between (lat, lon) points for several departures,
# as an array of shape (origins, departures, destinations) in seconds:
import numpy as np
deps = [tr.parse_gtfs_time(f"08:0{m}:00") for m in range(5)]
m = tr.travel_time_matrix(tt, [(0.0, 0.0)], [(0.01, 0.01)], deps, max_access_m=100)
np.median(m, axis=1)  # [[2220.]]: median over departures; np.inf if unreachable
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
