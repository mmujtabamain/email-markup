#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "email-markup/core/source.hpp"

namespace email_markup
{

    struct ExpansionFrame
    {
        std::string component;
        SourceRange call_site;
        SourceRange definition;
        bool operator==(const ExpansionFrame &) const = default;
    };

    struct OutputSegment
    {
        std::size_t output_start{};
        std::size_t output_end{};
        SourceRange origin;
        std::vector<ExpansionFrame> expansion_stack;
    };

    struct GeneratedHtml
    {
        std::string html;
        std::vector<OutputSegment> segments;

        void append(std::string_view text, SourceRange origin,
                    const std::vector<ExpansionFrame> &stack = {});
        void insert(std::size_t offset, std::string_view text, SourceRange origin,
                    const std::vector<ExpansionFrame> &stack = {});
        void replace(std::size_t offset, std::size_t count, std::string_view text,
                     SourceRange origin,
                     const std::vector<ExpansionFrame> &stack = {});
    };

} // namespace email_markup
