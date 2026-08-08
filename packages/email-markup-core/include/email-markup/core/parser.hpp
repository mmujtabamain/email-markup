#pragma once

#include <string_view>
#include <vector>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/diagnostic.hpp"

namespace email_markup
{

    struct ParseResult
    {
        Document document;
        std::vector<Diagnostic> diagnostics;
    };

    [[nodiscard]] ParseResult parse(SourceId source, std::string_view text,
                                    std::size_t diagnostic_limit = 100);

} // namespace email_markup
