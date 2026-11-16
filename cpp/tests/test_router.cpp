#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>
#include <stdexcept>
#include "geo.hpp"
#include "router.hpp"

using transit::active_services;
using transit::earliest_arrival;
using transit::kUnreachable;
using transit::parse_gtfs_date;
using transit::parse_gtfs_time;
using transit::Timetable;

// data/fixtures/tiny:
//   T1  A 08:00 -> B 08:10 (dep 08:12) -> C 08:20   WEEKDAY
//   T2  B 08:15 -> D 08:25                          WEEKDAY
//   T3  A 08:05 -> D 08:40  (rows out of order)     SATURDAY
//   T4  C 08:30 -> D 08:35                          HOLIDAY
//   E has no service.
// WEEKDAY and SATURDAY run all of 2026 (calendar.txt). On 2026-11-26
// (Thanksgiving, a Thursday) WEEKDAY is removed and HOLIDAY added
// (calendar_dates.txt).
static Timetable tiny() { return Timetable::from_gtfs_dir(TEST_DATA_DIR "/tiny"); }
static Timetable tiny_on(int32_t date) {
    return Timetable::from_gtfs_dir(TEST_DATA_DIR "/tiny", date);
}

TEST_CASE("parse_gtfs_time") {
    CHECK(parse_gtfs_time("08:00:00") == 8 * 3600);
    CHECK(parse_gtfs_time("8:05:30") == 8 * 3600 + 5 * 60 + 30);
    CHECK(parse_gtfs_time(" 25:10:00 ") == 25 * 3600 + 10 * 60);  // after midnight
    CHECK_THROWS_AS(parse_gtfs_time(""), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_time("08:00"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_time("08:60:00"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_time("08:00:00x"), std::invalid_argument);
}

TEST_CASE("loads the tiny GTFS feed") {
    const Timetable tt = tiny();
    REQUIRE(tt.num_stops() == 5);
    CHECK(tt.stop_ids() == std::vector<std::string>{"A", "B", "C", "D", "E"});
    CHECK(tt.stop_index("D") == 3);
    CHECK_THROWS_AS(tt.stop_index("Z"), std::out_of_range);

    const auto& conns = tt.connections();
    REQUIRE(conns.size() == 5);
    for (size_t i = 1; i < conns.size(); ++i) {
        CHECK(conns[i - 1].dep_time <= conns[i].dep_time);
    }
    // T3's out-of-order rows still produce A -> D.
    CHECK(conns[1].dep_stop == tt.stop_index("A"));
    CHECK(conns[1].arr_stop == tt.stop_index("D"));
    // Hop B -> C leaves at B's departure_time, not its arrival_time.
    CHECK(conns[2].dep_time == parse_gtfs_time("08:12:00"));
}

TEST_CASE("earliest_arrival on the tiny feed") {
    const Timetable tt = tiny();
    const auto at = [&](const std::vector<int32_t>& arr, const char* stop) {
        return arr[tt.stop_index(stop)];
    };

    SECTION("transfer beats the direct trip") {
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:00:00"));
        CHECK(at(arr, "A") == parse_gtfs_time("08:00:00"));
        CHECK(at(arr, "B") == parse_gtfs_time("08:10:00"));
        CHECK(at(arr, "C") == parse_gtfs_time("08:20:00"));
        CHECK(at(arr, "D") == parse_gtfs_time("08:25:00"));  // T1 then T2 at B
        CHECK(at(arr, "E") == kUnreachable);
    }

    SECTION("missing the first trip falls back to the direct trip") {
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:01:00"));
        CHECK(at(arr, "B") == kUnreachable);
        CHECK(at(arr, "C") == kUnreachable);
        CHECK(at(arr, "D") == parse_gtfs_time("08:40:00"));
    }

    SECTION("a connection departing exactly at the start time can be boarded") {
        const auto arr = earliest_arrival(tt, tt.stop_index("C"), parse_gtfs_time("08:30:00"));
        CHECK(at(arr, "D") == parse_gtfs_time("08:35:00"));
        CHECK(at(arr, "A") == kUnreachable);
        CHECK(at(arr, "B") == kUnreachable);
    }

    SECTION("after the last departure only the source is reached") {
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("09:00:00"));
        CHECK(at(arr, "A") == parse_gtfs_time("09:00:00"));
        for (const char* stop : {"B", "C", "D", "E"}) CHECK(at(arr, stop) == kUnreachable);
    }

    SECTION("invalid source") {
        CHECK_THROWS_AS(earliest_arrival(tt, 99, 0), std::out_of_range);
    }
}

TEST_CASE("zero-duration hops are followed within one scan") {
    std::istringstream stops("stop_id\nX\nY\nZ\n");
    std::istringstream stop_times(
        "trip_id,arrival_time,departure_time,stop_id,stop_sequence\n"
        "T,10:00:00,10:00:00,X,1\n"
        "T,10:00:00,10:00:00,Y,2\n"
        "T,10:00:00,10:00:00,Z,3\n");
    const Timetable tt = Timetable::from_gtfs(stops, stop_times);
    const auto arr = earliest_arrival(tt, tt.stop_index("X"), parse_gtfs_time("09:00:00"));
    CHECK(arr[tt.stop_index("Z")] == parse_gtfs_time("10:00:00"));
}

TEST_CASE("GTFS parsing tolerates BOM, CRLF, quotes and column order") {
    std::istringstream stops("\xEF\xBB\xBFstop_name,stop_id\r\n\"Main St, North\",S1\r\n\"Elm\",S2\r\n\r\n");
    std::istringstream stop_times(
        "stop_sequence,stop_id,departure_time,arrival_time,trip_id\r\n"
        "1,S1,23:50:00,23:50:00,\"night\"\r\n"
        "2,S2,24:05:00,24:05:00,\"night\"\r\n");
    const Timetable tt = Timetable::from_gtfs(stops, stop_times);
    CHECK(tt.stop_ids() == std::vector<std::string>{"S1", "S2"});
    const auto arr = earliest_arrival(tt, 0, parse_gtfs_time("23:00:00"));
    CHECK(arr[1] == parse_gtfs_time("24:05:00"));
}

TEST_CASE("GTFS parsing rejects bad input") {
    const std::string header = "trip_id,arrival_time,departure_time,stop_id,stop_sequence\n";
    const auto load = [](const std::string& stops_txt, const std::string& stop_times_txt) {
        std::istringstream stops(stops_txt), stop_times(stop_times_txt);
        return Timetable::from_gtfs(stops, stop_times);
    };
    CHECK_THROWS_AS(load("stop_id\nA\nA\n", header), std::runtime_error);            // duplicate
    CHECK_THROWS_AS(load("stop_id\nA\n", header + "T,08:00:00,08:00:00,B,1\n"),     // unknown stop
                    std::runtime_error);
    CHECK_THROWS_AS(load("stop_id\nA\n", header + "T,,,A,1\n"), std::runtime_error);  // no times
    CHECK_THROWS_AS(load("stop_id\nA\n", "trip_id,stop_id\n"), std::runtime_error);   // columns
    CHECK_THROWS_AS(Timetable::from_gtfs_dir("/nonexistent"), std::runtime_error);
    CHECK_THROWS_AS(Timetable::from_gtfs_dir("/nonexistent", 20261014), std::runtime_error);
}

TEST_CASE("stop coordinates") {
    const Timetable tt = tiny();
    REQUIRE(tt.stop_lats().size() == 5);
    REQUIRE(tt.stop_lons().size() == 5);
    CHECK(tt.stop_lats()[tt.stop_index("D")] == 0.01);
    CHECK(tt.stop_lons()[tt.stop_index("C")] == 0.02);

    const auto load = [](const std::string& stops_txt) {
        std::istringstream stops(stops_txt), stop_times("trip_id,arrival_time,departure_time,stop_id,stop_sequence\n");
        return Timetable::from_gtfs(stops, stop_times);
    };
    SECTION("columns are optional") {
        const Timetable no_coords = load("stop_id\nA\n");
        CHECK(std::isnan(no_coords.stop_lats()[0]));
        CHECK(std::isnan(no_coords.stop_lons()[0]));
    }
    SECTION("empty or half-missing coordinates become NaN") {
        const Timetable partial = load("stop_id,stop_lat,stop_lon\nA,,\nB,41.9,\nC,41.9,-87.6\n");
        CHECK(std::isnan(partial.stop_lats()[0]));
        CHECK(std::isnan(partial.stop_lats()[1]));
        CHECK(std::isnan(partial.stop_lons()[1]));
        CHECK(partial.stop_lats()[2] == 41.9);
        CHECK(partial.stop_lons()[2] == -87.6);
    }
    SECTION("malformed or out-of-range coordinates are rejected") {
        CHECK_THROWS_AS(load("stop_id,stop_lat,stop_lon\nA,north,-87.6\n"), std::runtime_error);
        CHECK_THROWS_AS(load("stop_id,stop_lat,stop_lon\nA,41.9x,-87.6\n"), std::runtime_error);
        CHECK_THROWS_AS(load("stop_id,stop_lat,stop_lon\nA,91,-87.6\n"), std::runtime_error);
        CHECK_THROWS_AS(load("stop_id,stop_lat,stop_lon\nA,41.9,-181\n"), std::runtime_error);
    }
}

TEST_CASE("generate_footpaths") {
    Timetable tt = tiny();
    CHECK(tt.num_footpaths() == 0);
    for (int32_t s = 0; s < tt.num_stops(); ++s) CHECK(tt.footpaths(s).empty());

    // B's neighbours A, C and D are 0.01 degrees (about 1112 m) away; every
    // other pair is at least about 1572 m apart, so E is out of reach.
    const auto id = [&](const char* stop) { return tt.stop_index(stop); };
    CHECK(tt.generate_footpaths(1000, 1.0) == 0);
    REQUIRE(tt.generate_footpaths(1200, 1.0) == 6);  // A-B, B-C, B-D both ways
    const auto& from_b = tt.footpaths(id("B"));
    REQUIRE(from_b.size() == 3);
    CHECK(from_b[0].to == id("A"));
    CHECK(from_b[1].to == id("C"));
    CHECK(from_b[2].to == id("D"));
    CHECK(from_b[0].duration == 1112);  // 1111.95 m at 1 m/s, rounded up
    CHECK(tt.footpaths(id("A")).size() == 1);
    CHECK(tt.footpaths(id("E")).empty());

    // Regenerating replaces the old footpaths; durations scale with speed.
    tt.generate_footpaths(1200, 2.0);
    CHECK(tt.num_footpaths() == 6);
    CHECK(tt.footpaths(id("B"))[0].duration == 556);

    CHECK_THROWS_AS(tt.generate_footpaths(-1, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(tt.generate_footpaths(400, 0.0), std::invalid_argument);
}

TEST_CASE("stops without coordinates get no footpaths") {
    std::istringstream stops("stop_id,stop_lat,stop_lon\nA,41.9,-87.6\nB,,\nC,41.9,-87.6001\n");
    std::istringstream stop_times("trip_id,arrival_time,departure_time,stop_id,stop_sequence\n");
    Timetable tt = Timetable::from_gtfs(stops, stop_times);
    CHECK(tt.generate_footpaths(400, 1.3) == 2);  // A <-> C only
    CHECK(tt.footpaths(1).empty());
    CHECK(tt.footpaths(0)[0].duration == 7);  // about 8.3 m at 1.3 m/s
}

TEST_CASE("earliest_arrival with footpaths on the tiny feed") {
    Timetable tt = tiny();
    const auto at = [&](const std::vector<int32_t>& arr, const char* stop) {
        return arr[tt.stop_index(stop)];
    };
    const int32_t walk_1_3 = 856;  // 1111.95 m at 1.3 m/s, rounded up

    SECTION("walking after a ride beats waiting for the next vehicle") {
        tt.generate_footpaths(1200, 1.3);
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:00:00"));
        CHECK(at(arr, "B") == parse_gtfs_time("08:10:00"));
        CHECK(at(arr, "C") == parse_gtfs_time("08:20:00"));
        // T1 to B at 08:10, then walk to D, beating T2's 08:25 arrival.
        CHECK(at(arr, "D") == parse_gtfs_time("08:10:00") + walk_1_3);
        CHECK(at(arr, "E") == kUnreachable);
    }

    SECTION("walking from the source catches a trip that was already missed at it") {
        tt.generate_footpaths(1200, 2.0);  // A -> B takes 556 s
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:01:00"));
        // At B by 08:10:16: board T1 (dep 08:12) to C and T2 (dep 08:15) to D.
        CHECK(at(arr, "B") == parse_gtfs_time("08:01:00") + 556);
        CHECK(at(arr, "C") == parse_gtfs_time("08:20:00"));
        CHECK(at(arr, "D") == parse_gtfs_time("08:25:00"));
    }

    SECTION("a walk too slow to catch the trip changes nothing") {
        tt.generate_footpaths(1200, 1.3);
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:01:00"));
        CHECK(at(arr, "B") == parse_gtfs_time("08:01:00") + walk_1_3);  // 08:15:16, T2 gone
        CHECK(at(arr, "C") == kUnreachable);
        CHECK(at(arr, "D") == parse_gtfs_time("08:40:00"));
    }
}

// S, Q and R lie due north of each other about 111 m apart, so with a 150 m
// radius S-Q and Q-R are footpaths but S-R is not. Trip T rides S -> Q.
static Timetable line_feed() {
    std::istringstream stops(
        "stop_id,stop_lat,stop_lon\nS,41.9000,-87.6\nQ,41.9010,-87.6\nR,41.9020,-87.6\n");
    std::istringstream stop_times(
        "trip_id,arrival_time,departure_time,stop_id,stop_sequence\n"
        "T,10:00:00,10:00:00,S,1\n"
        "T,10:10:00,10:10:00,Q,2\n");
    Timetable tt = Timetable::from_gtfs(stops, stop_times);
    tt.generate_footpaths(150, 1.0);
    return tt;
}

TEST_CASE("footpaths are not chained") {
    const Timetable tt = line_feed();
    const int32_t walk = tt.footpaths(0)[0].duration;  // S -> Q
    REQUIRE(walk == 112);
    // Leaving S after T has gone: Q is a walk away, R would need two walks.
    const auto arr = earliest_arrival(tt, tt.stop_index("S"), parse_gtfs_time("10:05:00"));
    CHECK(arr[tt.stop_index("Q")] == parse_gtfs_time("10:05:00") + walk);
    CHECK(arr[tt.stop_index("R")] == kUnreachable);
}

TEST_CASE("a ride ending where a walk already reached still allows a walk on") {
    const Timetable tt = line_feed();
    // Walking reaches Q at 09:59:52, but only riding T to Q (10:10) gives a
    // ride to walk on from, so R is reached at 10:10 + 112 s.
    const auto arr = earliest_arrival(tt, tt.stop_index("S"), parse_gtfs_time("09:58:00"));
    CHECK(arr[tt.stop_index("Q")] == parse_gtfs_time("09:58:00") + 112);
    CHECK(arr[tt.stop_index("R")] == parse_gtfs_time("10:10:00") + 112);
}

TEST_CASE("earliest_arrival from several sources") {
    const Timetable tt = tiny();
    const auto id = [&](const char* stop) { return tt.stop_index(stop); };
    using transit::Departure;

    // At C by 08:30 (T4 to D 08:35) or at B by 08:15 (T2 to D 08:25).
    auto arr = earliest_arrival(tt, {{id("C"), parse_gtfs_time("08:30:00")},
                                     {id("B"), parse_gtfs_time("08:14:00")}});
    CHECK(arr[id("B")] == parse_gtfs_time("08:14:00"));
    CHECK(arr[id("D")] == parse_gtfs_time("08:25:00"));
    // Duplicate sources keep the earliest time.
    arr = earliest_arrival(tt, {{id("B"), parse_gtfs_time("08:20:00")},
                                {id("B"), parse_gtfs_time("08:14:00")}});
    CHECK(arr[id("B")] == parse_gtfs_time("08:14:00"));
    // latest_departure drops T2 (08:15) but keeps nothing else useful.
    arr = earliest_arrival(tt, {{id("B"), parse_gtfs_time("08:14:00")}},
                           parse_gtfs_time("08:14:59"));
    CHECK(arr[id("D")] == kUnreachable);
    CHECK(earliest_arrival(tt, std::vector<Departure>{}) ==
          std::vector<int32_t>(tt.num_stops(), kUnreachable));
    CHECK_THROWS_AS(earliest_arrival(tt, {{99, 0}}), std::out_of_range);
}

TEST_CASE("parse_gtfs_date") {
    CHECK(parse_gtfs_date("20261014") == 20261014);
    CHECK(parse_gtfs_date(" 20240229 ") == 20240229);  // leap day
    CHECK_THROWS_AS(parse_gtfs_date("20230229"), std::invalid_argument);  // not a leap year
    CHECK_THROWS_AS(parse_gtfs_date("20261301"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_date("20261000"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_date("2026-10-14"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_date("2026101"), std::invalid_argument);
    CHECK_THROWS_AS(parse_gtfs_date(""), std::invalid_argument);
}

TEST_CASE("active_services applies weekdays, date ranges and exceptions") {
    const std::string calendar_txt =
        "service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date\n"
        "WK,1,1,1,1,1,0,0,20261001,20261031\n"
        "SUN,0,0,0,0,0,0,1,20261001,20261031\n";
    const std::string dates_txt =
        "service_id,date,exception_type\n"
        "WK,20261012,2\n"      // Monday: weekday service removed
        "SPECIAL,20261012,1\n"  // and a special service added
        "SUN,20261013,1\n";     // Tuesday: Sunday service added
    const auto services = [&](int32_t date) {
        std::istringstream calendar(calendar_txt), dates(dates_txt);
        return active_services(&calendar, &dates, date);
    };
    using Set = std::unordered_set<std::string>;
    CHECK(services(20261014) == Set{"WK"});                 // Wednesday
    CHECK(services(20261018) == Set{"SUN"});                // Sunday
    CHECK(services(20261017).empty());                      // Saturday
    CHECK(services(20261001) == Set{"WK"});                 // start_date is inclusive
    CHECK(services(20261031).empty());                      // end_date is a Saturday
    CHECK(services(20261102).empty());                      // after end_date
    CHECK(services(20261012) == Set{"SPECIAL"});            // removed + added
    CHECK(services(20261013) == Set{"WK", "SUN"});          // added on top

    SECTION("either file may be missing") {
        std::istringstream calendar(calendar_txt), dates(dates_txt);
        CHECK(active_services(&calendar, nullptr, 20261012) == Set{"WK"});
        CHECK(active_services(nullptr, &dates, 20261012) == Set{"SPECIAL"});
    }

    SECTION("bad rows are rejected") {
        std::istringstream bad_type("service_id,date,exception_type\nX,20261012,3\n");
        CHECK_THROWS_AS(active_services(nullptr, &bad_type, 20261012), std::runtime_error);
        std::istringstream bad_date("service_id,date,exception_type\nX,2026-10-12,1\n");
        CHECK_THROWS_AS(active_services(nullptr, &bad_date, 20261012), std::runtime_error);
    }
}

TEST_CASE("loading a service day keeps only the trips running that day") {
    const auto at = [](const Timetable& tt, const std::vector<int32_t>& arr, const char* stop) {
        return arr[tt.stop_index(stop)];
    };

    SECTION("Wednesday: weekday trips only, so A -> D goes T1 then T2") {
        const Timetable tt = tiny_on(20261014);
        CHECK(tt.num_stops() == 5);  // stops are kept even without service
        CHECK(tt.connections().size() == 3);
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:00:00"));
        CHECK(at(tt, arr, "D") == parse_gtfs_time("08:25:00"));
        // Without T3 there is no later way out of A.
        const auto late = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:01:00"));
        CHECK(at(tt, late, "D") == kUnreachable);
    }

    SECTION("Saturday: only the direct trip T3") {
        const Timetable tt = tiny_on(20261017);
        CHECK(tt.connections().size() == 1);
        const auto arr = earliest_arrival(tt, tt.stop_index("A"), parse_gtfs_time("08:00:00"));
        CHECK(at(tt, arr, "B") == kUnreachable);
        CHECK(at(tt, arr, "D") == parse_gtfs_time("08:40:00"));
    }

    SECTION("Thanksgiving: weekday service replaced by the holiday trip") {
        const Timetable tt = tiny_on(20261126);
        CHECK(tt.connections().size() == 1);
        const auto arr = earliest_arrival(tt, tt.stop_index("C"), parse_gtfs_time("08:00:00"));
        CHECK(at(tt, arr, "D") == parse_gtfs_time("08:35:00"));
    }

    SECTION("no service on Sunday or outside the calendar range") {
        CHECK(tiny_on(20261018).connections().empty());
        CHECK(tiny_on(20270104).connections().empty());
    }
}

// Reference answer by repeated relaxation in arbitrary order until nothing
// changes. It shares the one-walk-per-ride rule but not the scan's reliance on
// processing connections in departure order.
static std::vector<int32_t> fixpoint_arrival(const Timetable& tt, int32_t source, int32_t t0) {
    std::vector<int32_t> arrival(tt.num_stops(), kUnreachable), ride(tt.num_stops(), kUnreachable);
    arrival[source] = t0;
    for (const auto& w : tt.footpaths(source)) arrival[w.to] = std::min(arrival[w.to], t0 + w.duration);
    for (bool changed = true; changed;) {
        changed = false;
        for (auto c = tt.connections().rbegin(); c != tt.connections().rend(); ++c) {
            if (c->dep_time < t0 || arrival[c->dep_stop] > c->dep_time || c->arr_time >= ride[c->arr_stop]) continue;
            ride[c->arr_stop] = c->arr_time;
            arrival[c->arr_stop] = std::min(arrival[c->arr_stop], c->arr_time);
            for (const auto& w : tt.footpaths(c->arr_stop)) {
                arrival[w.to] = std::min(arrival[w.to], c->arr_time + w.duration);
            }
            changed = true;
        }
    }
    return arrival;
}

// A random feed: 30 stops scattered over about 1.1 km square and 60 trips of
// 2-6 stops between 08:00 and 09:00, with 300 m footpaths at 1.3 m/s.
static Timetable random_feed(std::mt19937& rng) {
    const int n_stops = 30;
    std::uniform_real_distribution<double> coord(0.0, 0.01);
    std::string stops = "stop_id,stop_lat,stop_lon\n";
    for (int s = 0; s < n_stops; ++s) {
        stops += "S" + std::to_string(s) + "," + std::to_string(41.9 + coord(rng)) + "," +
                 std::to_string(-87.6 + coord(rng)) + "\n";
    }
    std::string times = "trip_id,arrival_time,departure_time,stop_id,stop_sequence\n";
    std::uniform_int_distribution<int> stop(0, n_stops - 1), start(8 * 3600, 9 * 3600),
        hop(30, 600), dwell(0, 60), len(2, 6);
    const auto hms = [](int t) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
        return std::string(buf);
    };
    for (int trip = 0; trip < 60; ++trip) {
        int t = start(rng);
        const int n = len(rng);
        for (int k = 0; k < n; ++k) {
            const int arr = t;
            const int dep = arr + (k + 1 < n ? dwell(rng) : 0);
            times += "T" + std::to_string(trip) + "," + hms(arr) + "," + hms(dep) + ",S" +
                     std::to_string(stop(rng)) + "," + std::to_string(k + 1) + "\n";
            t = dep + hop(rng);
        }
    }
    std::istringstream stops_in(stops), times_in(times);
    Timetable tt = Timetable::from_gtfs(stops_in, times_in);
    tt.generate_footpaths(300, 1.3);
    return tt;
}

TEST_CASE("scan matches a fixpoint reference on random feeds") {
    std::mt19937 rng(11);
    for (int feed = 0; feed < 40; ++feed) {
        const Timetable tt = random_feed(rng);
        std::uniform_int_distribution<int> stop(0, tt.num_stops() - 1), start(8 * 3600, 9 * 3600);
        for (int q = 0; q < 10; ++q) {
            const int32_t source = stop(rng), t0 = start(rng);
            CHECK(earliest_arrival(tt, source, t0) == fixpoint_arrival(tt, source, t0));
        }
    }
}

// Leaving later never gets you anywhere earlier: whatever a later departure
// can do, an earlier one can too by waiting (the FIFO property).
TEST_CASE("leaving later never arrives earlier") {
    std::mt19937 rng(23);
    for (int feed = 0; feed < 20; ++feed) {
        const Timetable tt = random_feed(rng);
        std::uniform_int_distribution<int> stop(0, tt.num_stops() - 1);
        for (int q = 0; q < 5; ++q) {
            const int32_t source = stop(rng);
            auto prev = earliest_arrival(tt, source, 7 * 3600 + 50 * 60);
            for (int32_t t0 = 7 * 3600 + 51 * 60; t0 <= 9 * 3600; t0 += 60) {
                const auto cur = earliest_arrival(tt, source, t0);
                for (int32_t s = 0; s < tt.num_stops(); ++s) {
                    // kUnreachable is the largest int32, so it counts as latest.
                    REQUIRE(prev[s] <= cur[s]);
                }
                prev = cur;
            }
        }

        // The same holds door to door: departure + travel time never falls.
        using transit::LatLon;
        std::vector<LatLon> points;
        for (int32_t s = 0; s < tt.num_stops(); s += 3) {
            points.push_back({tt.stop_lats()[s] + 0.0005, tt.stop_lons()[s]});  // ~55 m away
        }
        std::vector<int32_t> deps;
        for (int32_t t0 = 7 * 3600 + 50 * 60; t0 <= 9 * 3600; t0 += 120) deps.push_back(t0);
        transit::MatrixOptions opt;
        opt.max_access_m = 300;
        const auto m = transit::travel_time_matrix(tt, points, points, deps, opt);
        const size_t n = points.size();
        for (size_t o = 0; o < n; ++o) {
            for (size_t d = 0; d < n; ++d) {
                for (size_t k = 1; k < deps.size(); ++k) {
                    const auto at = [&](size_t i) {
                        const int32_t v = m[(o * deps.size() + i) * n + d];
                        return v == kUnreachable ? int64_t{kUnreachable} : int64_t{deps[i]} + v;
                    };
                    REQUIRE(at(k - 1) <= at(k));
                }
            }
        }
    }
}

TEST_CASE("travel_time_matrix on the tiny feed") {
    using transit::LatLon;
    using transit::MatrixOptions;
    using transit::travel_time_matrix;
    Timetable tt = tiny();
    const LatLon a{0.0, 0.0}, c{0.0, 0.02}, d{0.01, 0.01}, e{0.02, 0.02};
    const int32_t t0800 = parse_gtfs_time("08:00:00"), t0801 = parse_gtfs_time("08:01:00");
    MatrixOptions opt;
    opt.max_access_m = 100;  // only the stop under each point

    SECTION("layout is [origin][departure][destination]") {
        const auto m = travel_time_matrix(tt, {a, c}, {d, e}, {t0800, t0801}, opt);
        REQUIRE(m.size() == 2 * 2 * 2);
        CHECK(m[0] == 25 * 60);            // A 08:00 -> D 08:25 (T1, T2)
        CHECK(m[1] == kUnreachable);       // nothing reaches E
        CHECK(m[2] == 39 * 60);            // A 08:01 -> D 08:40 (T3)
        CHECK(m[3] == kUnreachable);
        CHECK(m[4] == 35 * 60);            // C 08:00 -> D 08:35 (waits for T4)
        CHECK(m[5] == kUnreachable);
    }

    SECTION("access and egress walks are added") {
        // 0.00045 degrees of latitude north of A is about 50 m.
        const LatLon near_a{0.00045, 0.0}, near_d{0.01045, 0.01};
        const int32_t walk = static_cast<int32_t>(std::ceil(transit::haversine_m(0.00045, 0, 0, 0) / 1.3));
        const auto m = travel_time_matrix(tt, {near_a}, {near_d}, {t0800 - walk}, opt);
        CHECK(m[0] == 25 * 60 + 2 * walk);
        // Leaving one second later misses T1 at A.
        CHECK(travel_time_matrix(tt, {near_a}, {near_d}, {t0800 - walk + 1}, opt)[0] ==
              parse_gtfs_time("08:40:00") + walk - (t0800 - walk + 1));
    }

    SECTION("walking directly and staying put") {
        CHECK(travel_time_matrix(tt, {a}, {a}, {t0800}, opt)[0] == 0);
        opt.max_access_m = 1200;
        tt.generate_footpaths(1200, 1.3);
        // A and B are 1112 m apart. Riding T1 takes 600 s; once T1 has left,
        // walking (856 s) is the fastest way.
        const LatLon b{0.0, 0.01};
        CHECK(travel_time_matrix(tt, {a}, {b}, {t0800}, opt)[0] == 600);
        CHECK(travel_time_matrix(tt, {a}, {b}, {t0801}, opt)[0] == 856);
    }

    SECTION("max_travel_time") {
        opt.max_travel_time = 30 * 60;
        const auto m = travel_time_matrix(tt, {a}, {d}, {t0800, t0801}, opt);
        CHECK(m[0] == 25 * 60);
        CHECK(m[1] == kUnreachable);  // 39 minutes
    }

    SECTION("points without coordinates and empty inputs") {
        const auto m = travel_time_matrix(tt, {{NAN, NAN}}, {d}, {t0800}, opt);
        CHECK(m == std::vector<int32_t>{kUnreachable});
        CHECK(travel_time_matrix(tt, {}, {d}, {t0800}, opt).empty());
        CHECK(travel_time_matrix(tt, {a}, {d}, {}, opt).empty());
    }

    SECTION("invalid options") {
        MatrixOptions bad = opt;
        bad.walk_speed_mps = 0;
        CHECK_THROWS_AS(travel_time_matrix(tt, {a}, {d}, {t0800}, bad), std::invalid_argument);
        bad = opt;
        bad.max_access_m = -1;
        CHECK_THROWS_AS(travel_time_matrix(tt, {a}, {d}, {t0800}, bad), std::invalid_argument);
        bad = opt;
        bad.max_travel_time = -1;
        CHECK_THROWS_AS(travel_time_matrix(tt, {a}, {d}, {t0800}, bad), std::invalid_argument);
    }
}

TEST_CASE("travel_time_matrix agrees with stop routing and across thread counts") {
    using transit::LatLon;
    using transit::MatrixOptions;
    // The line feed's stops sit about 111 m apart; points on top of them with
    // a 1 m access radius make each point equivalent to its stop.
    const Timetable tt = line_feed();
    const std::vector<LatLon> pts{{41.9000, -87.6}, {41.9010, -87.6}, {41.9020, -87.6}};
    MatrixOptions opt;
    opt.max_access_m = 1;
    const std::vector<int32_t> deps{parse_gtfs_time("09:58:00"), parse_gtfs_time("10:05:00")};
    opt.threads = 1;
    const auto one = travel_time_matrix(tt, pts, pts, deps, opt);
    opt.threads = 3;
    CHECK(travel_time_matrix(tt, pts, pts, deps, opt) == one);
    // Stop routing walks from the source; points with a 1 m radius do not,
    // so compare a destination reached by riding: S 09:58 -> R via T + walk.
    CHECK(one[(0 * 2 + 0) * 3 + 2] == parse_gtfs_time("10:10:00") + 112 - deps[0]);
    CHECK(one[(0 * 2 + 0) * 3 + 0] == 0);
}

TEST_CASE("journey on the tiny feed") {
    using transit::journey;
    Timetable tt = tiny();
    const auto id = [&](const char* stop) { return tt.stop_index(stop); };
    const auto trip = [&](int32_t t) { return tt.trip_ids()[t]; };

    // A 08:00 -> D: T1 to B, then T2 to D.
    auto legs = journey(tt, id("A"), parse_gtfs_time("08:00:00"), id("D"));
    REQUIRE(legs.size() == 2);
    CHECK(trip(legs[0].trip) == "T1");
    CHECK(legs[0].from_stop == id("A"));
    CHECK(legs[0].to_stop == id("B"));
    CHECK(legs[0].arr_time == parse_gtfs_time("08:10:00"));
    CHECK(trip(legs[1].trip) == "T2");
    CHECK(legs[1].dep_time == parse_gtfs_time("08:15:00"));
    CHECK(legs[1].arr_time == parse_gtfs_time("08:25:00"));

    CHECK(journey(tt, id("A"), parse_gtfs_time("08:00:00"), id("E")).empty());
    CHECK(journey(tt, id("A"), parse_gtfs_time("08:00:00"), id("A")).empty());

    // With footpaths: ride T1 to B, then walk to D.
    tt.generate_footpaths(1200, 1.3);
    legs = journey(tt, id("A"), parse_gtfs_time("08:00:00"), id("D"));
    REQUIRE(legs.size() == 2);
    CHECK(trip(legs[0].trip) == "T1");
    CHECK(legs[1].trip == -1);
    CHECK(legs[1].from_stop == id("B"));
    CHECK(legs[1].arr_time == parse_gtfs_time("08:10:00") + 856);
}

TEST_CASE("journeys are consistent with earliest_arrival on random feeds") {
    std::mt19937 rng(31);
    for (int feed = 0; feed < 20; ++feed) {
        const Timetable tt = random_feed(rng);
        std::uniform_int_distribution<int> stop(0, tt.num_stops() - 1), start(8 * 3600, 9 * 3600);
        for (int q = 0; q < 5; ++q) {
            const int32_t source = stop(rng), t0 = start(rng);
            const auto arrival = earliest_arrival(tt, source, t0);
            for (int32_t target = 0; target < tt.num_stops(); ++target) {
                const auto legs = transit::journey(tt, source, t0, target);
                if (target == source || arrival[target] == kUnreachable) {
                    CHECK(legs.empty());
                    continue;
                }
                REQUIRE(!legs.empty());
                CHECK(legs.front().from_stop == source);
                CHECK(legs.front().dep_time >= t0);
                CHECK(legs.back().to_stop == target);
                CHECK(legs.back().arr_time == arrival[target]);
                for (size_t i = 0; i < legs.size(); ++i) {
                    const auto& leg = legs[i];
                    CHECK(leg.dep_time <= leg.arr_time);
                    if (i > 0) {
                        CHECK(legs[i - 1].to_stop == leg.from_stop);
                        CHECK(legs[i - 1].arr_time <= leg.dep_time);
                        CHECK(!(legs[i - 1].trip < 0 && leg.trip < 0));  // no chained walks
                    }
                    if (leg.trip < 0) {
                        bool found = false;
                        for (const auto& f : tt.footpaths(leg.from_stop)) {
                            found |= f.to == leg.to_stop && f.duration == leg.arr_time - leg.dep_time;
                        }
                        CHECK(found);
                    } else {
                        // The trip really leaves from_stop at dep_time and reaches to_stop at arr_time.
                        bool leaves = false, reaches = false;
                        for (const auto& c : tt.connections()) {
                            if (c.trip != leg.trip) continue;
                            leaves |= c.dep_stop == leg.from_stop && c.dep_time == leg.dep_time;
                            reaches |= c.arr_stop == leg.to_stop && c.arr_time == leg.arr_time;
                        }
                        CHECK(leaves);
                        CHECK(reaches);
                    }
                }
            }
        }
    }
}
