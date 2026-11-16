"""Jobs reachable by CTA transit from each Chicago census tract.

For every Chicago tract (origin) and every Cook County tract (destination),
routes door to door between population-weighted centroids at each minute of
a morning window, takes the median travel time over those departures (a
typical trip, not a lucky one), and counts the jobs in every destination
reachable within each time threshold.

Run from the repository root after analysis/prep_data.py:

    python analysis/job_access.py

Writes data/processed/job_access.csv (one row per Chicago tract) and
data/processed/travel_times.npz (the median travel-time matrix).
"""

from __future__ import annotations

import argparse
import datetime
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
PROCESSED = ROOT / "data" / "processed"
GTFS_DIR = ROOT / "data" / "raw" / "cta" / "gtfs"

JOB_TYPES = ["jobs", "jobs_low_wage", "jobs_mid_wage", "jobs_high_wage"]


@dataclass(frozen=True)
class Scenario:
    service_date: datetime.date = datetime.date(2026, 11, 18)  # a Wednesday
    window_start: int = 7 * 3600  # 7:00 AM
    window_end: int = 9 * 3600  # 9:00 AM, inclusive
    step: int = 60  # a departure every minute
    transfer_walk_m: float = 400.0  # longest walk between stops, straight line
    access_walk_m: float = 800.0  # longest walk from home / to work, straight line
    walk_kmh: float = 5.0  # walking pace along streets
    walk_detour: float = 1.2  # street distance / straight-line distance
    thresholds_min: tuple[int, ...] = (30, 45, 60)

    @property
    def walk_speed_mps(self) -> float:
        """Speed over straight-line distance: the router measures walks as
        straight lines, so the detour is folded into the speed
        (5 km/h with a 1.2 detour = 1.157 m/s)."""
        return self.walk_kmh / 3.6 / self.walk_detour

    @property
    def departures(self) -> list[int]:
        return list(range(self.window_start, self.window_end + 1, self.step))

    @property
    def max_travel_s(self) -> int:
        return max(self.thresholds_min) * 60


def import_router():
    """Import the compiled transit_router module, built into build/."""
    try:
        import transit_router
    except ImportError:
        sys.path.insert(0, str(ROOT / "build"))
        import transit_router
    return transit_router


def median_travel_times(tr, tt, origins: np.ndarray, destinations: np.ndarray,
                        scenario: Scenario, chunk: int = 64) -> np.ndarray:
    """(n_origins, n_destinations) median travel time in seconds over the
    departure window; np.inf when the median trip is longer than the largest
    threshold or impossible. Origins are routed in chunks so the full
    (origins x departures x destinations) array never sits in memory."""
    out = np.empty((len(origins), len(destinations)), dtype=np.float32)
    for start in range(0, len(origins), chunk):
        block = tr.travel_time_matrix(
            tt, origins[start:start + chunk], destinations, scenario.departures,
            max_access_m=scenario.access_walk_m, walk_speed_mps=scenario.walk_speed_mps,
            max_travel_time=scenario.max_travel_s)
        out[start:start + chunk] = np.median(block, axis=1)
    return out


def jobs_within(travel_s: np.ndarray, jobs: np.ndarray, minutes: int) -> np.ndarray:
    """Jobs reachable within `minutes` from each origin: a reachable/not
    matrix times the jobs at each destination."""
    return (travel_s <= minutes * 60).astype(np.float64) @ jobs.astype(np.float64)


def compute(tracts: pd.DataFrame, scenario: Scenario) -> tuple[pd.DataFrame, np.ndarray, pd.DataFrame, pd.DataFrame]:
    tr = import_router()
    tt = tr.Timetable.from_gtfs_dir(GTFS_DIR, date=scenario.service_date)
    tt.generate_footpaths(scenario.transfer_walk_m, scenario.walk_speed_mps)

    tracts = tracts.dropna(subset=["lat", "lon"])
    origins = tracts[tracts["in_chicago"]].reset_index(drop=True)
    destinations = tracts.reset_index(drop=True)

    t0 = time.time()
    travel = median_travel_times(tr, tt, origins[["lat", "lon"]].to_numpy(),
                                 destinations[["lat", "lon"]].to_numpy(), scenario)
    print(f"routed {len(origins)} origins x {len(destinations)} destinations x "
          f"{len(scenario.departures)} departures in {time.time() - t0:.1f} s", file=sys.stderr)

    result = origins[["geoid"]].copy()
    total = {kind: destinations[kind].sum() for kind in JOB_TYPES}
    for minutes in scenario.thresholds_min:
        for kind in JOB_TYPES:
            result[f"{kind}_{minutes}"] = jobs_within(travel, destinations[kind].to_numpy(), minutes).astype(int)
        result[f"share_jobs_{minutes}"] = result[f"jobs_{minutes}"] / total["jobs"]
    # Origins whose own stops are all too far to walk to reach only their own tract.
    result["has_transit"] = (np.isfinite(travel).sum(axis=1) > 1)
    return result, travel, origins, destinations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--tracts", type=Path, default=PROCESSED / "cook_tracts.csv")
    parser.add_argument("--out", type=Path, default=PROCESSED)
    args = parser.parse_args(argv)

    scenario = Scenario()
    tracts = pd.read_csv(args.tracts, dtype={"geoid": str})
    result, travel, origins, destinations = compute(tracts, scenario)

    args.out.mkdir(parents=True, exist_ok=True)
    result.to_csv(args.out / "job_access.csv", index=False)
    np.savez_compressed(args.out / "travel_times.npz", median_seconds=travel,
                        origins=origins["geoid"].to_numpy(), destinations=destinations["geoid"].to_numpy())

    total = destinations["jobs"].sum()
    for minutes in scenario.thresholds_min:
        col = result[f"jobs_{minutes}"]
        print(f"jobs within {minutes} min: median tract {col.median():,.0f} "
              f"({col.median() / total:.1%} of Cook County jobs), "
              f"10th pct {col.quantile(0.1):,.0f}, 90th pct {col.quantile(0.9):,.0f}")
    print(f"tracts with no reachable transit: {(~result['has_transit']).sum()}")
    print(f"wrote {args.out / 'job_access.csv'} and travel_times.npz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
