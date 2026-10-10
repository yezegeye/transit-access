#include <catch2/catch_test_macros.hpp>
#include "router.hpp"

TEST_CASE("add_seconds works") {
    REQUIRE(add_seconds(30, 12) == 42);
}
