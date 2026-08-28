#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/context_schema.hpp"

TEST_CASE("context schemas validate examples and project deterministic JSON Schema")
{
    const email_markup::Json document{
        {"format", "email-markup-context"}, {"version", 1}, {"name", "email-context"},
        {"fields", {{"business", {{"type", "object"}, {"required", true},
            {"fields", {{"name", {{"type", "string"}, {"required", true},
                                      {"example", "Acme"}}},
                        {"rating", {{"type", "number"}, {"nullable", true},
                                        {"example", 4.8}}}}}}}}}};
    const auto schema = email_markup::parse_context_schema(document);
    const auto example = email_markup::context_schema_example(schema);
    CHECK(example["business"]["name"] == "Acme");
    CHECK(email_markup::validate_context_data(schema, example).empty());
    const auto projected = email_markup::context_schema_json_schema(schema);
    CHECK(projected["required"][0] == "business");
    CHECK(projected["properties"]["business"]["properties"]["rating"]["type"].is_array());
}

TEST_CASE("context schemas reject recipient values with the wrong type")
{
    const email_markup::Json document{
        {"format", "email-markup-context"}, {"version", 1}, {"name", "email-context"},
        {"fields", {{"count", {{"type", "int"}, {"required", true}, {"minimum", 1}}}}}};
    const auto schema = email_markup::parse_context_schema(document);
    CHECK_FALSE(email_markup::validate_context_data(schema, {{"count", "many"}}).empty());
    CHECK_FALSE(email_markup::validate_context_data(schema, {{"count", 0}}).empty());
}
