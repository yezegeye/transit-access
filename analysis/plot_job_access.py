"""Map jobs reachable by transit from each Chicago tract.

Run after analysis/job_access.py:

    python analysis/plot_job_access.py

Writes results/job_access_map.png.
"""

from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import geopandas as gpd
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.ticker import FuncFormatter

ROOT = Path(__file__).resolve().parents[1]
PROCESSED = ROOT / "data" / "processed"
RESULTS = ROOT / "results"


def main() -> int:
    tracts = gpd.read_file(PROCESSED / "cook_tracts.gpkg")
    access = pd.read_csv(PROCESSED / "job_access.csv", dtype={"geoid": str})
    chicago = tracts[tracts["in_chicago"]].merge(access, on="geoid").to_crs("EPSG:26916")
    total_jobs = tracts["jobs"].sum()

    fig, axes = plt.subplots(1, 3, figsize=(15, 7.5), constrained_layout=True)
    for ax, minutes in zip(axes, (30, 45, 60)):
        col = f"jobs_{minutes}"
        chicago.plot(column=col, cmap="viridis", vmin=0, vmax=1_500_000, ax=ax,
                     edgecolor="none", legend=minutes == 60,
                     legend_kwds={"label": "jobs reachable", "shrink": 0.6,
                                  "format": FuncFormatter(lambda v, _: f"{v / 1e6:.1f}M")})
        no_transit = chicago[~chicago["has_transit"]]
        if len(no_transit):
            no_transit.plot(ax=ax, color="lightgrey", edgecolor="none")
        median = chicago[col].median()
        ax.set_title(f"Within {minutes} minutes\nmedian tract: {median:,.0f} jobs "
                     f"({median / total_jobs:.0%} of Cook County)", fontsize=12)
        ax.set_axis_off()
    fig.suptitle("Jobs reachable by CTA bus and 'L' from each Chicago census tract\n"
                 "median trip leaving 7–9 AM on a weekday; jobs from LODES 2024",
                 fontsize=14)
    RESULTS.mkdir(exist_ok=True)
    out = RESULTS / "job_access_map.png"
    fig.savefig(out, dpi=120)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
