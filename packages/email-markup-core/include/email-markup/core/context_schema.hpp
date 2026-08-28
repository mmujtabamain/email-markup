#pragma once

#include <string>
#include <vector>

#include "email-markup/core/expr.hpp"

namespace email_markup
{
    struct ContextSchema
    {
        std::string name;
        Json fields{Json::object()};
    };

    [[nodiscard]] ContextSchema parse_context_schema(const Json &document);
    [[nodiscard]] Json context_schema_example(const ContextSchema &schema);
    [[nodiscard]] Json context_schema_json_schema(const ContextSchema &schema);
    [[nodiscard]] std::vector<std::string> context_schema_paths(const ContextSchema &schema);
    [[nodiscard]] std::vector<std::string> validate_context_data(
        const ContextSchema &schema, const Json &data);
}
