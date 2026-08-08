#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/version.hpp"

TEST_CASE("the release version is available")
{
    CHECK(email_markup::version() == EMAIL_MARKUP_VERSION);
}
