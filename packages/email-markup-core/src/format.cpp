#include "email-markup/core/format.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "email-markup/core/lexer.hpp"

namespace email_markup
{
    namespace
    {
        const std::unordered_set<std::string> void_tags{
            "area", "base", "br", "col", "embed", "hr", "img", "input", "link",
            "meta", "param", "source", "track", "wbr"};

        const std::unordered_set<std::string> raw_tags{
            "pre", "script", "style", "textarea"};

        const std::unordered_set<std::string> structural_directives{
            "DefineComponent", "DefineStyle", "DefineToken", "Else", "Each",
            "If", "Include", "Media", "Props", "Slots", "Template", "With"};

        constexpr std::size_t compact_line_width = 120;

        bool whitespace(const char ch)
        {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        }

        std::string lower(std::string_view value)
        {
            std::string result{value};
            std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return result;
        }

        struct HtmlTag;

        std::size_t matching_tag_end(std::string_view source,
                                     std::size_t opening_start,
                                     const HtmlTag &opening);

        std::size_t tag_end(const std::string_view source, const std::size_t start)
        {
            if (source.substr(start, 4) == "<!--")
            {
                const auto end = source.find("-->", start + 4);
                return end == std::string_view::npos ? source.size() : end + 3;
            }

            bool quoted = false;
            char quote = '\0';
            for (std::size_t i = start + 1; i < source.size(); ++i)
            {
                if (quoted)
                {
                    if (source[i] == quote)
                        quoted = false;
                }
                else if (source[i] == '\'' || source[i] == '"')
                {
                    quoted = true;
                    quote = source[i];
                }
                else if (source[i] == '>')
                {
                    return i + 1;
                }
            }
            return source.size();
        }

        struct HtmlTag
        {
            std::string name;
            bool closing{};
            bool self_closing{};
            bool declaration{};
        };

        HtmlTag inspect_tag(const std::string_view tag)
        {
            HtmlTag result;
            std::size_t position = 1;
            if (position < tag.size() && tag[position] == '/')
            {
                result.closing = true;
                ++position;
            }
            if (position < tag.size() && (tag[position] == '!' || tag[position] == '?'))
            {
                result.declaration = true;
                return result;
            }
            while (position < tag.size() && whitespace(tag[position]))
                ++position;
            const auto start = position;
            while (position < tag.size() &&
                   (std::isalnum(static_cast<unsigned char>(tag[position])) ||
                    tag[position] == '-' || tag[position] == ':'))
            {
                ++position;
            }
            result.name = lower(tag.substr(start, position - start));
            const auto slash = tag.find_last_not_of(" \t\r\n>");
            result.self_closing = slash != std::string_view::npos && tag[slash] == '/';
            return result;
        }

        bool tag_start(const std::string_view source, const std::size_t position)
        {
            if (position + 1 >= source.size() || source[position] != '<')
                return false;
            const auto next = static_cast<unsigned char>(source[position + 1]);
            return source[position + 1] == '!' || source[position + 1] == '?' ||
                   std::isalpha(next) ||
                   (source[position + 1] == '/' && position + 2 < source.size() &&
                    std::isalpha(static_cast<unsigned char>(source[position + 2])));
        }

        class LayoutWriter
        {
        public:
            void text(const std::string_view value)
            {
                auto start = std::size_t{};
                if (skip_leading_whitespace_)
                {
                    while (start < value.size() && whitespace(value[start]))
                        ++start;
                    if (start == value.size())
                        return;
                    skip_leading_whitespace_ = false;
                }
                output_.append(value.substr(start));
            }

            void token(const std::string_view value)
            {
                skip_leading_whitespace_ = false;
                output_.append(value);
            }

            void line()
            {
                while (!output_.empty() &&
                       (output_.back() == ' ' || output_.back() == '\t' || output_.back() == '\r'))
                {
                    output_.pop_back();
                }
                if (!output_.empty() && output_.back() != '\n')
                    output_.push_back('\n');
                skip_leading_whitespace_ = true;
            }

