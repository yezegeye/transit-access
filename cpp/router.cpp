#include "router.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <tuple>

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


bool is_leap_year(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int days_in_month(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap_year(year) ? 29 : kDays[month - 1];
}

// Day of the week of a valid YYYYMMDD date, 0 = Monday ... 6 = Sunday
// (Sakamoto's method).
int weekday(int32_t date) {
    static const int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int year = date / 10000;
    const int month = date / 100 % 100;
    const int day = date % 100;
    if (month < 3) --year;
    const int from_sunday =
        (year + year / 4 - year / 100 + year / 400 + kOffsets[month - 1] + day) % 7;
    return (from_sunday + 6) % 7;
}

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

int32_t parse_gtfs_date(const std::string& text) {
    const std::string s = trim(text);
    const bool digits = s.size() == 8 && std::all_of(s.begin(), s.end(), [](char ch) {
                            return ch >= '0' && ch <= '9';
                        });
    if (digits) {
        const int32_t date = std::stoi(s);
        const int year = date / 10000;
        const int month = date / 100 % 100;
        const int day = date % 100;
        if (year >= 1 && month >= 1 && month <= 12 && day >= 1 &&
            day <= days_in_month(year, month)) {
            return date;
        }
    }
    throw std::invalid_argument("invalid GTFS date: '" + text + "'");
}

std::unordered_set<std::string> active_services(std::istream* calendar,
                                                std::istream* calendar_dates, int32_t date) {
    static const char* const kDayColumns[] = {"monday", "tuesday",  "wednesday", "thursday",
                                              "friday", "saturday", "sunday"};
    std::unordered_set<std::string> services;
    std::vector<std::string> row;

    if (calendar) {
        CsvReader csv(*calendar, "calendar.txt");
        const size_t id_col = csv.column("service_id");
        const size_t day_col = csv.column(kDayColumns[weekday(date)]);
        const size_t start_col = csv.column("start_date");
        const size_t end_col = csv.column("end_date");
        while (csv.next(row)) {
            try {
                if (row[day_col] == "1" && parse_gtfs_date(row[start_col]) <= date &&
                    date <= parse_gtfs_date(row[end_col])) {
                    services.insert(row[id_col]);
                }
            } catch (const std::invalid_argument& e) {
                throw std::runtime_error(csv.where() + ": " + e.what());
            }
        }
    }

    if (calendar_dates) {
        CsvReader csv(*calendar_dates, "calendar_dates.txt");
        const size_t id_col = csv.column("service_id");
        const size_t date_col = csv.column("date");
        const size_t type_col = csv.column("exception_type");
        while (csv.next(row)) {
            int32_t exception_date = 0;
            try {
                exception_date = parse_gtfs_date(row[date_col]);
            } catch (const std::invalid_argument& e) {
                throw std::runtime_error(csv.where() + ": " + e.what());
            }
            if (exception_date != date) continue;
            if (row[type_col] == "1") {
                services.insert(row[id_col]);
            } else if (row[type_col] == "2") {
                services.erase(row[id_col]);
            } else {
                throw std::runtime_error(csv.where() + ": invalid exception_type '" +
                                         row[type_col] + "'");
            }
        }
    }
    return services;
}

std::unordered_set<std::string> active_trips(std::istream& trips,
                                             const std::unordered_set<std::string>& services) {
    CsvReader csv(trips, "trips.txt");
    const size_t trip_col = csv.column("trip_id");
    const size_t service_col = csv.column("service_id");
    std::unordered_set<std::string> active;
    std::vector<std::string> row;
    while (csv.next(row)) {
        if (services.count(row[service_col])) active.insert(row[trip_col]);
    }
    return active;
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
