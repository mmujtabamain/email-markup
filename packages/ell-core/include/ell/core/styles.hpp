#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ell/core/ast.hpp"
#include "ell/core/css.hpp"
#include "ell/core/diagnostic.hpp"
#include "ell/core/expr.hpp"

namespace ell {

[[nodiscard]] CssDeclarations resolve_style_layers(
    std::string_view bundle_names, std::string_view call_css,
    const std::unordered_map<std::string, StyleDefinition>& styles,
    const EvaluationContext& context, SourceRange range,
    std::vector<Diagnostic>& diagnostics);
[[nodiscard]] std::vector<Diagnostic> validate_media(
    const MediaDefinition& media);

}  // namespace ell
