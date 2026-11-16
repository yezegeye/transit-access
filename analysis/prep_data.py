"""Download and tidy the census inputs for the accessibility analysis.

Builds one row per Cook County census tract (2020 boundaries) with:

- a population-weighted centroid (where the router starts and ends trips),
- the tract shape, clipped to the shoreline (for maps),
- whether the tract is in the City of Chicago,
- jobs located in the tract (LEHD LODES workplace area characteristics),
- median household income, population and car-free households (ACS 5-year).

Run from the repository root:

    python analysis/prep_data.py

Downloads are cached under data/raw/; outputs go to data/processed/.
"""

from __future__ import annotations

import argparse
import gzip
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
PROCESSED = ROOT / "data" / "processed"

STATE_FIPS = "17"  # Illinois
COUNTY_FIPS = "031"  # Cook County
CHICAGO_PLACE_FIPS = "14000"

LODES_YEAR = 2024
ACS_YEAR = 2024  # 2020-2024 5-year estimates; uses 2020 tract boundaries

CTA_GTFS_URL = "https://www.transitchicago.com/downloads/sch_data/google_transit.zip"
CTA_GTFS_ZIP = RAW / "cta" / "google_transit.zip"
CTA_GTFS_DIR = RAW / "cta" / "gtfs"

SOURCES = {
    "centroids": (
        "https://www2.census.gov/geo/docs/reference/cenpop2020/tract/CenPop2020_Mean_TR17.txt",
        RAW / "census" / "CenPop2020_Mean_TR17.txt",
    ),
    "tracts": (
        "https://www2.census.gov/geo/tiger/GENZ2020/shp/cb_2020_17_tract_500k.zip",
        RAW / "census" / "cb_2020_17_tract_500k.zip",
    ),
    "places": (
        "https://www2.census.gov/geo/tiger/GENZ2020/shp/cb_2020_17_place_500k.zip",
        RAW / "census" / "cb_2020_17_place_500k.zip",
    ),
    "jobs": (
        f"https://lehd.ces.census.gov/data/lodes/LODES8/il/wac/il_wac_S000_JT00_{LODES_YEAR}.csv.gz",
        RAW / "lodes" / f"il_wac_S000_JT00_{LODES_YEAR}.csv.gz",
    ),
}
ACS_TABLES = ["b19013", "b01003", "b08201"]
for _table in ACS_TABLES:
    SOURCES[f"acs_{_table}"] = (
        "https://www2.census.gov/programs-surveys/acs/summary_file/"
        f"{ACS_YEAR}/table-based-SF/data/5YRData/acsdt5y{ACS_YEAR}-{_table}.dat",
        RAW / "census" / f"acsdt5y{ACS_YEAR}-{_table}.dat",
    )

# LODES WAC columns kept: all jobs, then jobs by monthly earnings.
JOB_COLUMNS = {
    "C000": "jobs",
    "CE01": "jobs_low_wage",  # $1,250/month or less
    "CE02": "jobs_mid_wage",  # $1,251 to $3,333/month
    "CE03": "jobs_high_wage",  # more than $3,333/month
}

# ACS columns kept, by summary-file name.
ACS_COLUMNS = {
    "B19013_E001": "median_income",
    "B19013_M001": "median_income_moe",
    "B01003_E001": "population",
    "B08201_E001": "households",
    "B08201_E002": "households_no_vehicle",
}


def download(url: str, path: Path) -> Path:
    """Fetch `url` to `path` unless it is already there."""
    if path.exists() and path.stat().st_size > 0:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {url}", file=sys.stderr)
    tmp = path.with_suffix(path.suffix + ".part")
    with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
        while chunk := response.read(1 << 20):
            out.write(chunk)
    tmp.rename(path)
    return path


def ensure_cta_gtfs() -> Path:
    """Download and unzip the CTA GTFS feed unless it is already there.

    CTA publishes only its current feed, so a fresh download may not cover
    the analysis date in job_access.py; docs/decisions.md records the feed
    used for the published results."""
    if not (CTA_GTFS_DIR / "stop_times.txt").exists():
        download(CTA_GTFS_URL, CTA_GTFS_ZIP)
        with zipfile.ZipFile(CTA_GTFS_ZIP) as z:
            z.extractall(CTA_GTFS_DIR)
    return CTA_GTFS_DIR


def read_centroids(source: io.TextIOBase | Path, county: str = COUNTY_FIPS) -> pd.DataFrame:
    """Population-weighted tract centroids for one county (2020 Census).

    Returns geoid (11-digit state+county+tract), lat, lon, pop_2020.
    """
    df = pd.read_csv(source, dtype={"STATEFP": str, "COUNTYFP": str, "TRACTCE": str},
                     encoding="utf-8-sig")
    df = df[df["COUNTYFP"] == county]
    return pd.DataFrame({
        "geoid": df["STATEFP"] + df["COUNTYFP"] + df["TRACTCE"],
        "lat": df["LATITUDE"].astype(float),
        "lon": df["LONGITUDE"].astype(float),
        "pop_2020": df["POPULATION"].astype(int),
    }).reset_index(drop=True)


def jobs_by_tract(source: io.IOBase | Path, county_prefix: str = STATE_FIPS + COUNTY_FIPS) -> pd.DataFrame:
    """Sum LODES workplace jobs from census blocks to tracts.

    A block geocode is 15 digits: state (2), county (3), tract (6), block (4),
    so its first 11 digits are the tract geoid.
    """
    df = pd.read_csv(source, dtype={"w_geocode": str}, usecols=["w_geocode", *JOB_COLUMNS])
    df = df[df["w_geocode"].str.startswith(county_prefix)]
    df["geoid"] = df["w_geocode"].str[:11]
    return (df.groupby("geoid", as_index=False)[list(JOB_COLUMNS)].sum()
              .rename(columns=JOB_COLUMNS))


