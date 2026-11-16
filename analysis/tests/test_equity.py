import numpy as np
import pandas as pd
import pytest

from analysis.equity import QUARTILES, add_quartiles, spearman, summarize, top_bottom_ratio


@pytest.fixture
def tracts():
    # Eight tracts with incomes 10K..80K; access rises with income except tract 2.
    n = 8
    df = pd.DataFrame({
        "geoid": [f"t{i}" for i in range(n)],
        "median_income": [10_000 * (i + 1) for i in range(n)],
        "population": [1000] * n,
        "households": [400] * n,
        "households_no_vehicle": [100, 100, 100, 100, 40, 40, 40, 40],
    })
    for m in (30, 45, 60):
        df[f"jobs_{m}"] = [m * 100 * (i + 1) for i in range(n)]
    df.loc[1, "jobs_45"] = 0
    df["jobs_low_wage_45"] = df["jobs_45"] // 10
    return pd.concat([df, pd.DataFrame({"geoid": ["no_income"], "median_income": [np.nan],
                                        "population": [0], "households": [0],
                                        "households_no_vehicle": [0], "jobs_30": [1], "jobs_45": [1],
                                        "jobs_60": [1], "jobs_low_wage_45": [0]})], ignore_index=True)


def test_quartiles_split_tracts_evenly_and_skip_missing_income(tracts):
    df = add_quartiles(tracts)
    assert df["income_quartile"].value_counts().reindex(QUARTILES).tolist() == [2, 2, 2, 2]
    assert pd.isna(df.loc[df["geoid"] == "no_income", "income_quartile"]).all()
    assert df.loc[0, "income_quartile"] == QUARTILES[0]
    assert df.loc[7, "income_quartile"] == QUARTILES[3]


def test_summary_means_and_ratio(tracts):
    summary = summarize(add_quartiles(tracts))
    assert summary["tracts"].tolist() == [2, 2, 2, 2]
    # Q1 holds tracts 0 and 1: jobs_45 = 4500 and 0 -> mean 2250.
    assert summary.loc[QUARTILES[0], "mean_jobs_45"] == 2250
    # Q4 holds tracts 6 and 7: jobs_45 = 31500 and 36000 -> mean 33750.
    assert summary.loc[QUARTILES[3], "mean_jobs_45"] == 33750
    assert top_bottom_ratio(summary, 45) == pytest.approx(15.0)
    assert summary.loc[QUARTILES[0], "share_no_vehicle"] == 0.25


def test_spearman_ignores_tracts_without_income(tracts):
    assert spearman(tracts, 30) == pytest.approx(1.0)
