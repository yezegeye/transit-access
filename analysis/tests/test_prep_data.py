import io

import pandas as pd
import pytest

from analysis.prep_data import jobs_by_tract, read_acs_table, read_centroids


def test_read_centroids_keeps_one_county_and_builds_geoids():
    text = (
        "﻿STATEFP,COUNTYFP,TRACTCE,POPULATION,LATITUDE,LONGITUDE\n"
        "17,031,010100,4500,+42.021,-087.669\n"
        "17,043,840000,3000,+41.900,-088.100\n"
        "17,031,839100,9000,+41.880,-087.630\n"
    )
    df = read_centroids(io.StringIO(text))
    assert df["geoid"].tolist() == ["17031010100", "17031839100"]
    assert df["lat"].tolist() == [42.021, 41.88]
    assert df["lon"].tolist() == [-87.669, -87.63]
    assert df["pop_2020"].tolist() == [4500, 9000]


def test_jobs_by_tract_sums_blocks_and_drops_other_counties():
    text = (
        "w_geocode,C000,CE01,CE02,CE03,CNS01\n"
        "170318391001085,100,10,30,60,0\n"
        "170318391002008,50,5,15,30,0\n"
        "170310101001000,7,7,0,0,0\n"
        "170430001001000,999,0,0,999,0\n"  # DuPage County
    )
    df = jobs_by_tract(io.StringIO(text)).set_index("geoid")
    assert df.index.tolist() == ["17031010100", "17031839100"]
    assert df.loc["17031839100"].to_dict() == {
        "jobs": 150, "jobs_low_wage": 15, "jobs_mid_wage": 45, "jobs_high_wage": 90,
    }
    assert df.loc["17031010100", "jobs"] == 7


def test_read_acs_table_keeps_tracts_and_blanks_census_codes():
    text = (
        "GEO_ID|B19013_E001|B19013_M001\n"
        "0100000US|80734|166\n"                       # nation
        "0500000US17031|78304|412\n"                   # county
        "1400000US17031010100|74145|3495\n"
        "1400000US17031250600|-666666666|-222222222\n"  # estimate suppressed
        "1400000US17043840000|90000|5000\n"            # another county
    )
    df = read_acs_table(io.StringIO(text))
    assert df["geoid"].tolist() == ["17031010100", "17031250600"]
    assert df.loc[0, "B19013_E001"] == 74145
    assert pd.isna(df.loc[1, "B19013_E001"]) and pd.isna(df.loc[1, "B19013_M001"])
