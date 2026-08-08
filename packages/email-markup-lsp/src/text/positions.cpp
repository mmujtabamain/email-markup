#include "positions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace email_markup::lsp::text
{
    namespace
    {
        std::size_t utf8_width(const unsigned char byte)
        {
            if ((byte & 0x80U) == 0)
                return 1;
            if ((byte & 0xe0U) == 0xc0U)
                return 2;
            if ((byte & 0xf0U) == 0xe0U)
                return 3;
            if ((byte & 0xf8U) == 0xf0U)
                return 4;
            return 1;
        }

        std::uint32_t codepoint_at(const std::string_view text, const std::size_t offset,
                                   const std::size_t width)
        {
            const auto first = static_cast<unsigned char>(text[offset]);
            if (width == 1 || offset + width > text.size())
                return first;
            std::uint32_t value = first & (0x7fU >> width);
            for (std::size_t index = 1; index < width; ++index)
                value = (value << 6U) |
                        (static_cast<unsigned char>(text[offset + index]) & 0x3fU);
            return value;
        }
    } // namespace

    std::size_t offset_at(const std::string_view text, const std::size_t target_line,
                          const std::size_t target_character)
    {
        std::size_t offset = 0;
        std::size_t line = 0;
        while (offset < text.size() && line < target_line)
        {
            if (text[offset++] == '\n')
                ++line;
        }

        std::size_t character = 0;
        while (offset < text.size() && text[offset] != '\n' && character < target_character)
        {
            const auto width = utf8_width(static_cast<unsigned char>(text[offset]));
            const auto point = codepoint_at(text, offset, width);
            const auto units = point > 0xffffU ? 2U : 1U;
            if (character + units > target_character)
                break;
            character += units;
            offset += width;
        }
        return offset;
    }

    Json position_at(const std::string_view text, const std::size_t target_offset)
    {
        std::size_t offset = 0;
        std::size_t line = 0;
        std::size_t character = 0;
        while (offset < text.size() && offset < target_offset)
        {
            if (text[offset] == '\n')
            {
                ++line;
                character = 0;
                ++offset;
                continue;
            }
            const auto width = utf8_width(static_cast<unsigned char>(text[offset]));
            const auto point = codepoint_at(text, offset, width);
            character += point > 0xffffU ? 2U : 1U;
            offset += width;
        }
        return {{"line", line}, {"character", character}};
    }

    std::string word_at(const std::string_view text, const std::size_t offset)
    {
        std::size_t start = std::min(offset, text.size());
        while (start > 0 &&
               (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
                text[start - 1] == '_'))
            --start;
        std::size_t end = std::min(offset, text.size());
        while (end < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
            ++end;
        return std::string{text.substr(start, end - start)};
    }
} // namespace email_markup::lsp::text
