#pragma once

#include <string_view>

#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/expr.hpp"

namespace email_markup
{

    struct DataResult
    {
        Json data;
        std::vector<Diagnostic> diagnostics;
        bool ok{};
    };

    [[nodiscard]] DataResult parse_data(std::string_view text,
                                        std::size_t maximum_bytes);

} // namespace email_markup
