#pragma once
#include <cmath>
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

// A walk from one stop to another, `duration` seconds long.
struct Footpath {
    int32_t to;
    int32_t duration;
};

class Timetable {
public:
    // Reads stops.txt and stop_times.txt from a GTFS directory. Every trip is
    // loaded, whatever day it runs.
    static Timetable from_gtfs_dir(const std::string& dir);
    // Loads only the trips running on `service_date` (YYYYMMDD), using
    // trips.txt, calendar.txt and calendar_dates.txt.
    static Timetable from_gtfs_dir(const std::string& dir, int32_t service_date);
    static Timetable from_gtfs(std::istream& stops, std::istream& stop_times);
    // Rows of stop_times.txt whose trip_id is not in `trips` are skipped.
    static Timetable from_gtfs(std::istream& stops, std::istream& stop_times,
                               const std::unordered_set<std::string>& trips);

    int32_t num_stops() const { return static_cast<int32_t>(stop_ids_.size()); }
    const std::vector<std::string>& stop_ids() const { return stop_ids_; }
    // WGS84 coordinates from stops.txt, parallel to stop_ids(). NaN when the
    // stop_lat/stop_lon columns are absent or a stop leaves them empty (GTFS
    // allows that for generic nodes and boarding areas).
    const std::vector<double>& stop_lats() const { return stop_lats_; }
    const std::vector<double>& stop_lons() const { return stop_lons_; }
    // Throws std::out_of_range for an unknown stop_id.
    int32_t stop_index(const std::string& stop_id) const;

    // Sorted by departure time.
    const std::vector<Connection>& connections() const { return connections_; }

    // Replaces the footpaths with a walk between every pair of stops at most
    // `max_distance_m` apart (straight-line haversine distance), taking
    // distance / `walk_speed_mps` seconds, rounded up. Stops without
    // coordinates get none. Returns the number of footpaths (each direction
    // counts once). Throws std::invalid_argument for a negative distance or a
    // non-positive speed.
    size_t generate_footpaths(double max_distance_m, double walk_speed_mps);
    // Footpaths leaving `stop`, in increasing order of destination stop.
    const std::vector<Footpath>& footpaths(int32_t stop) const { return footpaths_[stop]; }
    size_t num_footpaths() const { return num_footpaths_; }

private:
    static Timetable load(std::istream& stops, std::istream& stop_times,
                          const std::unordered_set<std::string>* trips);

    std::vector<std::string> stop_ids_;
    std::unordered_map<std::string, int32_t> stop_index_;
    std::vector<double> stop_lats_;
    std::vector<double> stop_lons_;
    std::vector<Connection> connections_;
    std::vector<std::vector<Footpath>> footpaths_;  // indexed by stop
    size_t num_footpaths_ = 0;
};

// A way to start a journey: be at `stop` at `time`.
struct Departure {
    int32_t stop;
    int32_t time;
};

// Connection Scan Algorithm: earliest arrival time at every stop when the
// journey can start from any of `sources`. Transfers between vehicles at the
// same stop are instantaneous. After each ride the traveller may take one
// footpath from Timetable::footpaths() (walks are not chained); sources do not
// get a walk, since a caller-supplied source is usually already the end of a
// walk. Connections departing after `latest_departure` are ignored, which
// bounds the scan. Unreachable stops get kUnreachable.
std::vector<int32_t> earliest_arrival(const Timetable& tt, const std::vector<Departure>& sources,
                                      int32_t latest_departure = kUnreachable);

// Earliest arrival at every stop when leaving stop `source` at
// `departure_time`. The traveller may first walk one footpath from `source`,
// then ride, walking one footpath after each ride.
std::vector<int32_t> earliest_arrival(const Timetable& tt, int32_t source,
                                      int32_t departure_time);

}  // namespace transit
