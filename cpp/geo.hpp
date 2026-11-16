#pragma once
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace transit {

// Mean Earth radius (IUGG), metres.
constexpr double kEarthRadiusM = 6371008.8;

// Great-circle distance in metres between two WGS84 points given in degrees.
double haversine_m(double lat1, double lon1, double lat2, double lon2);

// A fixed grid over latitude/longitude for "which points are within R metres
// of here?" queries. Each cell is about `cell_m` metres tall; queries scan
// only the cells that can hold a match and then check the exact haversine
// distance, so results are exact. Points with a NaN coordinate are never
// returned. The grid keeps its own copy of the coordinates. Not meant for
// points near the poles or across the antimeridian.
class PointGrid {
public:
    PointGrid(const std::vector<double>& lats, const std::vector<double>& lons, double cell_m);

    // (point index, distance in metres) for every point within `radius_m` of
    // (lat, lon), in increasing index order.
    std::vector<std::pair<int32_t, double>> within(double lat, double lon,
                                                   double radius_m) const;

private:
    int64_t key(int64_t row, int64_t col) const { return row * 4000003 + col; }

    std::vector<double> lats_;
    std::vector<double> lons_;
    double cell_deg_;
    std::unordered_map<int64_t, std::vector<int32_t>> cells_;
};

}  // namespace transit
