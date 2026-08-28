#include "email-markup/core/css.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace email_markup
{
    namespace
    {
        struct CssDeclaration
        {
            std::string name;
            std::string value;
            bool important{};
            std::size_t order{};
        };

        enum class SelectorKind
        {
            class_name,
            tag_name
        };

        struct CssSelector
        {
            SelectorKind kind{};
            std::string value;
            int specificity{};
        };

        struct CssRule
        {
            std::vector<CssSelector> selectors;
            std::vector<std::string> unsupported_selectors;
            std::vector<CssDeclaration> declarations;
        };

        struct MediaRule
        {
            std::string query;
            std::vector<CssRule> rules;
            std::vector<std::string> retained_unsupported;
        };

        struct CssIssue
        {
            std::string code;
            std::string message;
            std::size_t offset{};
        };

        struct ParsedStylesheet
        {
            std::vector<CssRule> rules;
            std::vector<MediaRule> media;
            std::vector<std::string> retained_unsupported;
            std::vector<CssIssue> issues;
        };

        struct StyleBlock
        {
            std::size_t start{};
            std::size_t body_start{};
            std::size_t end{};
            ParsedStylesheet stylesheet;
        };

        struct HtmlElement
        {
            std::size_t start{};
            std::size_t end{};
            std::string tag_name;
            std::set<std::string> class_names;
            bool has_class_attribute{};
        };

        struct CascadeValue
        {
            std::string value;
            bool important{};
            int specificity{};
            std::size_t order{};
        };

        [[nodiscard]] bool ascii_iequal(const char left, const char right)
        {
            return std::tolower(static_cast<unsigned char>(left)) ==
                   std::tolower(static_cast<unsigned char>(right));
        }

        [[nodiscard]] bool starts_with_case_insensitive(
            const std::string_view text, const std::size_t offset,
            const std::string_view expected)
        {
            if (offset + expected.size() > text.size())
                return false;
            for (std::size_t index = 0; index < expected.size(); ++index)
                if (!ascii_iequal(text[offset + index], expected[index]))
                    return false;
            return true;
        }

        [[nodiscard]] std::string lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](const unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        [[nodiscard]] std::string_view trim_view(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] std::string trim(const std::string_view value)
        {
            return std::string{trim_view(value)};
        }

        [[nodiscard]] bool is_identifier_start(const char ch)
        {
            const auto value = static_cast<unsigned char>(ch);
            return std::isalpha(value) || ch == '_';
        }

        [[nodiscard]] bool is_identifier_character(const char ch)
        {
            const auto value = static_cast<unsigned char>(ch);
            return std::isalnum(value) || ch == '_' || ch == '-';
        }

        [[nodiscard]] bool is_identifier(const std::string_view value)
        {
            if (value.empty() || !is_identifier_start(value.front()))
                return false;
            return std::all_of(value.begin() + 1, value.end(), is_identifier_character);
        }

        void append_slice(GeneratedHtml &output, const GeneratedHtml &input,
                          const std::size_t start, const std::size_t end)
        {
            if (start >= end)
                return;
            for (const auto &segment : input.segments)
            {
                const auto slice_start = std::max(start, segment.output_start);
                const auto slice_end = std::min(end, segment.output_end);
                if (slice_start >= slice_end)
                    continue;
                output.append(std::string_view{input.html}.substr(slice_start, slice_end - slice_start),
                              segment.origin, segment.expansion_stack);
            }
        }

        [[nodiscard]] const OutputSegment *segment_at(
            const GeneratedHtml &input, const std::size_t offset)
        {
            const auto found = std::find_if(input.segments.begin(), input.segments.end(),
                                            [&](const auto &segment)
                                            {
                                                return offset >= segment.output_start &&
                                                       offset < segment.output_end;
                                            });
            return found == input.segments.end() ? nullptr : &*found;
        }

        void append_with_origin(GeneratedHtml &output, const GeneratedHtml &input,
                                const std::size_t offset, const std::string_view text)
        {
            if (const auto *segment = segment_at(input, offset))
                output.append(text, segment->origin, segment->expansion_stack);
            else
                output.append(text, {});
        }

        class StylesheetParser
        {
          public:
            explicit StylesheetParser(const std::string_view css,
                                      const bool inside_media = false)
                : css_(css), inside_media_(inside_media)
            {
            }

            [[nodiscard]] ParsedStylesheet parse()
            {
                ParsedStylesheet result;
                parse_rule_list(result.rules, result.media, result.retained_unsupported,
                                result.issues);
                return result;
            }

          private:
            void skip_ignored(std::vector<CssIssue> &issues)
            {
                while (position_ < css_.size())
                {
                    if (std::isspace(static_cast<unsigned char>(css_[position_])))
                    {
                        ++position_;
                        continue;
                    }
                    if (position_ + 1 < css_.size() && css_[position_] == '/' &&
                        css_[position_ + 1] == '*')
                    {
                        const auto start = position_;
                        position_ += 2;
                        const auto end = css_.find("*/", position_);
                        if (end == std::string_view::npos)
                        {
                            issues.push_back({"EM0521", "CSS comment is not closed.", start});
                            position_ = css_.size();
                            return;
                        }
                        position_ = end + 2;
                        continue;
                    }
                    break;
                }
            }

            [[nodiscard]] std::size_t scan_until(const char terminator) const
            {
                char quote = 0;
                bool escaped = false;
                int parentheses = 0;
                int brackets = 0;
                for (auto cursor = position_; cursor < css_.size(); ++cursor)
                {
                    const char ch = css_[cursor];
                    if (quote != 0)
                    {
                        if (escaped)
                            escaped = false;
                        else if (ch == '\\')
                            escaped = true;
                        else if (ch == quote)
                            quote = 0;
                        continue;
                    }
                    if (cursor + 1 < css_.size() && ch == '/' && css_[cursor + 1] == '*')
                    {
                        const auto end = css_.find("*/", cursor + 2);
                        if (end == std::string_view::npos)
                            return std::string_view::npos;
                        cursor = end + 1;
                        continue;
                    }
                    if (ch == '\'' || ch == '"')
                    {
                        quote = ch;
                        continue;
                    }
                    if (ch == '(')
                        ++parentheses;
                    else if (ch == ')' && parentheses > 0)
                        --parentheses;
                    else if (ch == '[')
                        ++brackets;
                    else if (ch == ']' && brackets > 0)
                        --brackets;
                    else if (parentheses == 0 && brackets == 0 && ch == terminator)
                        return cursor;
                }
                return std::string_view::npos;
            }

            [[nodiscard]] std::size_t matching_brace(const std::size_t open) const
            {
                char quote = 0;
                bool escaped = false;
                int depth = 1;
                for (auto cursor = open + 1; cursor < css_.size(); ++cursor)
                {
                    const char ch = css_[cursor];
                    if (quote != 0)
                    {
                        if (escaped)
                            escaped = false;
                        else if (ch == '\\')
                            escaped = true;
                        else if (ch == quote)
                            quote = 0;
                        continue;
                    }
                    if (cursor + 1 < css_.size() && ch == '/' && css_[cursor + 1] == '*')
                    {
                        const auto end = css_.find("*/", cursor + 2);
                        if (end == std::string_view::npos)
                            return std::string_view::npos;
                        cursor = end + 1;
                        continue;
                    }
                    if (ch == '\'' || ch == '"')
                        quote = ch;
                    else if (ch == '{')
                        ++depth;
                    else if (ch == '}' && --depth == 0)
                        return cursor;
                }
                return std::string_view::npos;
            }

            static std::vector<CssDeclaration> parse_declaration_list(
                const std::string_view body, const std::size_t body_offset,
                std::vector<CssIssue> &issues)
            {
                std::vector<CssDeclaration> declarations;
                std::string cleaned{body};
                char comment_quote = 0;
                bool comment_escaped = false;
                for (std::size_t index = 0; index < cleaned.size(); ++index)
                {
                    const char ch = cleaned[index];
                    if (comment_quote != 0)
                    {
                        if (comment_escaped)
                            comment_escaped = false;
                        else if (ch == '\\')
                            comment_escaped = true;
                        else if (ch == comment_quote)
                            comment_quote = 0;
                        continue;
                    }
                    if (ch == '\'' || ch == '"')
                    {
                        comment_quote = ch;
                        continue;
                    }
                    if (index + 1 < cleaned.size() && ch == '/' && cleaned[index + 1] == '*')
                    {
                        const auto end = cleaned.find("*/", index + 2);
                        if (end == std::string::npos)
                        {
                            issues.push_back({"EM0521", "CSS comment is not closed.",
                                              body_offset + index});
                            std::fill(cleaned.begin() + static_cast<std::ptrdiff_t>(index),
                                      cleaned.end(), ' ');
                            break;
                        }
                        std::fill(cleaned.begin() + static_cast<std::ptrdiff_t>(index),
                                  cleaned.begin() + static_cast<std::ptrdiff_t>(end + 2), ' ');
                        index = end + 1;
                    }
                }
                const std::string_view parsed{cleaned};
                std::size_t start = 0;
                std::size_t declaration_order = 0;
                char quote = 0;
                bool escaped = false;
                int parentheses = 0;
                const auto commit = [&](const std::size_t end)
                {
                    const auto declaration = trim_view(parsed.substr(start, end - start));
                    if (declaration.empty())
                        return;
                    char inner_quote = 0;
                    bool inner_escaped = false;
                    int inner_parentheses = 0;
                    std::size_t colon = std::string_view::npos;
                    for (std::size_t index = 0; index < declaration.size(); ++index)
                    {
                        const char ch = declaration[index];
                        if (inner_quote != 0)
                        {
                            if (inner_escaped)
                                inner_escaped = false;
                            else if (ch == '\\')
                                inner_escaped = true;
                            else if (ch == inner_quote)
                                inner_quote = 0;
                            continue;
                        }
                        if (ch == '\'' || ch == '"')
                            inner_quote = ch;
                        else if (ch == '(')
                            ++inner_parentheses;
                        else if (ch == ')' && inner_parentheses > 0)
                            --inner_parentheses;
                        else if (ch == ':' && inner_parentheses == 0)
                        {
                            colon = index;
                            break;
                        }
                    }
                    if (colon == std::string_view::npos)
                    {
                        issues.push_back({"EM0521", "CSS declaration requires a property and value.",
                                          body_offset + start});
                        return;
                    }
                    auto name = lowercase(trim(declaration.substr(0, colon)));
                    auto value = trim(declaration.substr(colon + 1));
                    if (name.empty() || value.empty())
                    {
                        issues.push_back({"EM0521", "CSS declaration requires a property and value.",
                                          body_offset + start});
                        return;
                    }
                    bool important = false;
                    const auto bang = value.rfind('!');
                    if (bang != std::string::npos &&
                        lowercase(trim(std::string_view{value}.substr(bang + 1))) == "important")
                    {
                        value = trim(std::string_view{value}.substr(0, bang));
                        important = true;
                    }
                    declarations.push_back(
                        {std::move(name), std::move(value), important, declaration_order++});
                };
                for (std::size_t index = 0; index < parsed.size(); ++index)
                {
                    const char ch = parsed[index];
                    if (quote != 0)
                    {
                        if (escaped)
                            escaped = false;
                        else if (ch == '\\')
                            escaped = true;
                        else if (ch == quote)
                            quote = 0;
                    }
                    else if (ch == '\'' || ch == '"')
                        quote = ch;
                    else if (ch == '(')
                        ++parentheses;
                    else if (ch == ')' && parentheses > 0)
                        --parentheses;
                    else if (ch == ';' && parentheses == 0)
                    {
                        commit(index);
                        start = index + 1;
                    }
                }
                commit(parsed.size());
                if (quote != 0 || parentheses != 0)
                    issues.push_back({"EM0521", "CSS declaration has an unclosed string or function.",
                                      body_offset});
                return declarations;
            }

            static void parse_selectors(const std::string_view prelude,
                                        const std::size_t offset, CssRule &rule,
                                        std::vector<CssIssue> &issues)
            {
                std::size_t start = 0;
                char quote = 0;
                bool escaped = false;
                int parentheses = 0;
                int brackets = 0;
                const auto commit = [&](const std::size_t end)
                {
                    auto selector = trim(prelude.substr(start, end - start));
                    if (selector.empty())
                    {
                        issues.push_back({"EM0521", "CSS selector list contains an empty selector.",
                                          offset + start});
                        return;
                    }
                    if (selector.front() == '.' &&
                        is_identifier(std::string_view{selector}.substr(1)))
                    {
                        rule.selectors.push_back(
                            {SelectorKind::class_name, selector.substr(1), 100});
                    }
                    else if (is_identifier(selector))
                    {
                        rule.selectors.push_back(
                            {SelectorKind::tag_name, lowercase(selector), 1});
                    }
                    else
                    {
                        rule.unsupported_selectors.push_back(selector);
                        issues.push_back(
                            {"EM0520",
                             "Unsupported CSS selector “" + selector +
                                 "”; v1 supports only simple class and tag selectors.",
                             offset + start});
                    }
                };
                for (std::size_t index = 0; index < prelude.size(); ++index)
                {
                    const char ch = prelude[index];
                    if (quote != 0)
                    {
                        if (escaped)
                            escaped = false;
                        else if (ch == '\\')
                            escaped = true;
                        else if (ch == quote)
                            quote = 0;
                    }
                    else if (ch == '\'' || ch == '"')
                        quote = ch;
                    else if (ch == '(')
                        ++parentheses;
                    else if (ch == ')' && parentheses > 0)
                        --parentheses;
                    else if (ch == '[')
                        ++brackets;
                    else if (ch == ']' && brackets > 0)
                        --brackets;
                    else if (ch == ',' && parentheses == 0 && brackets == 0)
                    {
                        commit(index);
                        start = index + 1;
                    }
                }
                commit(prelude.size());
            }

            [[nodiscard]] CssRule parse_ordinary_rule(
                const std::size_t start, const std::size_t open, const std::size_t close,
                std::vector<CssIssue> &issues)
            {
                CssRule rule;
                parse_selectors(css_.substr(start, open - start), start, rule, issues);
                rule.declarations = parse_declaration_list(
                    css_.substr(open + 1, close - open - 1), open + 1, issues);
                return rule;
            }

            void parse_rule_list(std::vector<CssRule> &rules,
                                 std::vector<MediaRule> &media,
                                 std::vector<std::string> &retained_unsupported,
                                 std::vector<CssIssue> &issues)
            {
                while (position_ < css_.size())
                {
                    skip_ignored(issues);
                    if (position_ >= css_.size())
                        return;
                    if (css_[position_] == '}')
                    {
                        issues.push_back({"EM0521", "CSS contains an unmatched closing brace.",
                                          position_++});
                        continue;
                    }
                    const auto start = position_;
                    const auto open = scan_until('{');
                    const auto semicolon = scan_until(';');
                    if (css_[start] == '@' && semicolon != std::string_view::npos &&
                        (open == std::string_view::npos || semicolon < open))
                    {
                        retained_unsupported.push_back(
                            trim(css_.substr(start, semicolon - start + 1)));
                        issues.push_back({"EM0522", "Unsupported CSS at-rule; only @media is supported.",
                                          start});
                        position_ = semicolon + 1;
                        continue;
                    }
                    if (open == std::string_view::npos)
                    {
                        retained_unsupported.push_back(trim(css_.substr(start)));
                        issues.push_back({"EM0521", "CSS rule is missing an opening brace.", start});
                        position_ = css_.size();
                        return;
                    }
                    const auto close = matching_brace(open);
                    if (close == std::string_view::npos)
                    {
                        retained_unsupported.push_back(trim(css_.substr(start)));
                        issues.push_back({"EM0521", "CSS rule is missing a closing brace.", start});
                        position_ = css_.size();
                        return;
                    }
                    const auto prelude = trim_view(css_.substr(start, open - start));
                    if (!prelude.empty() && prelude.front() == '@')
                    {
                        if (!inside_media_ &&
                            starts_with_case_insensitive(prelude, 0, "@media") &&
                            (prelude.size() == 6 ||
                             std::isspace(static_cast<unsigned char>(prelude[6])) ||
                             prelude[6] == '('))
                        {
                            MediaRule media_rule;
                            media_rule.query = trim(prelude.substr(6));
                            if (media_rule.query.empty())
                                issues.push_back({"EM0521", "A media query cannot be empty.", start});
                            StylesheetParser nested{
                                css_.substr(open + 1, close - open - 1), true};
                            auto nested_result = nested.parse();
                            media_rule.rules = std::move(nested_result.rules);
                            media_rule.retained_unsupported =
                                std::move(nested_result.retained_unsupported);
                            for (auto &issue : nested_result.issues)
                            {
                                issue.offset += open + 1;
                                issues.push_back(std::move(issue));
                            }
                            media.push_back(std::move(media_rule));
                        }
                        else
                        {
                            retained_unsupported.push_back(
                                trim(css_.substr(start, close - start + 1)));
                            issues.push_back(
                                {"EM0522", "Unsupported CSS at-rule; only top-level @media is supported.",
                                 start});
                        }
                    }
                    else
                    {
                        rules.push_back(parse_ordinary_rule(start, open, close, issues));
                    }
                    position_ = close + 1;
                }
            }

            std::string_view css_;
            bool inside_media_{};
            std::size_t position_{};
        };

        [[nodiscard]] std::vector<CssDeclaration> parse_declarations_detailed(
            const std::string_view css)
        {
            const std::string wrapped = ".inline{" + std::string{css} + "}";
            StylesheetParser parser{wrapped};
            auto stylesheet = parser.parse();
            if (stylesheet.rules.empty())
                return {};
            return std::move(stylesheet.rules.front().declarations);
        }

        [[nodiscard]] std::size_t find_tag_end(
            const std::string_view html, const std::size_t start)
        {
            char quote = 0;
            for (auto position = start + 1; position < html.size(); ++position)
            {
                const char ch = html[position];
                if (quote != 0)
                {
                    if (ch == quote)
                        quote = 0;
                }
                else if (ch == '\'' || ch == '"')
                    quote = ch;
                else if (ch == '>')
                    return position + 1;
            }
            return std::string_view::npos;
        }

        [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
        find_attribute_value(const std::string_view tag, const std::string_view wanted)
        {
            std::size_t position = 1;
            while (position < tag.size() && is_identifier_character(tag[position]))
                ++position;
            while (position < tag.size())
            {
                while (position < tag.size() &&
                       (std::isspace(static_cast<unsigned char>(tag[position])) ||
                        tag[position] == '/'))
                    ++position;
                const auto name_start = position;
                while (position < tag.size() &&
                       (is_identifier_character(tag[position]) || tag[position] == ':'))
                    ++position;
                if (name_start == position)
                {
                    ++position;
                    continue;
                }
                const auto name = lowercase(std::string{tag.substr(name_start, position - name_start)});
                while (position < tag.size() &&
                       std::isspace(static_cast<unsigned char>(tag[position])))
                    ++position;
                if (position >= tag.size() || tag[position] != '=')
                    continue;
                ++position;
                while (position < tag.size() &&
                       std::isspace(static_cast<unsigned char>(tag[position])))
                    ++position;
                if (position >= tag.size())
                    return std::nullopt;
                const auto quote = tag[position];
                if (quote == '\'' || quote == '"')
                {
                    const auto value_start = ++position;
                    while (position < tag.size() && tag[position] != quote)
                        ++position;
                    if (name == wanted)
                        return std::pair{value_start, position};
                    if (position < tag.size())
                        ++position;
                }
                else
                {
                    const auto value_start = position;
                    while (position < tag.size() &&
                           !std::isspace(static_cast<unsigned char>(tag[position])) &&
                           tag[position] != '>')
                        ++position;
                    if (name == wanted)
                        return std::pair{value_start, position};
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<HtmlElement> scan_elements(const std::string_view html)
        {
            std::vector<HtmlElement> elements;
            std::size_t position = 0;
            while ((position = html.find('<', position)) != std::string_view::npos)
            {
                if (position + 1 >= html.size() || !is_identifier_start(html[position + 1]))
                {
                    ++position;
                    continue;
                }
                const auto end = find_tag_end(html, position);
                if (end == std::string_view::npos)
                    break;
                auto name_end = position + 1;
                while (name_end < end && is_identifier_character(html[name_end]))
                    ++name_end;
                HtmlElement element;
                element.start = position;
                element.end = end;
                element.tag_name = lowercase(std::string{
                    html.substr(position + 1, name_end - position - 1)});
                const auto tag = html.substr(position, end - position);
                if (const auto classes = find_attribute_value(tag, "class"))
                {
                    element.has_class_attribute = true;
                    const auto value = tag.substr(classes->first,
                                                  classes->second - classes->first);
                    std::size_t class_start = 0;
                    while (class_start < value.size())
                    {
                        while (class_start < value.size() &&
                               std::isspace(static_cast<unsigned char>(value[class_start])))
                            ++class_start;
                        auto class_end = class_start;
                        while (class_end < value.size() &&
                               !std::isspace(static_cast<unsigned char>(value[class_end])))
                            ++class_end;
                        if (class_end > class_start)
                            element.class_names.emplace(value.substr(class_start,
                                                                     class_end - class_start));
                        class_start = class_end;
                    }
                }
                elements.push_back(std::move(element));
                position = end;
            }
            return elements;
        }

        [[nodiscard]] bool selector_matches(const CssSelector &selector,
                                            const HtmlElement &element)
        {
            if (selector.kind == SelectorKind::tag_name)
                return selector.value == element.tag_name;
            return element.class_names.contains(selector.value);
        }

        [[nodiscard]] bool selector_is_used(const CssSelector &selector,
                                            const std::set<std::string> &tags,
                                            const std::set<std::string> &classes)
        {
            return selector.kind == SelectorKind::tag_name
                       ? tags.contains(selector.value)
                       : classes.contains(selector.value);
        }

        [[nodiscard]] std::string selector_text(const CssSelector &selector)
        {
            return selector.kind == SelectorKind::class_name ? "." + selector.value
                                                             : selector.value;
        }

        [[nodiscard]] std::string serialize_rule(
            const CssRule &rule, const std::vector<std::string> &selectors)
        {
            if (selectors.empty() || rule.declarations.empty())
                return {};
            std::string output;
            for (const auto &selector : selectors)
            {
                if (!output.empty())
                    output.push_back(',');
                output += selector;
            }
            output.push_back('{');
            for (const auto &declaration : rule.declarations)
            {
                output += declaration.name + ":" + declaration.value;
                if (declaration.important)
                    output += "!important";
                output.push_back(';');
            }
            output.push_back('}');
            return output;
        }

        [[nodiscard]] std::string retained_css(
            const ParsedStylesheet &stylesheet, const std::set<std::string> &tags,
            const std::set<std::string> &classes)
        {
            std::string output;
            for (const auto &rule : stylesheet.rules)
                if (!rule.unsupported_selectors.empty())
                    output += serialize_rule(rule, rule.unsupported_selectors);
            for (const auto &raw : stylesheet.retained_unsupported)
                output += raw;
            for (const auto &media : stylesheet.media)
            {
                std::string body;
                for (const auto &rule : media.rules)
                {
                    std::vector<std::string> retained = rule.unsupported_selectors;
                    for (const auto &selector : rule.selectors)
                        if (selector_is_used(selector, tags, classes))
                            retained.push_back(selector_text(selector));
                    body += serialize_rule(rule, retained);
                }
                for (const auto &raw : media.retained_unsupported)
                    body += raw;
                if (!body.empty())
                    output += "@media " + media.query + "{" + body + "}";
            }
            return output;
        }

        void consider(std::map<std::string, CascadeValue> &cascade,
                      const CssDeclaration &declaration, const int specificity,
                      const std::size_t order)
        {
            const auto found = cascade.find(declaration.name);
            if (found == cascade.end() ||
                (declaration.important != found->second.important
                     ? declaration.important
                     : specificity != found->second.specificity
                           ? specificity > found->second.specificity
                           : order >= found->second.order))
            {
                cascade[declaration.name] =
                    {declaration.value, declaration.important, specificity, order};
            }
        }

        [[nodiscard]] std::string serialize_cascade(
            const std::map<std::string, CascadeValue> &cascade)
        {
            std::string output;
            for (const auto &[name, declaration] : cascade)
            {
                if (!output.empty())
                    output.push_back(' ');
                output += name + ": " + declaration.value;
                if (declaration.important)
                    output += " !important";
                output.push_back(';');
            }
            return output;
        }

        [[nodiscard]] std::string add_or_replace_style(
            std::string tag, const std::string &style)
        {
            if (const auto attribute = find_attribute_value(tag, "style"))
            {
                tag.replace(attribute->first, attribute->second - attribute->first, style);
                return tag;
            }
            auto insertion = tag.size() - 1;
            while (insertion > 0 &&
                   std::isspace(static_cast<unsigned char>(tag[insertion - 1])))
                --insertion;
            if (insertion > 0 && tag[insertion - 1] == '/')
                --insertion;
            tag.insert(insertion, " style=\"" + style + "\"");
            return tag;
        }

        [[nodiscard]] std::vector<StyleBlock> parse_style_blocks(
            const GeneratedHtml &generated)
        {
            std::vector<StyleBlock> blocks;
            std::size_t position = 0;
            while ((position = generated.html.find('<', position)) != std::string::npos)
            {
                if (!starts_with_case_insensitive(generated.html, position, "<style") ||
                    (position + 6 < generated.html.size() &&
                     is_identifier_character(generated.html[position + 6])))
                {
                    ++position;
                    continue;
                }
                const auto open_end = find_tag_end(generated.html, position);
                if (open_end == std::string::npos)
                    break;
                auto close = open_end;
                while (close < generated.html.size() &&
                       !starts_with_case_insensitive(generated.html, close, "</style"))
                    ++close;
                if (close >= generated.html.size())
                    break;
                const auto close_end = find_tag_end(generated.html, close);
                if (close_end == std::string_view::npos)
                    break;
                StyleBlock block;
                block.start = position;
                block.body_start = open_end;
                block.end = close_end;
                StylesheetParser parser{
                    std::string_view{generated.html}.substr(open_end, close - open_end)};
                block.stylesheet = parser.parse();
                blocks.push_back(std::move(block));
                position = close_end;
            }
            return blocks;
        }

        [[nodiscard]] GeneratedHtml inline_css_impl(
            GeneratedHtml generated, std::vector<Diagnostic> *diagnostics)
        {
            const auto blocks = parse_style_blocks(generated);
            if (blocks.empty())
                return generated;

            GeneratedHtml without_styles;
            std::size_t position = 0;
            for (const auto &block : blocks)
            {
                append_slice(without_styles, generated, position, block.start);
                position = block.end;
                if (diagnostics != nullptr)
                {
                    for (const auto &issue : block.stylesheet.issues)
                    {
                        const auto absolute = block.body_start + issue.offset;
                        const auto *segment = segment_at(generated, absolute);
                        diagnostics->push_back(
                            {issue.code, Severity::error, issue.message,
                             segment == nullptr ? SourceRange{} : segment->origin});
                    }
                }
            }
            append_slice(without_styles, generated, position, generated.html.size());

            const auto bare_elements = scan_elements(without_styles.html);
            std::set<std::string> used_tags;
            std::set<std::string> used_classes;
            for (const auto &element : bare_elements)
            {
                used_tags.insert(element.tag_name);
                used_classes.insert(element.class_names.begin(), element.class_names.end());
            }

            GeneratedHtml with_retained_styles;
            position = 0;
            for (const auto &block : blocks)
            {
                append_slice(with_retained_styles, generated, position, block.start);
                const auto css = retained_css(block.stylesheet, used_tags, used_classes);
                if (!css.empty())
                    append_with_origin(with_retained_styles, generated, block.start,
                                       "<style>" + css + "</style>");
                position = block.end;
            }
            append_slice(with_retained_styles, generated, position, generated.html.size());

            std::vector<const CssRule *> ordinary_rules;
            for (const auto &block : blocks)
                for (const auto &rule : block.stylesheet.rules)
                    ordinary_rules.push_back(&rule);
            if (ordinary_rules.empty())
                return with_retained_styles;

            const auto elements = scan_elements(with_retained_styles.html);
            GeneratedHtml output;
            position = 0;
            for (const auto &element : elements)
            {
                append_slice(output, with_retained_styles, position, element.start);
                std::map<std::string, CascadeValue> cascade;
                std::size_t cascade_order = 0;
                bool matched_stylesheet = false;
                for (const auto *rule : ordinary_rules)
                {
                    int specificity = -1;
                    for (const auto &selector : rule->selectors)
                        if (selector_matches(selector, element))
                            specificity = std::max(specificity, selector.specificity);
                    for (const auto &declaration : rule->declarations)
                    {
                        if (specificity >= 0)
                        {
                            consider(cascade, declaration, specificity, cascade_order);
                            matched_stylesheet = true;
                        }
                        ++cascade_order;
                    }
                }

                auto tag = with_retained_styles.html.substr(
                    element.start, element.end - element.start);
                if (matched_stylesheet || element.has_class_attribute)
                {
                    if (const auto inline_style = find_attribute_value(tag, "style"))
                    {
                        const auto declarations = parse_declarations_detailed(
                            std::string_view{tag}.substr(
                                inline_style->first,
                                inline_style->second - inline_style->first));
                        for (const auto &declaration : declarations)
                            cascade[declaration.name] =
                                {declaration.value, declaration.important, 1000,
                                 std::numeric_limits<std::size_t>::max() / 2 +
                                     declaration.order};
                    }
                }
                if (!cascade.empty())
                    tag = add_or_replace_style(std::move(tag), serialize_cascade(cascade));

                const auto original = std::string_view{with_retained_styles.html}.substr(
                    element.start, element.end - element.start);
                if (tag == original)
                    append_slice(output, with_retained_styles, element.start, element.end);
                else
                    append_with_origin(output, with_retained_styles, element.start, tag);
                position = element.end;
            }
            append_slice(output, with_retained_styles, position,
                         with_retained_styles.html.size());
            return output;
        }

    } // namespace

    CssDeclarations parse_declarations(const std::string_view css)
    {
        CssDeclarations result;
        for (const auto &declaration : parse_declarations_detailed(css))
        {
            auto value = declaration.value;
            if (declaration.important)
                value += " !important";
            result[declaration.name] = std::move(value);
        }
        return result;
    }

    std::string serialize_declarations(const CssDeclarations &declarations)
    {
        std::string result;
        for (const auto &[name, value] : declarations)
        {
            if (!result.empty())
                result.push_back(' ');
            result += name + ": " + value + ';';
        }
        return result;
    }

    std::string apply_root_styles(std::string html, const CssDeclarations &additions)
    {
        if (additions.empty())
            return html;
        const auto open_end = find_tag_end(html, 0);
        if (open_end == std::string_view::npos)
            return html;
        auto tag = html.substr(0, open_end);
        CssDeclarations merged;
        if (const auto style = find_attribute_value(tag, "style"))
            merged = parse_declarations(
                std::string_view{tag}.substr(style->first, style->second - style->first));
        for (const auto &[name, value] : additions)
            merged[name] = value;
        return add_or_replace_style(std::move(tag), serialize_declarations(merged)) +
               html.substr(open_end);
    }

    std::string inline_css(std::string html)
    {
        GeneratedHtml generated;
        generated.append(html, {});
        return inline_css_impl(std::move(generated), nullptr).html;
    }

    GeneratedHtml inline_css(GeneratedHtml generated)
    {
        return inline_css_impl(std::move(generated), nullptr);
    }

    GeneratedHtml inline_css(GeneratedHtml generated,
                             std::vector<Diagnostic> &diagnostics)
    {
        return inline_css_impl(std::move(generated), &diagnostics);
    }

} // namespace email_markup
