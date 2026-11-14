#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include "geo.hpp"

using Catch::Approx;
using transit::haversine_m;
using transit::PointGrid;

TEST_CASE("haversine_m") {
    CHECK(haversine_m(41.88, -87.63, 41.88, -87.63) == 0.0);
    // One degree of latitude on the mean-radius sphere.
    CHECK(haversine_m(0, 0, 1, 0) == Approx(111195.08).epsilon(1e-6));
    // A degree of longitude shrinks with cos(latitude).
    CHECK(haversine_m(60, 0, 60, 1) == Approx(111195.08 * 0.5).epsilon(1e-3));
    // Chicago Union Station to the Thompson Center: about 0.9 km.
    CHECK(haversine_m(41.8786, -87.6403, 41.8846, -87.6321) == Approx(940).margin(30));
    CHECK(haversine_m(41.0, -87.0, 41.5, -87.5) == haversine_m(41.5, -87.5, 41.0, -87.0));
}

TEST_CASE("PointGrid matches a brute-force search") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> lat(41.6, 42.1), lon(-87.95, -87.5);
    std::vector<double> lats, lons;
    for (int i = 0; i < 2000; ++i) {
        lats.push_back(lat(rng));
        lons.push_back(lon(rng));
    }
    lats.push_back(NAN);  // never returned
    lons.push_back(-87.7);

    for (const double cell : {100.0, 400.0, 2000.0}) {
        const PointGrid grid(lats, lons, cell);
        for (const double radius : {0.0, 150.0, 400.0, 1200.0}) {
            for (int q = 0; q < 50; ++q) {
                const double qlat = lat(rng), qlon = lon(rng);
                std::vector<int32_t> expected;
                for (size_t i = 0; i < lats.size(); ++i) {
                    if (!std::isnan(lats[i]) && haversine_m(qlat, qlon, lats[i], lons[i]) <= radius) {
                        expected.push_back(static_cast<int32_t>(i));
                    }
                }
                std::vector<int32_t> got;
                for (const auto& [i, d] : grid.within(qlat, qlon, radius)) {
                    got.push_back(i);
                    CHECK(d == Approx(haversine_m(qlat, qlon, lats[i], lons[i])));
                }
                CHECK(got == expected);
            }
        }
    }
}

TEST_CASE("PointGrid edge cases") {
    const std::vector<double> lats{41.9, 41.9}, lons{-87.6, -87.6};
    const PointGrid grid(lats, lons, 400);
    CHECK(grid.within(41.9, -87.6, 0).size() == 2);  // identical points, radius 0
    CHECK(grid.within(NAN, -87.6, 400).empty());
    CHECK(grid.within(41.9, -87.6, -1).empty());
    CHECK_THROWS_AS(PointGrid(lats, {1.0}, 400), std::invalid_argument);
    CHECK_THROWS_AS(PointGrid(lats, lons, 0), std::invalid_argument);
}
