#pragma once

#include <string_view>
#include <vector>

#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/provenance.hpp"

namespace email_markup {

enum class LintRole { content, shell };

[[nodiscard]] std::vector<Diagnostic> lint_html(
    std::string_view html, LintRole role, SourceRange fallback,
    std::size_t gmail_limit = 102400);
[[nodiscard]] std::vector<Diagnostic> lint_html(
    const GeneratedHtml& generated, LintRole role, SourceRange fallback,
    std::size_t gmail_limit = 102400);

}  // namespace email_markup
