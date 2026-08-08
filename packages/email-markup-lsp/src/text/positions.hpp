#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "protocol/json.hpp"

namespace email_markup::lsp::text
{
    [[nodiscard]] std::size_t offset_at(std::string_view text, std::size_t line,
                                        std::size_t character);
    [[nodiscard]] Json position_at(std::string_view text, std::size_t offset);
    [[nodiscard]] std::string word_at(std::string_view text, std::size_t offset);
} // namespace email_markup::lsp::text
