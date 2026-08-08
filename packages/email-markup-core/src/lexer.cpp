#include "email-markup/core/lexer.hpp"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace email_markup
{
    namespace
    {

        const std::unordered_set<std::string> raw_body_names{
            "Props", "Slots", "DefineStyle", "Media"};

        bool name_start(const char ch)
        {
            return ch >= 'A' && ch <= 'Z';
        }

        bool name_continue(const char ch)
        {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        }

        void add_error(LexResult &result, const SourceRange range, std::string code,
                       std::string message, const std::size_t limit)
        {
            if (result.diagnostics.size() < limit)
            {
                result.diagnostics.push_back(
                    {std::move(code), Severity::error, std::move(message), range});
            }
        }

        std::size_t delimited(std::string_view text, std::size_t start, const char opener,
                              const char closer, std::string &output)
        {
            std::size_t position = start + 1;
            int depth = 1;
            bool quoted = false;
            bool escaped = false;
            while (position < text.size())
            {
                const char ch = text[position++];
                if (quoted)
                {
                    if (ch == '"' && !escaped)
                        quoted = false;
                    escaped = ch == '\\' && !escaped;
                    if (ch != '\\')
                        escaped = false;
                    if (depth > 0)
                        output.push_back(ch);
                    continue;
                }
                if (ch == '"')
                    quoted = true;
                if (!quoted && ch == opener)
                    ++depth;
                if (!quoted && ch == closer)
                {
                    --depth;
                    if (depth == 0)
                    {
                        return position;
                    }
                }
                output.push_back(ch);
            }
            return std::string_view::npos;
        }

    } // namespace

    LexResult lex(const SourceId source, const std::string_view text,
                  const std::size_t diagnostic_limit)
    {
        LexResult result;
        std::string buffer;
        std::size_t buffer_start = 0;
        const auto flush = [&](const std::size_t end)
        {
            if (!buffer.empty())
            {
                result.tokens.push_back(
                    {TokenKind::text, {}, {}, std::move(buffer), {source, buffer_start, end}});
                buffer.clear();
            }
        };

        std::size_t position = 0;
        while (position < text.size())
        {
            if (text[position] != '@')
            {
                if (buffer.empty())
                    buffer_start = position;
                buffer.push_back(text[position++]);
                continue;
            }
            const auto start = position;
            const char next = position + 1 < text.size() ? text[position + 1] : '\0';
            if (next == '@')
            {
                if (buffer.empty())
                    buffer_start = position;
                buffer.push_back('@');
                position += 2;
                continue;
            }
            if (next == '/')
            {
                if (position + 2 < text.size() && text[position + 2] == '/')
                {
                    position = text.find('\n', position + 3);
                    if (position == std::string_view::npos)
                        position = text.size();
                    continue;
                }
                flush(start);
                position += 2;
                if (position >= text.size() || !name_start(text[position]))
                {
                    add_error(result, {source, start, position}, "EM0101",
                              "@/ must be followed by a capitalized name.", diagnostic_limit);
                    continue;
                }
                const auto name_start_at = position++;
                while (position < text.size() && name_continue(text[position]))
                    ++position;
                result.tokens.push_back({TokenKind::close,
                                         std::string{text.substr(name_start_at,
                                                                 position - name_start_at)},
                                         {},
                                         {},
                                         {source, start, position}});
                continue;
            }
            if (next == '{')
            {
                flush(start);
                std::string expression;
                const auto end = delimited(text, position + 1, '{', '}', expression);
                if (end == std::string_view::npos)
                {
                    add_error(result, {source, start, text.size()}, "EM0102",
                              "Unclosed expression; expected }.", diagnostic_limit);
                    position = text.size();
                }
                else
                {
                    if (expression.find_first_not_of(" \t\r\n") == std::string::npos)
                    {
                        add_error(result, {source, start, end}, "EM0103",
                                  "An interpolation expression cannot be empty.", diagnostic_limit);
                    }
                    result.tokens.push_back({TokenKind::expression, {}, {}, std::move(expression), {source, start, end}});
                    position = end;
                }
                continue;
            }
            if (next == '*')
            {
                flush(start);
                const auto end = text.find("*@", position + 2);
                if (end == std::string_view::npos)
                {
                    add_error(result, {source, start, text.size()}, "EM0104",
                              "Unclosed block comment; expected *@.", diagnostic_limit);
                    position = text.size();
                }
                else
                {
                    position = end + 2;
                }
                continue;
            }
            if (next == '[')
            {
                flush(start);
                const auto end = text.find(']', position + 2);
                const auto stop = end == std::string_view::npos ? text.size() : end + 1;
                add_error(result, {source, start, stop}, "EM0107",
                          "Square-bracket macro syntax is not part of Email Markup 1.",
                          diagnostic_limit);
                position = stop;
                continue;
            }
            if (!name_start(next))
            {
                if (buffer.empty())
                    buffer_start = position;
                buffer.push_back('@');
                ++position;
                continue;
            }

            flush(start);
            position += 2;
            while (position < text.size() && name_continue(text[position]))
                ++position;
            const auto name = std::string{text.substr(start + 1, position - start - 1)};
            if (name == "Engine")
            {
                add_error(result, {source, start, position}, "EM0108",
                          "@Engine is deferred syntax and is not part of Email Markup 1.",
                          diagnostic_limit);
            }
            std::string parameters;
            if (position < text.size() && text[position] == '(')
            {
                const auto end = delimited(text, position, '(', ')', parameters);
                if (end == std::string_view::npos)
                {
                    add_error(result, {source, start, text.size()}, "EM0105",
                              "Unclosed parameter list; expected ).", diagnostic_limit);
                    result.tokens.push_back({TokenKind::invalid, name, std::move(parameters), {}, {source, start, text.size()}});
                    position = text.size();
                    continue;
                }
                position = end;
            }
            if (position < text.size() && text[position] == ';')
            {
                ++position;
                result.tokens.push_back({TokenKind::self_closing, name, std::move(parameters), {}, {source, start, position}});
                continue;
            }
            result.tokens.push_back({TokenKind::open, name, std::move(parameters), {}, {source, start, position}});
            if (raw_body_names.contains(name))
            {
                const auto needle = "@/" + name;
                const auto end = text.find(needle, position);
                if (end == std::string_view::npos)
                {
                    add_error(result, {source, start, text.size()}, "EM0106",
                              "@" + name + " is not closed; expected " + needle + ".",
                              diagnostic_limit);
                    position = text.size();
                    continue;
                }
                if (end > position)
                {
                    result.tokens.push_back({TokenKind::text, {}, {}, std::string{text.substr(position, end - position)}, {source, position, end}});
                }
                result.tokens.push_back({TokenKind::close, name, {}, {}, {source, end, end + needle.size()}});
                position = end + needle.size();
            }
        }
        flush(position);
        result.tokens.push_back({TokenKind::end, {}, {}, {}, {source, position, position}});
        return result;
    }

} // namespace email_markup
