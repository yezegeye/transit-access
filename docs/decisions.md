# Decisions

What the analysis uses, where the data came from, and why each choice was
made. Results in the README and `results/` come from exactly these settings.

## Data

| Data | Source | Version | Downloaded |
|---|---|---|---|
| CTA schedules (bus and 'L') | [CTA GTFS](https://www.transitchicago.com/downloads/sch_data/google_transit.zip) | Service 2026-09-11 to 2026-11-30; `google_transit.zip` SHA-256 `a9372be5b70fcee4ed98c9476433f86065637a9a500b8af6afbb84d000ef2746` | 2026-10-29 |
| Tract population centroids | [Census 2020 population-weighted centroids](https://www2.census.gov/geo/docs/reference/cenpop2020/tract/CenPop2020_Mean_TR17.txt) | 2020 Census | 2026-11-16 |
| Tract shapes | [Census cartographic boundary file](https://www2.census.gov/geo/tiger/GENZ2020/shp/cb_2020_17_tract_500k.zip), 1:500k | 2020 | 2026-11-16 |
| City of Chicago boundary | [Census places](https://www2.census.gov/geo/tiger/GENZ2020/shp/cb_2020_17_place_500k.zip) | 2020 | 2026-11-16 |
| Jobs | [LEHD LODES 8](https://lehd.ces.census.gov/data/lodes/LODES8/il/wac/), Illinois workplace area characteristics, all jobs (JT00) | 2024 | 2026-11-16 |
| Income, population, cars | [ACS 5-year table-based summary files](https://www2.census.gov/programs-surveys/acs/summary_file/2024/table-based-SF/data/5YRData/), tables B19013, B01003, B08201 | 2020-2024 | 2026-11-16 |

CTA publishes only its current feed. `make data` downloads it if
`data/raw/cta/gtfs` is missing, and a newer feed may not cover the analysis
date; `job_access.py` then stops and asks for a date the feed covers. To
reproduce the published numbers exactly, use the feed with the hash above.

The Census API now requires a key, so ACS figures come from the bulk summary
files instead, which do not.

All census inputs use 2020 tract boundaries (Cook County: 1,332 tracts; the
lake-only tract 17031990000 has no shape and is dropped, leaving 1,331).

## Analysis settings

| Setting | Value | Why |
|---|---|---|
| Service day | Wednesday 18 Nov 2026 | An ordinary weekday inside the feed, not a holiday |
| Departure window | Every minute 7:00-9:00 AM, median travel time | A single departure time makes results depend on whether a bus happens to leave just then; the median over the peak describes a typical trip. Standard in accessibility studies |
| Thresholds | 30, 45, 60 minutes | Common commute benchmarks |
| Origins | 793 City of Chicago tracts (centroid inside the city) | The question is about Chicago neighborhoods |
| Destinations | All 1,331 Cook County tracts | Jobs just outside the city still count |
| Trip ends | Population-weighted tract centroids | Trips start where people live, not at a tract's geometric middle |
| Walk to and from transit | Up to 800 m, straight line | About a 10-minute walk, the usual catchment for a stop |
| Walk between stops | Up to 400 m, straight line | Covers same-corner and cross-street transfers |
| Walking speed | 5 km/h along streets, streets 1.2x the straight line (1.157 m/s over the straight line) | Fixed in the original roadmap; the detour factor corrects straight-line distances on average |
| Transfer time | 2 minutes on top of any walk | See below |
| Jobs | LODES workplace counts, summed from blocks to tracts, counted once per reachable tract | The roadmap's single destination measure |
| Income groups | Quartiles of tract median household income (equal numbers of tracts); 19 tracts without an estimate left out | Simple and easy to explain; matches the roadmap |

### Transfer time

The router first allowed instantaneous transfers. Checking five trips
against Google Maps showed the gap: from Midway, Google took the same
Orange Line train as the router but allowed about 9 minutes through the
tunnel to the Red Line at Roosevelt, while the router allowed a 1-minute
walk. Real CTA transfers range from across the platform (Belmont, Wilson)
to that tunnel, so no single value is right everywhere. 2 minutes is a
middle value; `analysis/sensitivity.py` reruns everything at 0, 2 and 5
minutes, and the income gap holds (and grows) at every setting.

## Changes from the original roadmap

| Roadmap | Built | Why |
|---|---|---|
| Time-dependent Dijkstra | Connection Scan Algorithm | One forward pass over a sorted array: simpler, fast for one-to-all queries, easy to verify |
| One 8:00 AM departure for headline results | Median over every minute 7-9 AM | More robust (see above) |
| Geometric tract centroids, TIGER/Line shapes | Population-weighted centroids, cartographic boundary shapes | Trips start where people live; shapes clipped to the shoreline for maps |
| Destination reached when a nearby stop is reached | Also adds the walk from that stop to the destination | Slightly more realistic travel times |
| Python modules `load_gtfs.py`, `access.py`, `equity.py`, `plots.py` | `analysis/prep_data.py`, `job_access.py`, `plot_job_access.py`, `equity.py`, `sensitivity.py` | Same pieces, named for what they do |
| (not planned) | Minimum transfer time, journey reconstruction, multithreaded door-to-door matrix, CI | Needed for validation and to make full-city runs take seconds |

## Validation

- Unit tests on hand-made feeds, plus randomized checks: the scan against an
  order-independent reference (at 0, 90 and 300-second transfer times),
  "leaving later never arrives earlier" (also on the full CTA weekday: no
  violations in 40M comparisons), and every reconstructed journey against
  the timetable.
- Five station-to-station trips compared with Google Maps (see the README).
  Ride times match CTA's schedule; differences come from transfer time.
  Kimball to 95th/Dan Ryan stays about 6 minutes faster than Google even
  with a 5-minute transfer, probably a different route; not resolved.

## Limitations

- CTA only: Metra and Pace are left out, so places they serve (two Far
  Northwest Side tracts have no CTA stop within 800 m) score low, and
  suburban job centers are under-counted.
- Straight-line walking with an average detour factor; one fixed transfer
  time everywhere.
- Scheduled service only: no delays, crowding or reliability.
- Jobs are counted where they are, with no match to residents' skills or
  wages; access is not the same as employment.
- Results describe one weekday morning in fall 2026.