            std::string take() { return std::move(output_); }

        private:
            std::string output_;
            bool skip_leading_whitespace_{};
        };

        std::vector<std::pair<std::size_t, std::size_t>> raw_directive_ranges(
            const std::string_view source)
        {
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            const auto lexed = lex(0, source);
            for (std::size_t index = 0; index < lexed.tokens.size(); ++index)
            {
                const auto &token = lexed.tokens[index];
                if (token.kind != TokenKind::open ||
                    (token.name != "DefineStyle" && token.name != "Props" &&
                     token.name != "Slots" && token.name != "Media"))
                {
                    continue;
                }
                if (index + 1 < lexed.tokens.size() &&
                    lexed.tokens[index + 1].kind == TokenKind::text)
                    ranges.emplace_back(lexed.tokens[index + 1].range.start,
                                        lexed.tokens[index + 1].range.end);
                else if (index + 1 >= lexed.tokens.size() ||
                         lexed.tokens[index + 1].kind == TokenKind::end)
                    ranges.emplace_back(token.range.end, source.size());
            }
            std::sort(ranges.begin(), ranges.end());
            return ranges;
        }

        std::string add_html_layout(const std::string_view source)
        {
            LayoutWriter writer;
            const auto raw_ranges = raw_directive_ranges(source);
            std::size_t raw_index = 0;
            std::size_t position = 0;
            while (position < source.size())
            {
                while (raw_index < raw_ranges.size() &&
                       raw_ranges[raw_index].second <= position)
                {
                    ++raw_index;
                }
                if (raw_index < raw_ranges.size() &&
                    position >= raw_ranges[raw_index].first &&
                    position < raw_ranges[raw_index].second)
                {
                    const auto end = raw_ranges[raw_index].second;
                    writer.text(source.substr(position, end - position));
                    position = end;
                    continue;
                }
                const auto next = source.find('<', position);
                if (next == std::string_view::npos)
                {
                    writer.text(source.substr(position));
                    break;
                }
                if (raw_index < raw_ranges.size() && next >= raw_ranges[raw_index].first)
                {
                    writer.text(source.substr(position,
                                              raw_ranges[raw_index].first - position));
                    position = raw_ranges[raw_index].first;
                    continue;
                }
                writer.text(source.substr(position, next - position));
                if (!tag_start(source, next))
                {
                    writer.token("<");
                    position = next + 1;
                    continue;
                }

                auto end = tag_end(source, next);
                auto token = source.substr(next, end - next);
                auto tag = inspect_tag(token);
                bool raw_element = false;

                if (!tag.closing && !tag.self_closing && raw_tags.contains(tag.name))
                {
                    const auto closing = "</" + tag.name;
                    const auto lowered = lower(source.substr(end));
                    const auto close_at = lowered.find(closing);
                    if (close_at != std::string::npos)
                    {
                        const auto close_start = end + close_at;
                        end = tag_end(source, close_start);
                        token = source.substr(next, end - next);
                        raw_element = true;
                    }
                }

                const bool standalone = tag.declaration || tag.self_closing ||
                                        void_tags.contains(tag.name);
                if (!tag.closing && !standalone && !raw_element)
                {
                    const auto element_end = matching_tag_end(source, next, tag);
                    if (element_end != std::string_view::npos)
                    {
                        const auto close_start = source.rfind('<', element_end - 1);
                        bool has_child = false;
                        for (auto child = source.find('<', end);
                             child != std::string_view::npos && child < close_start;
                             child = source.find('<', child + 1))
                        {
                            if (tag_start(source, child))
                            {
                                has_child = true;
                                break;
                            }
                        }
                        if (!has_child)
                        {
                            writer.line();
                            writer.token(source.substr(next, element_end - next));
                            writer.line();
                            position = element_end;
                            continue;
                        }
                    }
                    else
                    {
                        writer.line();
                        writer.token(token);
                        position = end;
                        continue;
                    }
                }

                writer.line();
                writer.token(token);
                writer.line();
                position = end;
            }
            return writer.take();
        }

