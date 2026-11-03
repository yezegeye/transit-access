#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <stdexcept>
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
