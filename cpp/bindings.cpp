#include <pybind11/pybind11.h>
#include "router.hpp"
namespace py = pybind11;

PYBIND11_MODULE(transit_router, m) {
    m.def("add_seconds", &add_seconds);
}
