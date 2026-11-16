from pathlib import Path

import numpy as np

from analysis.job_access import Scenario, jobs_within, median_travel_times

TINY = Path(__file__).resolve().parents[2] / "data" / "fixtures" / "tiny"


def test_jobs_within_counts_reachable_destinations():
    travel = np.array([[0, 600, 2000, np.inf],
                       [1800, 0, np.inf, 900]], dtype=np.float32)
    jobs = np.array([10, 20, 30, 40])
    assert jobs_within(travel, jobs, 30).tolist() == [30, 70]  # 1800 s counts
    assert jobs_within(travel, jobs, 10).tolist() == [30, 20]


def test_median_travel_times_on_tiny_feed():
    import transit_router as tr

    tt = tr.Timetable.from_gtfs_dir(TINY)
    a, c, d = (0.0, 0.0), (0.0, 0.02), (0.01, 0.01)
    # Leaving A at 08:00, 08:01, 08:02: D in 25, 39, 38 minutes -> median 38.
    scenario = Scenario(window_start=8 * 3600, window_end=8 * 3600 + 120,
                        access_walk_m=100, thresholds_min=(60,))
    out = median_travel_times(tr, tt, np.array([a, c]), np.array([d]), scenario, chunk=1)
    assert out.shape == (2, 1)
    assert out[0, 0] == 38 * 60
    assert out[1, 0] == 34 * 60  # C -> D waits for T4 (08:30 -> 08:35): 35, 34, 33 min

    short = Scenario(window_start=8 * 3600, window_end=8 * 3600 + 120,
                     access_walk_m=100, thresholds_min=(30,))
    capped = median_travel_times(tr, tt, np.array([a]), np.array([d]), short)
    assert np.isinf(capped[0, 0])  # 2 of 3 departures take over 30 minutes
