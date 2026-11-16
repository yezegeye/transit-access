"""Router answers (with no minimum transfer time) for a few station-to-station
trips, to compare by hand
against a trip planner (CTA/RTA trip planner or Google Maps transit, set to
leave at the same time on the same date, buses and 'L' only).

    python analysis/check_trips.py
"""

from __future__ import annotations

import sys

import pandas as pd

from job_access import GTFS_DIR, Scenario, import_router

# (from, to) as GTFS parent-station ids, with names for the trip planner.
TRIPS = [
    ("40890", "40380", "O'Hare -> Clark/Lake", "one Blue Line ride"),
    ("40900", "41400", "Howard -> Roosevelt", "Red Line, or Purple Express then Red"),
    ("40930", "41420", "Midway -> Addison (Red)", "Orange then Red"),
    ("41290", "40450", "Kimball -> 95th/Dan Ryan", "Brown then Red"),
    ("41280", "40930", "Jefferson Park -> Midway", "no single obvious route"),
]
DEPART = "08:00:00"


def hhmm(t: int) -> str:
    return f"{t // 3600:02d}:{t // 60 % 60:02d}:{t % 60:02d}"


def main() -> int:
    tr = import_router()
    scenario = Scenario()
    tt = tr.Timetable.from_gtfs_dir(GTFS_DIR, date=scenario.service_date)
    tt.generate_footpaths(scenario.transfer_walk_m, scenario.walk_speed_mps)
    t0 = tr.parse_gtfs_time(DEPART)
    stops = pd.read_csv(GTFS_DIR / "stops.txt", dtype=str).set_index("stop_id")["stop_name"]
    trips = pd.read_csv(GTFS_DIR / "trips.txt", dtype=str).set_index("trip_id")["route_id"]
    routes = pd.read_csv(GTFS_DIR / "routes.txt", dtype=str).set_index("route_id")
    route_name = routes["route_long_name"].fillna(routes["route_short_name"])

    def describe(leg: dict) -> str:
        where = f"{stops[leg['from_stop']]} -> {stops[leg['to_stop']]}"
        if leg["mode"] == "walk":
            what = f"walk ({(leg['arrival'] - leg['departure']) / 60:.1f} min)"
        else:
            route = trips[leg["trip_id"]]
            what = f"{route} {route_name.get(route, '')}".strip()
        return f"      {hhmm(leg['departure'])}-{hhmm(leg['arrival'])}  {what}: {where}"

    print(f"Leaving at {DEPART} on {scenario.service_date:%A %d %b %Y}")
    for origin, dest, label, note in TRIPS:
        arrival = tr.earliest_arrival(tt, origin, t0).get(dest)
        if arrival is None:
            print(f"  {label:<28} unreachable")
            continue
        h, m, s = arrival // 3600, arrival // 60 % 60, arrival % 60
        print(f"  {label:<28} arrive {h:02d}:{m:02d}:{s:02d}  ({(arrival - t0) / 60:.0f} min; {note})")
        for leg in tr.journey(tt, origin, t0, dest):
            if leg["mode"] == "walk" and leg["arrival"] == leg["departure"]:
                continue  # station entrance to platform, same coordinates
            print(describe(leg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
