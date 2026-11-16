"""How transit job access varies with neighborhood income.

Splits Chicago tracts into quartiles of median household income and compares
the jobs each can reach by transit: the mean per quartile, the ratio between
the top and bottom quartiles, and the Spearman rank correlation between
income and access. Also draws a scatter of access against income and a bar
chart by quartile.

Run after analysis/job_access.py:

    python analysis/equity.py

Writes results/access.csv (one row per Chicago tract), results/equity.csv
(one row per income quartile) and two figures in results/.
"""

from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.ticker import FuncFormatter

ROOT = Path(__file__).resolve().parents[1]
PROCESSED = ROOT / "data" / "processed"
RESULTS = ROOT / "results"

THRESHOLDS = (30, 45, 60)
QUARTILES = ["Q1 (lowest income)", "Q2", "Q3", "Q4 (highest income)"]
COLORS = ["#b3541e", "#d9a441", "#5b8fa8", "#1f4e79"]


def load(processed: Path = PROCESSED) -> pd.DataFrame:
    """Chicago tracts with access scores and income, one row each."""
    tracts = pd.read_csv(processed / "cook_tracts.csv", dtype={"geoid": str})
    access = pd.read_csv(processed / "job_access.csv", dtype={"geoid": str})
    df = access.merge(tracts, on="geoid", how="left", validate="1:1")
    return df


def add_quartiles(df: pd.DataFrame) -> pd.DataFrame:
    """Income quartile per tract (equal numbers of tracts in each); tracts
    without an income estimate get no quartile."""
    df = df.copy()
    has_income = df["median_income"].notna()
    df.loc[has_income, "income_quartile"] = pd.qcut(
        df.loc[has_income, "median_income"], 4, labels=QUARTILES)
    df["income_quartile"] = pd.Categorical(df["income_quartile"], categories=QUARTILES, ordered=True)
    return df


def summarize(df: pd.DataFrame) -> pd.DataFrame:
    """Mean jobs reachable per income quartile at each threshold, plus the
    quartile's income range and size."""
    rated = df.dropna(subset=["income_quartile"])
    grouped = rated.groupby("income_quartile", observed=True)
    summary = pd.DataFrame({
        "tracts": grouped.size(),
        "population": grouped["population"].sum().astype(int),
        "income_min": grouped["median_income"].min(),
        "income_max": grouped["median_income"].max(),
    })
    for minutes in THRESHOLDS:
        summary[f"mean_jobs_{minutes}"] = grouped[f"jobs_{minutes}"].mean().round().astype(int)
    summary["mean_low_wage_jobs_45"] = grouped["jobs_low_wage_45"].mean().round().astype(int)
    summary["share_no_vehicle"] = (grouped["households_no_vehicle"].sum()
                                   / grouped["households"].sum()).round(3)
    return summary


def top_bottom_ratio(summary: pd.DataFrame, minutes: int) -> float:
    col = summary[f"mean_jobs_{minutes}"]
    return col.iloc[-1] / col.iloc[0]


def spearman(df: pd.DataFrame, minutes: int) -> float:
    rated = df.dropna(subset=["median_income"])
    return rated["median_income"].corr(rated[f"jobs_{minutes}"], method="spearman")


def plot_scatter(df: pd.DataFrame, out: Path, minutes: int = 45) -> None:
    rated = df.dropna(subset=["income_quartile"])
    fig, ax = plt.subplots(figsize=(8, 5.5), constrained_layout=True)
    for quartile, color in zip(QUARTILES, COLORS):
        part = rated[rated["income_quartile"] == quartile]
        ax.scatter(part["median_income"], part[f"jobs_{minutes}"], s=12, color=color,
                   alpha=0.75, label=quartile, edgecolors="none")
    ax.set_xscale("log")
    dollars = FuncFormatter(lambda v, _: f"${v / 1000:,.0f}K")
    ax.xaxis.set_major_formatter(dollars)
    ax.xaxis.set_minor_formatter(FuncFormatter(lambda v, _: dollars(v) if str(int(v))[0] in "25" else ""))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v / 1e6:.1f}M"))
    ax.set_xlabel("Median household income (ACS 2020–2024, log scale)")
    ax.set_ylabel(f"Jobs reachable within {minutes} minutes")
    rho = spearman(df, minutes)
    ax.set_title(f"Transit job access vs. income, Chicago tracts\n"
                 f"Spearman ρ = {rho:.2f}; {len(rated)} tracts with an income estimate", fontsize=11)
    ax.grid(alpha=0.25)
    ax.legend(frameon=False, fontsize=9)
    fig.savefig(out, dpi=120)
    plt.close(fig)


def plot_quartile_bars(summary: pd.DataFrame, out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 5), constrained_layout=True)
    width = 0.2
    for i, (quartile, color) in enumerate(zip(summary.index, COLORS)):
        values = [summary.loc[quartile, f"mean_jobs_{m}"] for m in THRESHOLDS]
        xs = [t + (i - 1.5) * width for t in range(len(THRESHOLDS))]
        ax.bar(xs, values, width, color=color, label=quartile)
    ax.set_xticks(range(len(THRESHOLDS)), [f"within {m} min" for m in THRESHOLDS])
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v / 1e6:.1f}M"))
    ax.set_ylabel("Mean jobs reachable per tract")
    ratios = ", ".join(f"{top_bottom_ratio(summary, m):.1f}x within {m} min" for m in THRESHOLDS)
    ax.set_title(f"Jobs reachable by transit, by tract income quartile\n"
                 f"highest vs. lowest quartile: {ratios}", fontsize=11)
    ax.grid(axis="y", alpha=0.25)
    ax.legend(frameon=False, fontsize=9)
    fig.savefig(out, dpi=120)
    plt.close(fig)


def main() -> int:
    df = add_quartiles(load())
    summary = summarize(df)

    RESULTS.mkdir(exist_ok=True)
    columns = ["geoid", "median_income", "income_quartile", "population", "share_no_vehicle",
               *[f"jobs_{m}" for m in THRESHOLDS], *[f"share_jobs_{m}" for m in THRESHOLDS],
               "jobs_low_wage_45", "has_transit"]
    df[columns].to_csv(RESULTS / "access.csv", index=False)
    summary.to_csv(RESULTS / "equity.csv", index_label="income_quartile")
    plot_scatter(df, RESULTS / "access_vs_income.png")
    plot_quartile_bars(summary, RESULTS / "access_by_quartile.png")

    with pd.option_context("display.width", 160, "display.max_columns", 20):
        print(summary)
    for minutes in THRESHOLDS:
        print(f"{minutes} min: top/bottom quartile ratio {top_bottom_ratio(summary, minutes):.2f}, "
              f"Spearman rho {spearman(df, minutes):.3f}")
    print(f"tracts without income (left out): {df['income_quartile'].isna().sum()}")
    print("wrote results/access.csv, equity.csv, access_vs_income.png, access_by_quartile.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
