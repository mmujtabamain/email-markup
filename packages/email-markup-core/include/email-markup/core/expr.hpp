#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "email-markup/core/diagnostic.hpp"

namespace email_markup {

using Json = nlohmann::json;

struct EvaluationContext {
    const Json* data{};
    std::unordered_map<std::string, Json> locals;
    std::unordered_map<std::string, Json> props;
    std::unordered_map<std::string, Json> tokens;
};

struct EvaluationResult {
    Json value;
    std::vector<Diagnostic> diagnostics;
    bool ok{};
};

[[nodiscard]] EvaluationResult evaluate_expression(
    std::string_view expression, const EvaluationContext& context,
    SourceRange range);
[[nodiscard]] std::string emit_scalar(const Json& value);
[[nodiscard]] bool truthy(const Json& value) noexcept;

}  // namespace email_markup
