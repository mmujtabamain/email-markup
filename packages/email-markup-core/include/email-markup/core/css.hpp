#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/provenance.hpp"

namespace email_markup
{

    using CssDeclarations = std::map<std::string, std::string>;

    [[nodiscard]] CssDeclarations parse_declarations(std::string_view css);
    [[nodiscard]] std::string serialize_declarations(const CssDeclarations &declarations);
    [[nodiscard]] std::string apply_root_styles(std::string html,
                                                const CssDeclarations &additions);
    [[nodiscard]] std::string inline_css(std::string html);
    [[nodiscard]] GeneratedHtml inline_css(GeneratedHtml generated);
    [[nodiscard]] GeneratedHtml inline_css(GeneratedHtml generated,
                                           std::vector<Diagnostic> &diagnostics);

} // namespace email_markup
