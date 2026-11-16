"""Router answers for a few station-to-station trips, to compare by hand
against a trip planner (CTA/RTA trip planner or Google Maps transit, set to
leave at the same time on the same date, buses and 'L' only).

    python analysis/check_trips.py
"""

from __future__ import annotations

import sys

from job_access import GTFS_DIR, Scenario, import_router

# (from, to) as GTFS parent-station ids, with names for the trip planner.
TRIPS = [
    ("40890", "40380", "O'Hare -> Clark/Lake", "one Blue Line ride"),
    ("40900", "41400", "Howard -> Roosevelt", "one Red Line ride"),
    ("40930", "41420", "Midway -> Addison (Red)", "Orange then Red"),
    ("41290", "40450", "Kimball -> 95th/Dan Ryan", "Brown then Red"),
    ("41280", "40930", "Jefferson Park -> Midway", "no single obvious route"),
]
DEPART = "08:00:00"


def main() -> int:
    tr = import_router()
    scenario = Scenario()
    tt = tr.Timetable.from_gtfs_dir(GTFS_DIR, date=scenario.service_date)
    tt.generate_footpaths(scenario.transfer_walk_m, scenario.walk_speed_mps)
    t0 = tr.parse_gtfs_time(DEPART)
    print(f"Leaving at {DEPART} on {scenario.service_date:%A %d %b %Y}")
    for origin, dest, label, note in TRIPS:
        arrival = tr.earliest_arrival(tt, origin, t0).get(dest)
        if arrival is None:
            print(f"  {label:<28} unreachable")
            continue
        h, m, s = arrival // 3600, arrival // 60 % 60, arrival % 60
        print(f"  {label:<28} arrive {h:02d}:{m:02d}:{s:02d}  ({(arrival - t0) / 60:.0f} min; {note})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
