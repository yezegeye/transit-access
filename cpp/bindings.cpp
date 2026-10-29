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
            [](const py::object& path) {
                const auto dir = py::module_::import("os").attr("fspath")(path).cast<std::string>();
                return Timetable::from_gtfs_dir(dir);
            },
            py::arg("path"), "Load stops.txt and stop_times.txt from a GTFS directory.")
        .def_property_readonly("num_stops", &Timetable::num_stops)
        .def_property_readonly("num_connections",
                               [](const Timetable& tt) { return tt.connections().size(); })
        .def_property_readonly("stop_ids", &Timetable::stop_ids)
        .def("stop_index", &stop_index, py::arg("stop_id"))
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
