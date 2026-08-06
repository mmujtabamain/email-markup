#pragma once

#include <string_view>
#include <vector>

#include "ell/core/diagnostic.hpp"

namespace ell {

enum class LintRole { content, shell };

[[nodiscard]] std::vector<Diagnostic> lint_html(
    std::string_view html, LintRole role, SourceRange fallback,
    std::size_t gmail_limit = 102400);

}  // namespace ell