def read_acs_table(source: io.TextIOBase | Path, county_prefix: str = STATE_FIPS + COUNTY_FIPS) -> pd.DataFrame:
    """Tract rows of an ACS table-based summary file (pipe-delimited).

    Tract GEO_IDs look like 1400000US17031010100. The Census marks missing or
    unreliable values with large negative codes (e.g. -666666666), which become NaN.
    """
    df = pd.read_csv(source, sep="|", dtype={"GEO_ID": str})
    df = df[df["GEO_ID"].str.startswith("1400000US" + county_prefix)].copy()
    df.insert(0, "geoid", df.pop("GEO_ID").str[len("1400000US"):])
    values = df.columns.drop("geoid")
    df[values] = df[values].apply(pd.to_numeric, errors="coerce")
    df[values] = df[values].mask(df[values] < 0)
    return df.reset_index(drop=True)


def build(paths: dict[str, Path]) -> "geopandas.GeoDataFrame":
    import geopandas as gpd

    centroids = read_centroids(paths["centroids"])

    shapes = gpd.read_file(paths["tracts"])
    shapes = shapes.loc[shapes["COUNTYFP"] == COUNTY_FIPS, ["GEOID", "ALAND", "geometry"]]
    shapes = shapes.rename(columns={"GEOID": "geoid", "ALAND": "land_m2"})

    places = gpd.read_file(paths["places"])
    chicago = places.loc[places["PLACEFP"] == CHICAGO_PLACE_FIPS].to_crs(shapes.crs)
    if len(chicago) != 1:
        raise ValueError(f"expected one Chicago boundary, found {len(chicago)}")

    with gzip.open(paths["jobs"], "rb") as f:
        jobs = jobs_by_tract(f)

    acs = None
    for table in ACS_TABLES:
        part = read_acs_table(paths[f"acs_{table}"])
        part = part[["geoid", *[c for c in ACS_COLUMNS if c in part.columns]]]
        acs = part if acs is None else acs.merge(part, on="geoid", how="outer")
    acs = acs.rename(columns=ACS_COLUMNS)

    tracts = shapes.merge(centroids, on="geoid", how="left", validate="1:1")
    tracts = tracts.merge(jobs, on="geoid", how="left", validate="1:1")
    tracts = tracts.merge(acs, on="geoid", how="left", validate="1:1")
    job_cols = list(JOB_COLUMNS.values())
    tracts[job_cols] = tracts[job_cols].fillna(0).astype(int)  # no LODES rows = no jobs

    points = gpd.GeoSeries(gpd.points_from_xy(tracts["lon"], tracts["lat"]), crs="EPSG:4326")
    tracts["in_chicago"] = points.to_crs(shapes.crs).within(chicago.geometry.iloc[0]).to_numpy()
    tracts["share_no_vehicle"] = tracts["households_no_vehicle"] / tracts["households"].where(
        tracts["households"] > 0)

    columns = ["geoid", "in_chicago", "lat", "lon", "pop_2020", "population", "households",
               "households_no_vehicle", "share_no_vehicle", "median_income",
               "median_income_moe", *job_cols, "land_m2", "geometry"]
    return tracts[columns].sort_values("geoid").reset_index(drop=True)


def check(tracts, paths: dict[str, Path]) -> list[str]:
    """Sanity checks; returns human-readable problems (empty if none)."""
    problems = []
    if tracts["geoid"].duplicated().any():
        problems.append("duplicate tract geoids")
    missing_centroid = tracts["lat"].isna().sum()
    if missing_centroid:
        problems.append(f"{missing_centroid} tracts have no population centroid")
    with gzip.open(paths["jobs"], "rb") as f:
        county_jobs = jobs_by_tract(f)["jobs"].sum()
    if tracts["jobs"].sum() != county_jobs:
        problems.append(f"jobs on tracts ({tracts['jobs'].sum():,}) != county total ({county_jobs:,})")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=Path, default=PROCESSED, help="output directory")
    args = parser.parse_args(argv)

    ensure_cta_gtfs()
    paths = {name: download(url, path) for name, (url, path) in SOURCES.items()}
    tracts = build(paths)
    problems = check(tracts, paths)
    for p in problems:
        print(f"PROBLEM: {p}", file=sys.stderr)

    args.out.mkdir(parents=True, exist_ok=True)
    tracts.to_file(args.out / "cook_tracts.gpkg", driver="GPKG")
    tracts.drop(columns="geometry").to_csv(args.out / "cook_tracts.csv", index=False)

    chicago = tracts[tracts["in_chicago"]]
    print(f"Cook County tracts: {len(tracts):,} ({len(chicago):,} in Chicago)")
    print(f"Jobs ({LODES_YEAR} LODES): {tracts['jobs'].sum():,} in Cook, "
          f"{chicago['jobs'].sum():,} in Chicago")
    print(f"Population (ACS {ACS_YEAR - 4}-{ACS_YEAR}): {int(tracts['population'].sum()):,} in Cook, "
          f"{int(chicago['population'].sum()):,} in Chicago")
    print(f"Tracts without a median income estimate: {tracts['median_income'].isna().sum()} "
          f"({chicago['median_income'].isna().sum()} in Chicago)")
    print(f"wrote {args.out / 'cook_tracts.gpkg'} and cook_tracts.csv")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
