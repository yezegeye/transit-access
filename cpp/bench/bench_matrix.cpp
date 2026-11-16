// Times travel_time_matrix on the analysis workload: Chicago tract centroids
// to all Cook County tract centroids, every minute 7-9 AM, 2-minute
// transfers, 60-minute cap. Used for profiling; not part of the test suite.
//
//   build/bench_matrix data/raw/cta/gtfs 20261118 data/processed/cook_tracts.csv [origins] [threads]
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "router.hpp"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s GTFS_DIR YYYYMMDD TRACTS_CSV [ORIGINS] [THREADS]\n", argv[0]);
        return 2;
    }
    const size_t max_origins = argc > 4 ? std::stoul(argv[4]) : 100;
    int threads = argc > 5 ? std::stoi(argv[5]) : 1;
    if (threads <= 0) threads = static_cast<int>(std::thread::hardware_concurrency());

    using clock = std::chrono::steady_clock;
    auto t = clock::now();
    transit::Timetable tt = transit::Timetable::from_gtfs_dir(argv[1], transit::parse_gtfs_date(argv[2]));
    tt.generate_footpaths(400, 5.0 / 3.6 / 1.2);
    tt.set_min_transfer_time(120);
    const double load_s = std::chrono::duration<double>(clock::now() - t).count();

    // cook_tracts.csv: geoid,in_chicago,lat,lon,...
    std::ifstream csv(argv[3]);
    std::string line;
    std::getline(csv, line);
    std::vector<transit::LatLon> origins, destinations;
    while (std::getline(csv, line)) {
        std::stringstream ss(line);
        std::string geoid, in_chicago, lat, lon;
        std::getline(ss, geoid, ',');
        std::getline(ss, in_chicago, ',');
        std::getline(ss, lat, ',');
        std::getline(ss, lon, ',');
        if (lat.empty()) continue;
        const transit::LatLon p{std::stod(lat), std::stod(lon)};
        destinations.push_back(p);
        if (in_chicago == "True" && origins.size() < max_origins) origins.push_back(p);
    }
    std::vector<int32_t> deps;
    for (int32_t d = 7 * 3600; d <= 9 * 3600; d += 60) deps.push_back(d);

    transit::MatrixOptions opt;
    opt.max_access_m = 800;
    opt.walk_speed_mps = 5.0 / 3.6 / 1.2;
    opt.max_travel_time = 60 * 60;
    opt.threads = threads;
    t = clock::now();
    const auto m = transit::travel_time_matrix(tt, origins, destinations, deps, opt);
    const double s = std::chrono::duration<double>(clock::now() - t).count();
    long long checksum = 0;
    for (int32_t v : m) checksum += v == transit::kUnreachable ? 1 : v;
    const double queries = static_cast<double>(origins.size()) * deps.size();
    std::printf("load %.2f s | %zu origins x %zu departures x %zu destinations on %d thread(s): "
                "%.2f s, %.3f ms per origin-departure | checksum %lld\n",
                load_s, origins.size(), deps.size(), destinations.size(), threads, s,
                1000 * s / queries, checksum);
    return 0;
}