        std::string add_directive_layout(const std::string_view source)
        {
            LayoutWriter writer;
            const auto lexed = lex(0, source);
            std::size_t position = 0;
            for (const auto &token : lexed.tokens)
            {
                if (token.kind != TokenKind::open &&
                    token.kind != TokenKind::self_closing &&
                    token.kind != TokenKind::close)
                    continue;
                writer.text(source.substr(position, token.range.start - position));
                writer.line();
                std::string formatted;
                if (token.kind == TokenKind::close)
                {
                    formatted = "@/" + token.name;
                }
                else
                {
                    formatted = "@" + token.name;
                    const auto original = source.substr(
                        token.range.start, token.range.end - token.range.start);
                    if (original.find('(') != std::string_view::npos)
                    {
                        std::string parameters;
                        bool quoted = false;
                        bool escaped = false;
                        bool pending_space = false;
                        for (const char ch : token.parameters)
                        {
                            if (ch == '"' && !escaped)
                                quoted = !quoted;
                            if (!quoted && whitespace(ch))
                            {
                                pending_space = !parameters.empty();
                                continue;
                            }
                            if (pending_space && ch != ',' && ch != ')')
                                parameters.push_back(' ');
                            pending_space = false;
                            parameters.push_back(ch);
                            escaped = quoted && ch == '\\' && !escaped;
                            if (ch != '\\')
                                escaped = false;
                        }
                        formatted += "(" + parameters + ")";
                    }
                    if (token.kind == TokenKind::self_closing)
                        formatted.push_back(';');
                }
                writer.token(formatted);
                writer.line();
                position = token.range.end;
            }
            writer.text(source.substr(position));
            return writer.take();
        }

        int html_delta(const std::string_view line, int &leading_closes)
        {
            int delta = 0;
            bool only_leading_whitespace = true;
            for (std::size_t position = 0; position < line.size();)
            {
                if (!tag_start(line, position))
                {
                    if (!whitespace(line[position]))
                        only_leading_whitespace = false;
                    ++position;
                    continue;
                }
                const auto end = tag_end(line, position);
                const auto tag = inspect_tag(line.substr(position, end - position));
                if (tag.closing)
                {
                    --delta;
                    if (only_leading_whitespace)
                        ++leading_closes;
                }
                else if (!tag.declaration && !tag.self_closing &&
                         !void_tags.contains(tag.name))
                {
                    ++delta;
                }
                only_leading_whitespace = false;
                position = end;
            }
            return delta;
        }

