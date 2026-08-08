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
        const std::unordered_set<std::string> container_tags{
            "html", "head", "body", "main", "header", "footer", "section",
            "article", "aside", "nav", "div", "table", "thead", "tbody",
            "tfoot", "tr", "ul", "ol", "dl", "figure", "figcaption", "form"};

        const std::unordered_set<std::string> compact_block_tags{
            "p", "h1", "h2", "h3", "h4", "h5", "h6", "td", "th", "li",
            "dt", "dd", "blockquote", "address", "title", "pre", "textarea",
            "style", "script"};

        const std::unordered_set<std::string> line_void_tags{
            "base", "link", "meta", "hr"};

        const std::unordered_set<std::string> void_tags{
            "area", "base", "br", "col", "embed", "hr", "img", "input", "link",
            "meta", "param", "source", "track", "wbr"};

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

                if (!tag.closing && !tag.self_closing &&
                    (tag.name == "pre" || tag.name == "textarea" || tag.name == "style" ||
                     tag.name == "script"))
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

                const bool container = container_tags.contains(tag.name);
                const bool compact = compact_block_tags.contains(tag.name);
                const bool line_void = line_void_tags.contains(tag.name);
                const bool standalone = tag.declaration || line_void ||
                                        (tag.self_closing && (container || compact));

                if (container || (!tag.closing && compact) || standalone)
                    writer.line();
                if (tag.closing && container)
                    writer.line();

                writer.token(token);

                if (container || standalone || raw_element || (tag.closing && compact) ||
                    (tag.self_closing && compact))
                {
                    writer.line();
                }
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
                const bool block = container_tags.contains(tag.name) ||
                                   compact_block_tags.contains(tag.name);
                if (block && tag.closing)
                {
                    --delta;
                    if (only_leading_whitespace)
                        ++leading_closes;
                }
                else if (block && !tag.self_closing && !void_tags.contains(tag.name))
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

                const auto lowered = lower(line);
                for (const auto &candidate : {"pre", "textarea", "style", "script"})
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
            indent_lines(add_directive_layout(add_html_layout(normalized))));
    }

} // namespace email_markup
