#include "geo.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace transit {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
// Metres per degree of latitude on the mean-radius sphere.
constexpr double kMetresPerDegree = kEarthRadiusM * kDegToRad;

}  // namespace

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    const double phi1 = lat1 * kDegToRad;
    const double phi2 = lat2 * kDegToRad;
    const double dphi = (lat2 - lat1) * kDegToRad;
    const double dlambda = (lon2 - lon1) * kDegToRad;
    const double a = std::sin(dphi / 2) * std::sin(dphi / 2) +
                     std::cos(phi1) * std::cos(phi2) * std::sin(dlambda / 2) * std::sin(dlambda / 2);
    return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

PointGrid::PointGrid(const std::vector<double>& lats, const std::vector<double>& lons,
                     double cell_m)
    : lats_(lats), lons_(lons), cell_deg_(cell_m / kMetresPerDegree) {
    if (lats.size() != lons.size()) throw std::invalid_argument("lats and lons differ in size");
    if (!(cell_m > 0)) throw std::invalid_argument("cell size must be positive");
    for (size_t i = 0; i < lats.size(); ++i) {
        if (std::isnan(lats[i]) || std::isnan(lons[i])) continue;
        const auto row = static_cast<int64_t>(std::floor(lats[i] / cell_deg_));
        const auto col = static_cast<int64_t>(std::floor(lons[i] / cell_deg_));
        cells_[key(row, col)].push_back(static_cast<int32_t>(i));
    }
}

std::vector<std::pair<int32_t, double>> PointGrid::within(double lat, double lon,
                                                          double radius_m) const {
    std::vector<std::pair<int32_t, double>> found;
    if (std::isnan(lat) || std::isnan(lon) || radius_m < 0) return found;

    // Degrees of latitude/longitude the radius can span. A degree of longitude
    // shrinks with cos(latitude), so use the latitude farthest from the
    // equator that the search box reaches.
    const double dlat = radius_m / kMetresPerDegree;
    const double far_lat = std::min(89.0, std::fabs(lat) + dlat);
    const double dlon = dlat / std::cos(far_lat * kDegToRad);

    const auto row_lo = static_cast<int64_t>(std::floor((lat - dlat) / cell_deg_));
    const auto row_hi = static_cast<int64_t>(std::floor((lat + dlat) / cell_deg_));
    const auto col_lo = static_cast<int64_t>(std::floor((lon - dlon) / cell_deg_));
    const auto col_hi = static_cast<int64_t>(std::floor((lon + dlon) / cell_deg_));
    for (int64_t row = row_lo; row <= row_hi; ++row) {
        for (int64_t col = col_lo; col <= col_hi; ++col) {
            const auto cell = cells_.find(key(row, col));
            if (cell == cells_.end()) continue;
            for (const int32_t i : cell->second) {
                const double d = haversine_m(lat, lon, lats_[i], lons_[i]);
                if (d <= radius_m) found.emplace_back(i, d);
            }
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

}  // namespace transit