        int directive_delta(const std::string_view line, int &leading_closes)
        {
            int delta = 0;
            bool only_leading_whitespace = true;
            for (std::size_t position = 0; position < line.size();)
            {
                if (line[position] != '@')
                {
                    if (!whitespace(line[position]))
                        only_leading_whitespace = false;
                    ++position;
                    continue;
                }
                if (position + 1 >= line.size())
                    break;
                if (line[position + 1] == '@')
                {
                    position += 2;
                    only_leading_whitespace = false;
                    continue;
                }
                if (line[position + 1] == '{')
                {
                    const auto end = line.find('}', position + 2);
                    position = end == std::string_view::npos ? line.size() : end + 1;
                    only_leading_whitespace = false;
                    continue;
                }

                bool closing = false;
                auto name_start = position + 1;
                if (line[name_start] == '/')
                {
                    closing = true;
                    ++name_start;
                }
                if (name_start >= line.size() ||
                    !std::isupper(static_cast<unsigned char>(line[name_start])))
                {
                    ++position;
                    only_leading_whitespace = false;
                    continue;
                }
                auto end = name_start + 1;
                while (end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_'))
                {
                    ++end;
                }
                const std::string name{line.substr(name_start, end - name_start)};
                if (closing)
                {
                    --delta;
                    if (only_leading_whitespace)
                        ++leading_closes;
                }
                else if (!closing && name == "Else")
                {
                    if (only_leading_whitespace)
                        ++leading_closes;
                }
                else if (!closing)
                {
                    auto directive_end = end;
                    int parentheses = 0;
                    bool quoted = false;
                    for (; directive_end < line.size(); ++directive_end)
                    {
                        const char ch = line[directive_end];
                        if (ch == '"' && (directive_end == 0 || line[directive_end - 1] != '\\'))
                            quoted = !quoted;
                        if (!quoted && ch == '(')
                            ++parentheses;
                        else if (!quoted && ch == ')')
                            --parentheses;
                        else if (!quoted && parentheses == 0 && ch == ';')
                            break;
                        else if (!quoted && parentheses == 0 && whitespace(ch))
                            break;
                    }
                    if (directive_end >= line.size() || line[directive_end] != ';')
                        ++delta;
                }
                only_leading_whitespace = false;
                position = end;
            }
            return delta;
        }

        std::string indent_lines(const std::string_view source)
        {
            std::istringstream stream{std::string{source}};
            std::string line;
            std::string output;
            int depth = 0;
            std::string raw_tag;
            std::string raw_directive;
            while (std::getline(stream, line))
            {
                if (!raw_tag.empty())
                {
                    output += line;
                    output.push_back('\n');
                    const auto closing = "</" + raw_tag;
                    if (lower(line).find(closing) != std::string::npos)
                    {
                        int ignored = 0;
                        depth = std::max(0, depth + html_delta(line, ignored));
                        raw_tag.clear();
                    }
                    continue;
                }

                if (!raw_directive.empty())
                {
                    const auto first = line.find_first_not_of(" \t\r");
                    if (first == std::string::npos)
                        continue;
                    line.erase(0, first);
                    const auto last = line.find_last_not_of(" \t\r");
                    line.erase(last + 1);
                    if (line == "@/" + raw_directive)
                    {
                        int leading_closes = 0;
                        const auto directives = directive_delta(line, leading_closes);
                        output.append(static_cast<std::size_t>(
                                          std::max(0, depth - leading_closes)) *
                                          2,
                                      ' ');
                        output += line;
                        output.push_back('\n');
                        depth = std::max(0, depth + directives);
                        raw_directive.clear();
                    }
                    else
                    {
                        output.append(static_cast<std::size_t>(depth) * 2, ' ');
                        output += line;
                        output.push_back('\n');
                    }
                    continue;
                }

                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos)
                {
                    if (!output.empty() && output.back() != '\n')
                        output.push_back('\n');
                    continue;
                }
                line.erase(0, first);
                const auto last = line.find_last_not_of(" \t\r");
                line.erase(last + 1);

                int leading_closes = 0;
                const auto html = html_delta(line, leading_closes);
                const auto directives = directive_delta(line, leading_closes);
                const auto indentation = std::max(0, depth - leading_closes);
                output.append(static_cast<std::size_t>(indentation) * 2, ' ');
                output += line;
                output.push_back('\n');
                depth = std::max(0, depth + html + directives);

                for (const auto &candidate : {"Props", "Slots", "DefineStyle", "Media"})
                {
                    const auto opening = "@" + std::string{candidate};
                    if (line.starts_with(opening))
                    {
                        raw_directive = candidate;
                        break;
                    }
                }

                const auto lowered = lower(line);
                for (const auto &candidate : raw_tags)
                {
                    const auto opening = "<" + std::string{candidate};
                    const auto closing = "</" + std::string{candidate};
                    if (lowered.find(opening) != std::string::npos &&
                        lowered.find(closing) == std::string::npos)
                    {
                        raw_tag = candidate;
                        break;
                    }
                }
            }
            return output;
        }

