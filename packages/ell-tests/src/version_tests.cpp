#include <catch2/catch_test_macros.hpp>

#include "ell/core/version.hpp"

TEST_CASE("the release version is available") {
    CHECK(ell::version() == "1.0.0");
}
