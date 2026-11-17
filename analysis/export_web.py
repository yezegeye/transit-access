"""Export the results the website needs to web/data.json.

For each Chicago tract: a simplified shape (as an SVG path in a fixed
projection), its community area name, income, car ownership, the median trip
to the Loop, and jobs reachable at each time limit and transfer setting.
Plus the full Chicago-to-Chicago median travel-time table (whole minutes,
2-minute transfers) as base64 bytes.

Run after analysis/job_access.py:

    python analysis/export_web.py
"""

from __future__ import annotations

import base64
import dataclasses
import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from analysis.job_access import PROCESSED, Scenario, compute  # noqa: E402
from analysis.prep_data import RAW, download  # noqa: E402

WEB = ROOT / "web"
COMMUNITY_AREAS_URL = "https://data.cityofchicago.org/api/geospatial/igwz-8jzy?method=export&format=GeoJSON"
COMMUNITY_AREAS = RAW / "chicago" / "community_areas.geojson"
LOOP_TRACT = "17031839100"
WIDTH_PX = 520  # map width in SVG units
SIMPLIFY_M = 25  # shape simplification tolerance, metres
UNREACHABLE = 255  # travel-time byte for "over 60 minutes"
TRANSFER_MINUTES = (0, 2, 5)
THRESHOLDS = (30, 45, 60)


def svg_path(geom, minx: float, maxy: float, scale: float) -> str:
    polygons = [geom] if geom.geom_type == "Polygon" else list(geom.geoms)
    parts = []
    for polygon in polygons:
        for ring in [polygon.exterior, *polygon.interiors]:
            pts = [(round((x - minx) * scale, 1), round((maxy - y) * scale, 1)) for x, y in ring.coords[:-1]]
            parts.append("M" + "L".join(f"{x:g},{y:g}" for x, y in pts) + "Z")
    return "".join(parts)


def main() -> int:
    import geopandas as gpd

    tracts = gpd.read_file(PROCESSED / "cook_tracts.gpkg")
    cook = pd.read_csv(PROCESSED / "cook_tracts.csv", dtype={"geoid": str})
    chicago = tracts[tracts["in_chicago"]].reset_index(drop=True)

    areas = gpd.read_file(download(COMMUNITY_AREAS_URL, COMMUNITY_AREAS))[["community", "geometry"]]
    areas = areas.to_crs("EPSG:26916")
    centroids = gpd.GeoDataFrame(chicago[["geoid"]], crs="EPSG:4326",
                                 geometry=gpd.points_from_xy(chicago["lon"], chicago["lat"])).to_crs("EPSG:26916")
    named = gpd.sjoin_nearest(centroids, areas, how="left").drop_duplicates("geoid")
    chicago = chicago.merge(named[["geoid", "community"]], on="geoid", how="left")
    chicago["community"] = chicago["community"].str.title()

    shapes = chicago.to_crs("EPSG:26916").geometry.simplify(SIMPLIFY_M)
    minx, miny, maxx, maxy = shapes.total_bounds
    scale = WIDTH_PX / (maxx - minx)

    saved = np.load(PROCESSED / "travel_times.npz", allow_pickle=True)
    origins, destinations = list(saved["origins"]), list(saved["destinations"])
    rows = [origins.index(g) for g in chicago["geoid"]]
    cols = [destinations.index(g) for g in chicago["geoid"]]
    minutes = saved["median_seconds"][np.ix_(rows, cols)] / 60
    table = np.where(np.isfinite(minutes), np.minimum(np.round(minutes), 250), UNREACHABLE).astype(np.uint8)
    to_loop = saved["median_seconds"][rows, destinations.index(LOOP_TRACT)] / 60

    jobs = {}
    for transfer in TRANSFER_MINUTES:
        access, *_ = compute(cook, dataclasses.replace(Scenario(), min_transfer_s=transfer * 60))
        access = access.set_index("geoid").loc[chicago["geoid"]]
        jobs[str(transfer)] = {str(t): access[f"jobs_{t}"].astype(int).tolist() for t in THRESHOLDS}
        jobs[str(transfer)]["low45"] = access["jobs_low_wage_45"].astype(int).tolist()

    info = pd.read_csv(ROOT / "results" / "access.csv", dtype={"geoid": str}).set_index("geoid").loc[chicago["geoid"]]

    def maybe(value, cast):
        return None if pd.isna(value) else cast(value)

    data = {
        "w": WIDTH_PX,
        "h": round((maxy - miny) * scale),
        "total_jobs": int(cook["jobs"].sum()),
        "service_date": Scenario().service_date.isoformat(),
        "tracts": [
            {"id": geoid[5:], "ca": community, "inc": maybe(income, int), "q": maybe(quartile, lambda q: q[:2]),
             "pop": maybe(population, int), "nocar": maybe(nocar, lambda v: round(float(v), 3)),
             "loop": None if not np.isfinite(loop) else round(float(loop))}
            for geoid, community, income, quartile, population, nocar, loop in zip(
                chicago["geoid"], chicago["community"], info["median_income"], info["income_quartile"],
                info["population"], info["share_no_vehicle"], to_loop)
        ],
        "jobs": jobs,
        "paths": [svg_path(g, minx, maxy, scale) for g in shapes],
        "tt": base64.b64encode(table.tobytes()).decode(),
    }
    WEB.mkdir(exist_ok=True)
    out = WEB / "data.json"
    out.write_text(json.dumps(data, separators=(",", ":")))
    print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB, {len(chicago)} tracts, "
          f"{chicago['community'].nunique()} community areas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
