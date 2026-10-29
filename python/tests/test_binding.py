from pathlib import Path

import pytest
import transit_router as tr

TINY = Path(__file__).resolve().parents[2] / "data" / "fixtures" / "tiny"


@pytest.fixture(scope="module")
def tt():
    return tr.Timetable.from_gtfs_dir(TINY)


def t(hms):
    return tr.parse_gtfs_time(hms)


def test_parse_gtfs_time():
    assert t("08:05:30") == 8 * 3600 + 5 * 60 + 30
    assert t("25:00:00") == 25 * 3600
    with pytest.raises(ValueError):
        t("8am")


def test_timetable_metadata(tt):
    assert tt.num_stops == 5
    assert tt.num_connections == 5
    assert tt.stop_ids == ["A", "B", "C", "D", "E"]
    assert tt.stop_index("D") == 3
    with pytest.raises(KeyError):
        tt.stop_index("Z")


def test_earliest_arrival_with_transfer(tt):
    assert tr.earliest_arrival(tt, "A", t("08:00:00")) == {
        "A": t("08:00:00"),
        "B": t("08:10:00"),
        "C": t("08:20:00"),
        "D": t("08:25:00"),
    }


def test_earliest_arrival_after_missing_first_trip(tt):
    assert tr.earliest_arrival(tt, "A", t("08:01:00")) == {
        "A": t("08:01:00"),
        "D": t("08:40:00"),
    }


def test_errors(tt):
    with pytest.raises(KeyError):
        tr.earliest_arrival(tt, "Z", 0)
    with pytest.raises(RuntimeError):
        tr.Timetable.from_gtfs_dir(TINY / "missing")
