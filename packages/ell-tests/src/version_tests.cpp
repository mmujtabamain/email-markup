#include <catch2/catch_test_macros.hpp>

#include "ell/core/version.hpp"

TEST_CASE("the development version is available") {
    CHECK(ell::version() == "0.1.0-dev");
}
