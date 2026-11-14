#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include "router.hpp"
namespace py = pybind11;
using transit::Timetable;

namespace {

int32_t stop_index(const Timetable& tt, const std::string& stop_id) {
    try {
        return tt.stop_index(stop_id);
    } catch (const std::out_of_range&) {
        throw py::key_error(stop_id);
    }
}

// An (n, 2) array-like of (lat, lon) rows.
std::vector<transit::LatLon> to_points(const py::array_t<double, py::array::c_style | py::array::forcecast>& a,
                                       const char* name) {
    if (a.ndim() != 2 || a.shape(1) != 2) {
        if (!(a.ndim() == 1 && a.shape(0) == 0)) {
            throw py::value_error(std::string(name) + " must have shape (n, 2): rows of (lat, lon)");
        }
    }
    std::vector<transit::LatLon> points;
    const auto n = a.ndim() == 2 ? a.shape(0) : 0;
    const double* p = a.data();
    for (py::ssize_t i = 0; i < n; ++i) points.push_back({p[2 * i], p[2 * i + 1]});
    return points;
}

}  // namespace

PYBIND11_MODULE(transit_router, m) {
    m.doc() = "Earliest-arrival transit routing over GTFS timetables";

    m.def("parse_gtfs_time", &transit::parse_gtfs_time, py::arg("text"),
          "Parse a GTFS 'HH:MM:SS' time into seconds after midnight.");

    py::class_<Timetable>(m, "Timetable")
        .def_static(
            "from_gtfs_dir",
            [](const py::object& path, const py::object& date) {
                const auto dir = py::module_::import("os").attr("fspath")(path).cast<std::string>();
                if (date.is_none()) return Timetable::from_gtfs_dir(dir);
                // Accept a datetime.date (or datetime) as well as a "YYYYMMDD" string.
                const std::string text = py::hasattr(date, "strftime")
                                             ? date.attr("strftime")("%Y%m%d").cast<std::string>()
                                             : py::str(date).cast<std::string>();
                return Timetable::from_gtfs_dir(dir, transit::parse_gtfs_date(text));
            },
            py::arg("path"), py::arg("date") = py::none(),
            "Load a GTFS directory. With `date` (a datetime.date or 'YYYYMMDD'),\n"
            "only trips running on that service day are loaded, using trips.txt,\n"
            "calendar.txt and calendar_dates.txt; without it every trip is loaded.")
        .def_property_readonly("num_stops", &Timetable::num_stops)
        .def_property_readonly("num_connections",
                               [](const Timetable& tt) { return tt.connections().size(); })
        .def_property_readonly("stop_ids", &Timetable::stop_ids)
        .def_property_readonly("stop_lats", &Timetable::stop_lats,
                               "stop_lat per stop, parallel to stop_ids (NaN if missing).")
        .def_property_readonly("stop_lons", &Timetable::stop_lons,
                               "stop_lon per stop, parallel to stop_ids (NaN if missing).")
        .def("stop_index", &stop_index, py::arg("stop_id"))
        .def("generate_footpaths", &Timetable::generate_footpaths,
             py::arg("max_distance_m") = 400.0, py::arg("walk_speed_mps") = 1.3,
             "Replace the footpaths with a walk between every pair of stops at most\n"
             "`max_distance_m` apart (straight line), taking distance / walk_speed_mps\n"
             "seconds. Returns the number of footpaths (each direction counts once).")
        .def_property_readonly("num_footpaths", &Timetable::num_footpaths)
        .def(
            "footpaths",
            [](const Timetable& tt, const std::string& stop_id) {
                py::dict walks;
                for (const auto& f : tt.footpaths(stop_index(tt, stop_id))) {
                    walks[py::str(tt.stop_ids()[f.to])] = f.duration;
                }
                return walks;
            },
            py::arg("stop_id"), "Walking time in seconds from `stop_id` to each neighbouring stop.")
        .def("__repr__", [](const Timetable& tt) {
            return "<Timetable stops=" + std::to_string(tt.num_stops()) +
                   " connections=" + std::to_string(tt.connections().size()) + ">";
        });

    m.def(
        "earliest_arrival",
        [](const Timetable& tt, const std::string& source, int32_t departure_time) {
            const auto arrival =
                transit::earliest_arrival(tt, stop_index(tt, source), departure_time);
            py::dict reached;
            for (int32_t i = 0; i < tt.num_stops(); ++i) {
                if (arrival[i] != transit::kUnreachable) reached[py::str(tt.stop_ids()[i])] = arrival[i];
            }
            return reached;
        },
        py::arg("timetable"), py::arg("source"), py::arg("departure_time"),
        "Earliest arrival (seconds after midnight) at every stop reachable from\n"
        "`source` when leaving at `departure_time`. Unreachable stops are omitted.");

    m.def(
        "travel_time_matrix",
        [](const Timetable& tt, const py::array_t<double, py::array::c_style | py::array::forcecast>& origins,
           const py::array_t<double, py::array::c_style | py::array::forcecast>& destinations,
           const std::vector<int32_t>& departure_times, double max_access_m, double walk_speed_mps,
           const py::object& max_travel_time, int threads) {
            transit::MatrixOptions opt;
            opt.max_access_m = max_access_m;
            opt.walk_speed_mps = walk_speed_mps;
            if (!max_travel_time.is_none()) opt.max_travel_time = max_travel_time.cast<int32_t>();
            opt.threads = threads;
            const auto orig = to_points(origins, "origins");
            const auto dest = to_points(destinations, "destinations");
            std::vector<int32_t> seconds;
            {
                py::gil_scoped_release release;
                seconds = transit::travel_time_matrix(tt, orig, dest, departure_times, opt);
            }
            py::array_t<float> out({orig.size(), departure_times.size(), dest.size()});
            float* o = out.mutable_data();
            for (size_t i = 0; i < seconds.size(); ++i) {
                o[i] = seconds[i] == transit::kUnreachable ? std::numeric_limits<float>::infinity()
                                                           : static_cast<float>(seconds[i]);
            }
            return out;
        },
        py::arg("timetable"), py::arg("origins"), py::arg("destinations"), py::arg("departure_times"),
        py::arg("max_access_m") = 800.0, py::arg("walk_speed_mps") = 1.3,
        py::arg("max_travel_time") = py::none(), py::arg("threads") = 0,
        "Door-to-door travel time in seconds from each origin to each destination\n"
        "for each departure time (seconds after midnight).\n\n"
        "origins, destinations: (n, 2) arrays of (lat, lon). Returns a float32 array\n"
        "of shape (n_origins, n_departures, n_destinations); np.inf where there is no\n"
        "journey (or it takes longer than max_travel_time seconds). A journey walks up\n"
        "to max_access_m to a stop, rides using the timetable's footpaths for\n"
        "transfers (see Timetable.generate_footpaths), and walks up to max_access_m\n"
        "to the destination; nearby points may also be walked between directly.\n"
        "Waiting for the first vehicle counts. Runs on `threads` threads (0 = all\n"
        "cores) without holding the GIL.");
}
