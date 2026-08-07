#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/css.hpp"
#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/expr.hpp"

namespace email_markup {

[[nodiscard]] CssDeclarations resolve_style_layers(
    std::string_view bundle_names, std::string_view call_css,
    const std::unordered_map<std::string, StyleDefinition>& styles,
    const EvaluationContext& context, SourceRange range,
    std::vector<Diagnostic>& diagnostics);
[[nodiscard]] std::vector<Diagnostic> validate_media(
    const MediaDefinition& media);

}  // namespace email_markup
