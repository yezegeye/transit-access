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
}
