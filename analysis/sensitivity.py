"""How the main results change with the minimum transfer time.

Reruns job access and the income comparison with 0, 2 and 5 minutes to
change vehicles (2 minutes is the headline setting) and writes
results/sensitivity.csv.

    python analysis/sensitivity.py
"""

from __future__ import annotations

import dataclasses
import sys
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis import equity  # noqa: E402
from analysis.job_access import PROCESSED, Scenario, compute  # noqa: E402

RESULTS = Path(__file__).resolve().parents[1] / "results"
TRANSFER_MINUTES = (0, 2, 5)


def main() -> int:
    tracts = pd.read_csv(PROCESSED / "cook_tracts.csv", dtype={"geoid": str})
    rows = []
    for minutes in TRANSFER_MINUTES:
        scenario = dataclasses.replace(Scenario(), min_transfer_s=minutes * 60)
        access, *_ = compute(tracts, scenario)
        df = equity.add_quartiles(access.merge(tracts, on="geoid"))
        summary = equity.summarize(df)
        row = {"min_transfer_min": minutes}
        for t in equity.THRESHOLDS:
            row[f"median_jobs_{t}"] = int(access[f"jobs_{t}"].median())
        for t in equity.THRESHOLDS:
            row[f"top_bottom_ratio_{t}"] = round(equity.top_bottom_ratio(summary, t), 2)
        for t in equity.THRESHOLDS:
            row[f"spearman_{t}"] = round(equity.spearman(df, t), 3)
        rows.append(row)
    out = pd.DataFrame(rows)
    RESULTS.mkdir(exist_ok=True)
    out.to_csv(RESULTS / "sensitivity.csv", index=False)
    print(out.to_string(index=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
