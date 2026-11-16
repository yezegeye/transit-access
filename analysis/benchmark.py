"""Benchmark the C++ router against the same algorithm in pure Python.

Both run the Connection Scan Algorithm with the analysis settings (footpaths,
2-minute transfers) on the same random queries: a stop and a departure time
between 7 and 9 AM, every stop reached within 90 minutes. The Python version
is a line-by-line port of the C++ scan, so the comparison measures the
language and data layout, not a different algorithm. Every query's answers
are checked to be identical before timing counts.

    python analysis/benchmark.py [--queries 200]

Writes results/benchmark.csv.
"""

from __future__ import annotations

import argparse
import bisect
import platform
import random
import statistics
import sys
import time
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis.job_access import GTFS_DIR, Scenario, import_router  # noqa: E402

RESULTS = Path(__file__).resolve().parents[1] / "results"
MAX_SECONDS = 90 * 60
UNREACHABLE = 2**31 - 1


class PythonRouter:
    """Connection Scan in plain Python lists, mirroring cpp/router.cpp."""

    def __init__(self, tr, tt):
        c = tr.connection_arrays(tt)
        self.dep_stop = c["dep_stop"].tolist()
        self.arr_stop = c["arr_stop"].tolist()
        self.dep_time = c["dep_time"].tolist()
        self.arr_time = c["arr_time"].tolist()
        self.trip = c["trip"].tolist()
        self.n_stops = tt.num_stops
        self.n_trips = max(self.trip) + 1 if self.trip else 0
        self.transfer = tt.min_transfer_time
        index = {sid: i for i, sid in enumerate(tt.stop_ids)}
        self.footpaths = [[(index[to], d) for to, d in tt.footpaths(sid).items()] for sid in tt.stop_ids]

    def arrival_times(self, source: int, t0: int, max_seconds: int) -> list[int]:
        limit = t0 + max_seconds
        n = self.n_stops
        arrival = [UNREACHABLE] * n
        by_vehicle = [UNREACHABLE] * n
        ready = [UNREACHABLE] * n
        on_trip = [False] * self.n_trips
        arrival[source] = ready[source] = t0
        for to, d in self.footpaths[source]:
            t = t0 + d
            if t < arrival[to]:
                arrival[to] = t
            if t < ready[to]:
                ready[to] = t

        transfer = self.transfer
        dep_stop, arr_stop, dep_time, arr_time, trip = (
            self.dep_stop, self.arr_stop, self.dep_time, self.arr_time, self.trip)
        footpaths = self.footpaths
        i = bisect.bisect_left(dep_time, t0)
        end = bisect.bisect_right(dep_time, limit)
        while i < end:
            k = trip[i]
            if not on_trip[k]:
                if ready[dep_stop[i]] > dep_time[i]:
                    i += 1
                    continue
                on_trip[k] = True
            a, s = arr_time[i], arr_stop[i]
            if a < by_vehicle[s]:
                by_vehicle[s] = a
                if a < arrival[s]:
                    arrival[s] = a
                if a + transfer < ready[s]:
                    ready[s] = a + transfer
                for to, d in footpaths[s]:
                    t = a + d
                    if t < arrival[to]:
                        arrival[to] = t
                    if t + transfer < ready[to]:
                        ready[to] = t + transfer
            i += 1
        return [x if x <= limit else UNREACHABLE for x in arrival]


def percentile(values: list[float], q: float) -> float:
    return float(np.percentile(values, q))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--queries", type=int, default=200)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args(argv)

    tr = import_router()
    scenario = Scenario()
    tt = tr.Timetable.from_gtfs_dir(GTFS_DIR, date=scenario.service_date)
    tt.generate_footpaths(scenario.transfer_walk_m, scenario.walk_speed_mps)
    tt.min_transfer_time = scenario.min_transfer_s
    py_router = PythonRouter(tr, tt)

    rng = random.Random(args.seed)
    queries = [(rng.randrange(tt.num_stops), rng.randrange(7 * 3600, 9 * 3600 + 1))
               for _ in range(args.queries)]
    stop_ids = tt.stop_ids

    # Warm up and check the two agree on every query.
    tr.arrival_times(tt, stop_ids[0], 8 * 3600, MAX_SECONDS)
    for stop, t0 in queries:
        cpp = tr.arrival_times(tt, stop_ids[stop], t0, MAX_SECONDS).tolist()
        if cpp != py_router.arrival_times(stop, t0, MAX_SECONDS):
            print(f"MISMATCH for stop {stop_ids[stop]} at {t0}", file=sys.stderr)
            return 1

    timings = {"C++ (via Python binding)": [], "Pure Python": []}
    for stop, t0 in queries:
        start = time.perf_counter()
        tr.arrival_times(tt, stop_ids[stop], t0, MAX_SECONDS)
        timings["C++ (via Python binding)"].append(time.perf_counter() - start)
        start = time.perf_counter()
        py_router.arrival_times(stop, t0, MAX_SECONDS)
        timings["Pure Python"].append(time.perf_counter() - start)

    rows = []
    for name, ts in timings.items():
        ms = [t * 1000 for t in ts]
        rows.append({"implementation": name, "queries": len(ms),
                     "queries_per_s": round(len(ms) / sum(ts), 1),
                     "p50_ms": round(percentile(ms, 50), 3), "p99_ms": round(percentile(ms, 99), 3),
                     "mean_ms": round(statistics.mean(ms), 3)})
    out = pd.DataFrame(rows)
    speedup = out.loc[1, "p50_ms"] / out.loc[0, "p50_ms"]
    RESULTS.mkdir(exist_ok=True)
    out.to_csv(RESULTS / "benchmark.csv", index=False)
    print(f"{tt.num_connections:,} connections, {tt.num_stops:,} stops, {tt.num_footpaths:,} footpaths; "
          f"{len(queries)} queries, each reaching every stop within 90 min; answers identical")
    print(f"machine: {platform.machine()} {platform.system()}, Python {platform.python_version()}")
    print(out.to_string(index=False))
    print(f"median speedup: {speedup:.0f}x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
