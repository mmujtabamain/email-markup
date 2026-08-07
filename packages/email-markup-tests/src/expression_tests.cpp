#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/data.hpp"
#include "email-markup/core/expr.hpp"

TEST_CASE("JSON input must be an object") {
    CHECK(email_markup::parse_data(R"({"name":"Northstar"})", 1024).ok);
    CHECK_FALSE(email_markup::parse_data("[]", 1024).ok);
    CHECK_FALSE(email_markup::parse_data("{", 1024).ok);
}

TEST_CASE("expressions resolve scope and arithmetic") {
    const email_markup::Json data = {{"business", {{"rating", 4}, {"name", "Northstar"}}}};
    email_markup::EvaluationContext context;
    context.data = &data;
    context.locals["bonus"] = 1;

    auto result = email_markup::evaluate_expression(
        "business.rating + bonus * 2 == 6 and business.name != \"\"", context, {});

    REQUIRE(result.ok);
    CHECK(result.value == true);
}

TEST_CASE("and and or short circuit missing paths") {
    const email_markup::Json data = email_markup::Json::object();
    email_markup::EvaluationContext context;
    context.data = &data;

    CHECK(email_markup::evaluate_expression("false and missing.path", context, {}).ok);
    CHECK(email_markup::evaluate_expression("true or missing.path", context, {}).ok);
}

TEST_CASE("expression failures are deterministic") {
    const email_markup::Json data = email_markup::Json::object();
    email_markup::EvaluationContext context;
    context.data = &data;

    CHECK_FALSE(email_markup::evaluate_expression("1 / 0", context, {}).ok);
    CHECK_FALSE(email_markup::evaluate_expression("1 + \"2\"", context, {}).ok);
    CHECK_FALSE(email_markup::evaluate_expression("missing.path", context, {}).ok);
    CHECK_FALSE(email_markup::evaluate_expression("9223372036854775807 + 1", context, {}).ok);
}