        std::string_view trimmed(const std::string_view value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        std::size_t matching_tag_end(const std::string_view source,
                                     const std::size_t opening_start,
                                     const HtmlTag &opening)
        {
            int depth = 0;
            for (auto position = opening_start; position < source.size();)
            {
                const auto next = source.find('<', position);
                if (next == std::string_view::npos)
                    return std::string_view::npos;
                if (!tag_start(source, next))
                {
                    position = next + 1;
                    continue;
                }
                const auto end = tag_end(source, next);
                const auto tag = inspect_tag(source.substr(next, end - next));
                if (tag.name == opening.name)
                {
                    if (tag.closing)
                    {
                        if (--depth == 0)
                            return end;
                    }
                    else if (!tag.self_closing && !void_tags.contains(tag.name))
                    {
                        ++depth;
                    }
                }
                position = end;
            }
            return std::string_view::npos;
        }

        std::string compact_simple_directives(const std::string_view source)
        {
            std::istringstream stream{std::string{source}};
            std::vector<std::string> lines;
            for (std::string line; std::getline(stream, line);)
                lines.push_back(std::move(line));

            std::string output;
            for (std::size_t index = 0; index < lines.size(); ++index)
            {
                const auto &opening_line = lines[index];
                const auto first = opening_line.find_first_not_of(" \t");
                const auto opening = first == std::string::npos
                                         ? std::string_view{}
                                         : std::string_view{opening_line}.substr(first);
                auto name_end = std::size_t{1};
                while (name_end < opening.size() &&
                       (std::isalnum(static_cast<unsigned char>(opening[name_end])) ||
                        opening[name_end] == '_'))
                {
                    ++name_end;
                }
                const auto name = opening.substr(1, name_end - 1);
                const auto body = index + 1 < lines.size()
                                      ? trimmed(lines[index + 1])
                                      : std::string_view{};
                const auto closing = "@/" + std::string{name};
                const bool simple = opening.starts_with('@') && !name.empty() &&
                                    std::isupper(static_cast<unsigned char>(name.front())) &&
                                    !opening.ends_with(';') &&
                                    !structural_directives.contains(std::string{name}) &&
                                    index + 2 < lines.size() && !body.empty() &&
                                    body.find_first_of("<@") == std::string_view::npos &&
                                    trimmed(lines[index + 2]) == closing &&
                                    opening.size() + body.size() + closing.size() + 2 <=
                                        compact_line_width;
                if (simple)
                {
                    output.append(opening_line, 0, first);
                    output.append(opening);
                    output.push_back(' ');
                    output.append(body);
                    output.push_back(' ');
                    output += closing;
                    output.push_back('\n');
                    index += 2;
                    continue;
                }

                output += opening_line;
                output.push_back('\n');
            }
            return output;
        }

        bool top_level_line(const std::string_view line, const std::string_view prefix)
        {
            return line.starts_with(prefix);
        }

        std::string separate_top_level_constructs(const std::string_view source)
        {
            std::istringstream stream{std::string{source}};
            std::string line;
            std::string output;
            bool needs_spacing = false;
            while (std::getline(stream, line))
            {
                if (needs_spacing)
                {
                    if (!line.empty())
                        output.push_back('\n');
                    needs_spacing = false;
                }
                const bool opening = top_level_line(line, "@Include(") ||
                                     top_level_line(line, "@DefineComponent") ||
                                     top_level_line(line, "@DefineStyle") ||
                                     top_level_line(line, "@DefineToken") ||
                                     top_level_line(line, "@Media");
                if (opening && !output.empty() &&
                    (output.size() < 2 || output[output.size() - 2] != '\n'))
                    output.push_back('\n');

                output += line;
                output.push_back('\n');

                const bool self_contained = top_level_line(line, "@Include(") ||
                                            top_level_line(line, "@DefineToken");
                const bool closing = top_level_line(line, "@/DefineComponent") ||
                                     top_level_line(line, "@/DefineStyle") ||
                                     top_level_line(line, "@/Media");
                needs_spacing = self_contained || closing;
            }
            return output;
        }
    } // namespace

    std::string format_source(const std::string_view source)
    {
        std::string normalized;
        normalized.reserve(source.size() + 1);
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '\r' && i + 1 < source.size() && source[i + 1] == '\n')
                continue;
            normalized.push_back(source[i]);
        }
        return separate_top_level_constructs(
            compact_simple_directives(
                indent_lines(add_directive_layout(add_html_layout(normalized)))));
    }

} // namespace email_markup
