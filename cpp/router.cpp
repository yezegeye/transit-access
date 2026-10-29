#include "router.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <tuple>

int32_t add_seconds(int32_t a, int32_t b) { return a + b; }

namespace transit {
namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Splits one CSV record, honouring double-quoted fields ("" escapes a quote).
// Quoted fields spanning several lines are not supported.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else {
                field += ch;
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += ch;
        }
    }
    fields.push_back(trim(field));
    return fields;
}

class CsvReader {
public:
    CsvReader(std::istream& in, const std::string& name) : in_(in), name_(name) {
        std::string header;
        if (!std::getline(in_, header)) {
            throw std::runtime_error(name_ + ": missing header row");
        }
        if (header.rfind("\xEF\xBB\xBF", 0) == 0) header.erase(0, 3);  // UTF-8 BOM
        header_ = split_csv(header);
    }

    size_t column(const std::string& col) const {
        const auto it = std::find(header_.begin(), header_.end(), col);
        if (it == header_.end()) {
            throw std::runtime_error(name_ + ": missing column '" + col + "'");
        }
        return static_cast<size_t>(it - header_.begin());
    }

    // Reads the next non-blank record; returns false at end of input.
    bool next(std::vector<std::string>& row) {
        std::string line;
        while (std::getline(in_, line)) {
            ++line_no_;
            if (trim(line).empty()) continue;
            row = split_csv(line);
            if (row.size() < header_.size()) row.resize(header_.size());
            return true;
        }
        return false;
    }

    std::string where() const { return name_ + " line " + std::to_string(line_no_ + 1); }

private:
    std::istream& in_;
    std::string name_;
    std::vector<std::string> header_;
    size_t line_no_ = 0;
};

struct StopTime {
    int32_t trip;
    int32_t sequence;
    int32_t stop;
    int32_t arrival;
    int32_t departure;
};

}  // namespace

int32_t parse_gtfs_time(const std::string& text) {
    const std::string s = trim(text);
    int h = 0, m = 0, sec = 0;
    char c1 = 0, c2 = 0;
    size_t used = 0;
    if (std::sscanf(s.c_str(), "%d%c%2d%c%2d%zn", &h, &c1, &m, &c2, &sec, &used) != 5 ||
        used != s.size() || c1 != ':' || c2 != ':' || h < 0 || m < 0 || m > 59 ||
        sec < 0 || sec > 59) {
        throw std::invalid_argument("invalid GTFS time: '" + text + "'");
    }
    return h * 3600 + m * 60 + sec;
}

int32_t Timetable::stop_index(const std::string& stop_id) const {
    const auto it = stop_index_.find(stop_id);
    if (it == stop_index_.end()) throw std::out_of_range("unknown stop_id: " + stop_id);
    return it->second;
}

Timetable Timetable::from_gtfs_dir(const std::string& dir) {
    const std::string stops_path = dir + "/stops.txt";
    const std::string stop_times_path = dir + "/stop_times.txt";
    std::ifstream stops(stops_path);
    if (!stops) throw std::runtime_error("cannot open " + stops_path);
    std::ifstream stop_times(stop_times_path);
    if (!stop_times) throw std::runtime_error("cannot open " + stop_times_path);
    return from_gtfs(stops, stop_times);
}

Timetable Timetable::from_gtfs(std::istream& stops, std::istream& stop_times) {
    Timetable tt;
    std::vector<std::string> row;

    CsvReader stops_csv(stops, "stops.txt");
    const size_t stop_id_col = stops_csv.column("stop_id");
    while (stops_csv.next(row)) {
        const std::string& id = row[stop_id_col];
        if (id.empty()) throw std::runtime_error(stops_csv.where() + ": empty stop_id");
        if (!tt.stop_index_.emplace(id, tt.num_stops()).second) {
            throw std::runtime_error(stops_csv.where() + ": duplicate stop_id " + id);
        }
        tt.stop_ids_.push_back(id);
    }

    CsvReader times_csv(stop_times, "stop_times.txt");
    const size_t trip_col = times_csv.column("trip_id");
    const size_t arr_col = times_csv.column("arrival_time");
    const size_t dep_col = times_csv.column("departure_time");
    const size_t stop_col = times_csv.column("stop_id");
    const size_t seq_col = times_csv.column("stop_sequence");

    std::unordered_map<std::string, int32_t> trip_index;
    std::vector<StopTime> rows;
    while (times_csv.next(row)) {
        const auto stop = tt.stop_index_.find(row[stop_col]);
        if (stop == tt.stop_index_.end()) {
            throw std::runtime_error(times_csv.where() + ": unknown stop_id " + row[stop_col]);
        }
        // Times are optional in GTFS for non-timepoint stops; interpolating
        // them is not supported yet.
        if (row[arr_col].empty() || row[dep_col].empty()) {
            throw std::runtime_error(times_csv.where() +
                                     ": missing arrival/departure time (interpolation not supported)");
        }
        const auto trip = trip_index.emplace(row[trip_col], static_cast<int32_t>(trip_index.size()));
        try {
            rows.push_back({trip.first->second, std::stoi(row[seq_col]), stop->second,
                            parse_gtfs_time(row[arr_col]), parse_gtfs_time(row[dep_col])});
        } catch (const std::logic_error& e) {
            throw std::runtime_error(times_csv.where() + ": " + e.what());
        }
    }

    std::sort(rows.begin(), rows.end(), [](const StopTime& a, const StopTime& b) {
        return std::tie(a.trip, a.sequence) < std::tie(b.trip, b.sequence);
    });
    for (size_t i = 1; i < rows.size(); ++i) {
        const StopTime& from = rows[i - 1];
        const StopTime& to = rows[i];
        if (from.trip != to.trip) continue;
        tt.connections_.push_back({from.stop, to.stop, from.departure, to.arrival, from.trip});
    }
    // Stable so that zero-duration hops of one trip keep their stop_sequence
    // order, which the single-pass scan relies on.
    std::stable_sort(tt.connections_.begin(), tt.connections_.end(),
                     [](const Connection& a, const Connection& b) {
                         return std::tie(a.dep_time, a.arr_time) < std::tie(b.dep_time, b.arr_time);
                     });
    return tt;
}

std::vector<int32_t> earliest_arrival(const Timetable& tt, int32_t source,
                                      int32_t departure_time) {
    if (source < 0 || source >= tt.num_stops()) {
        throw std::out_of_range("source stop index out of range");
    }
    std::vector<int32_t> arrival(tt.num_stops(), kUnreachable);
    arrival[source] = departure_time;

    const auto& conns = tt.connections();
    auto it = std::lower_bound(conns.begin(), conns.end(), departure_time,
                               [](const Connection& c, int32_t t) { return c.dep_time < t; });
    for (; it != conns.end(); ++it) {
        if (arrival[it->dep_stop] <= it->dep_time && it->arr_time < arrival[it->arr_stop]) {
            arrival[it->arr_stop] = it->arr_time;
        }
    }
    return arrival;
}

}  // namespace transit
