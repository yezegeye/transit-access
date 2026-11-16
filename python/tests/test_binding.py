import datetime
from pathlib import Path

import numpy as np
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


def test_stop_coordinates(tt):
    assert tt.stop_lats == [0.0, 0.0, 0.0, 0.01, 0.02]
    assert tt.stop_lons == [0.0, 0.01, 0.02, 0.01, 0.02]


def test_generate_footpaths():
    tt = tr.Timetable.from_gtfs_dir(TINY)
    assert tt.num_footpaths == 0
    assert tt.generate_footpaths(1200, 1.0) == 6
    assert tt.num_footpaths == 6
    assert tt.footpaths("B") == {"A": 1112, "C": 1112, "D": 1112}
    assert tt.footpaths("E") == {}
    with pytest.raises(KeyError):
        tt.footpaths("Z")
    with pytest.raises(ValueError):
        tt.generate_footpaths(400, 0)


def test_earliest_arrival_with_transfer(tt):
    assert tr.earliest_arrival(tt, "A", t("08:00:00")) == {
        "A": t("08:00:00"),
        "B": t("08:10:00"),
        "C": t("08:20:00"),
        "D": t("08:25:00"),
    }


def test_earliest_arrival_uses_footpaths():
    tt = tr.Timetable.from_gtfs_dir(TINY)
    tt.generate_footpaths(1200, 1.3)
    reached = tr.earliest_arrival(tt, "A", t("08:00:00"))
    assert reached["D"] == t("08:10:00") + 856  # ride T1 to B, then walk to D
    assert "E" not in reached


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


@pytest.mark.parametrize("date", [datetime.date(2026, 10, 14), "20261014"])
def test_service_day_accepts_date_or_string(date):
    weekday = tr.Timetable.from_gtfs_dir(TINY, date=date)
    assert weekday.num_connections == 3
    assert tr.earliest_arrival(weekday, "A", t("08:01:00")) == {"A": t("08:01:00")}


def test_service_day_filters_trips():
    saturday = tr.Timetable.from_gtfs_dir(TINY, datetime.date(2026, 10, 17))
    assert tr.earliest_arrival(saturday, "A", t("08:00:00")) == {
        "A": t("08:00:00"),
        "D": t("08:40:00"),
    }
    sunday = tr.Timetable.from_gtfs_dir(TINY, datetime.date(2026, 10, 18))
    assert sunday.num_connections == 0


def test_invalid_service_day():
    with pytest.raises(ValueError):
        tr.Timetable.from_gtfs_dir(TINY, "2026-10-14")


def test_travel_time_matrix(tt):
    a, c, d, e = (0.0, 0.0), (0.0, 0.02), (0.01, 0.01), (0.02, 0.02)
    m = tr.travel_time_matrix(
        tt, [a, c], np.array([d, e]), [t("08:00:00"), t("08:01:00")], max_access_m=100
    )
    assert m.shape == (2, 2, 2) and m.dtype == np.float32
    assert m[0, 0, 0] == 25 * 60  # A -> D leaving 08:00
    assert m[0, 1, 0] == 39 * 60  # leaving 08:01 misses T1
    assert m[1, 0, 0] == 35 * 60  # C -> D waits for T4
    assert np.isinf(m[:, :, 1]).all()  # nothing reaches E
    # A median over departure times is the usual accessibility summary.
    assert np.median(m[0, :, 0]) == 32 * 60

    capped = tr.travel_time_matrix(
        tt, [a], [d], [t("08:00:00"), t("08:01:00")], max_access_m=100, max_travel_time=1800
    )
    assert capped[0, 0, 0] == 1500 and np.isinf(capped[0, 1, 0])


def test_travel_time_matrix_errors(tt):
    with pytest.raises(ValueError):
        tr.travel_time_matrix(tt, [[0.0, 0.0, 0.0]], [(0.0, 0.0)], [0])
    with pytest.raises(ValueError):
        tr.travel_time_matrix(tt, [(0.0, 0.0)], [(0.0, 0.0)], [0], walk_speed_mps=0)
    assert tr.travel_time_matrix(tt, np.empty((0, 2)), [(0.0, 0.0)], [0]).shape == (0, 1, 1)


def test_journey(tt):
    legs = tr.journey(tt, "A", t("08:00:00"), "D")
    assert [(leg["mode"], leg["trip_id"], leg["from_stop"], leg["to_stop"]) for leg in legs] == [
        ("ride", "T1", "A", "B"),
        ("ride", "T2", "B", "D"),
    ]
    assert legs[-1]["arrival"] == t("08:25:00")
    assert tr.journey(tt, "A", t("08:00:00"), "E") == []


def test_min_transfer_time():
    tt = tr.Timetable.from_gtfs_dir(TINY)
    assert tt.min_transfer_time == 0
    tt.min_transfer_time = 301  # T1 -> T2 at B is a 5-minute connection
    assert tr.earliest_arrival(tt, "A", t("08:00:00"))["D"] == t("08:35:00")
    with pytest.raises(ValueError):
        tt.min_transfer_time = -1


def test_arrival_times(tt):
    arr = tr.arrival_times(tt, "A", t("08:00:00"))
    assert arr.dtype == np.int32 and arr.shape == (5,)
    assert arr.tolist() == [t("08:00:00"), t("08:10:00"), t("08:20:00"), t("08:25:00"), tr.UNREACHABLE]
    capped = tr.arrival_times(tt, "A", t("08:00:00"), max_seconds=20 * 60)
    assert capped.tolist()[3] == tr.UNREACHABLE  # D at 08:25 is beyond 20 minutes
    assert capped.tolist()[2] == t("08:20:00")


def test_connection_arrays(tt):
    c = tr.connection_arrays(tt)
    assert len(c["dep_time"]) == tt.num_connections == 5
    assert (np.diff(c["dep_time"]) >= 0).all()
