#pragma once
#include <cstdint>
#include <istream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace transit {

// Arrival time reported for stops that cannot be reached.
constexpr int32_t kUnreachable = std::numeric_limits<int32_t>::max();

// Parses a GTFS time ("H:MM:SS" or "HH:MM:SS") into seconds after midnight.
// Hours may exceed 23 for trips that run past midnight.
int32_t parse_gtfs_time(const std::string& text);

// Parses a GTFS date ("YYYYMMDD") and returns it as the integer YYYYMMDD.
// Throws std::invalid_argument for malformed or impossible dates.
int32_t parse_gtfs_date(const std::string& text);

// service_ids that run on `date` (YYYYMMDD): calendar.txt weekday flags within
// start_date..end_date, then calendar_dates.txt exceptions (1 adds, 2 removes).
// Either stream may be null; GTFS requires at least one of the two files.
std::unordered_set<std::string> active_services(std::istream* calendar,
                                                std::istream* calendar_dates, int32_t date);

// trip_ids in trips.txt whose service_id is in `services`.
std::unordered_set<std::string> active_trips(std::istream& trips,
                                             const std::unordered_set<std::string>& services);

// One vehicle hop between consecutive stops of a trip.
struct Connection {
    int32_t dep_stop;
    int32_t arr_stop;
    int32_t dep_time;
    int32_t arr_time;
    int32_t trip;
};

class Timetable {
public:
    // Reads stops.txt and stop_times.txt from a GTFS directory.
    static Timetable from_gtfs_dir(const std::string& dir);
    static Timetable from_gtfs(std::istream& stops, std::istream& stop_times);

    int32_t num_stops() const { return static_cast<int32_t>(stop_ids_.size()); }
    const std::vector<std::string>& stop_ids() const { return stop_ids_; }
    // Throws std::out_of_range for an unknown stop_id.
    int32_t stop_index(const std::string& stop_id) const;

    // Sorted by departure time.
    const std::vector<Connection>& connections() const { return connections_; }

private:
    std::vector<std::string> stop_ids_;
    std::unordered_map<std::string, int32_t> stop_index_;
    std::vector<Connection> connections_;
};

// Connection Scan Algorithm: earliest arrival time at every stop when leaving
// `source` at `departure_time`. Transfers are instantaneous and there are no
// footpaths between stops. Unreachable stops get kUnreachable.
std::vector<int32_t> earliest_arrival(const Timetable& tt, int32_t source,
                                      int32_t departure_time);

}  // namespace transit
