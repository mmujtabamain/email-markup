#include "email-markup/core/parser.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "email-markup/core/lexer.hpp"
#include "email-markup/core/types.hpp"

namespace email_markup
{
    namespace
    {

        const std::unordered_set<std::string> block_keywords{
            "DefineComponent", "DefineStyle", "Props", "Slots", "Template", "Media",
            "If", "For"};
        const std::unordered_set<std::string> reserved_keywords{
            "DefineComponent", "DefineStyle", "DefineToken", "Props", "Slots", "Template",
            "Media", "If", "Else", "For", "Slot", "Include"};

        std::string trim(std::string value)
        {
            const auto keep = [](const unsigned char ch)
            { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
            value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
            return value;
        }

        std::string normalize(std::string text)
        {
            static const std::regex newline{R"([ \t]*\r?\n[ \t\r\n]*)"};
            return std::regex_replace(text, newline, " ");
        }

        std::vector<Parameter> parameters(const Token &token, std::vector<Diagnostic> &diagnostics)
        {
            std::vector<Parameter> result;
            std::size_t start = 0;
            bool quoted = false;
            bool escaped = false;
            int depth = 0;
            const auto commit = [&](const std::size_t end)
            {
                auto part = trim(token.parameters.substr(start, end - start));
                if (part.empty())
                    return;
                bool in_string = false;
                std::size_t colon = std::string::npos;
                int nested = 0;
                for (std::size_t i = 0; i < part.size(); ++i)
                {
                    if (part[i] == '"' && (i == 0 || part[i - 1] != '\\'))
                        in_string = !in_string;
                    if (!in_string && part[i] == '(')
                        ++nested;
                    if (!in_string && part[i] == ')')
                        --nested;
                    if (!in_string && nested == 0 && part[i] == ':')
                    {
                        colon = i;
                        break;
                    }
                }
                if (colon == std::string::npos)
                {
                    diagnostics.push_back({"EM0201", Severity::error,
                                           "Component arguments must be named: name: value.",
                                           token.range});
                    return;
                }
                auto name = trim(part.substr(0, colon));
                auto expression = trim(part.substr(colon + 1));
                static const std::regex name_pattern{R"(^[A-Za-z_][A-Za-z0-9_]*$)"};
                if (!std::regex_match(name, name_pattern) || expression.empty())
                {
                    diagnostics.push_back({"EM0202", Severity::error,
                                           "Invalid named component argument.", token.range});
                    return;
                }
                if (std::any_of(result.begin(), result.end(), [&](const auto &item)
                                { return item.name == name; }))
                {
                    diagnostics.push_back({"EM0203", Severity::error,
                                           "Argument “" + name + "” is set more than once.", token.range});
                    return;
                }
                result.push_back({std::move(name), std::move(expression), token.range});
            };
            for (std::size_t i = 0; i < token.parameters.size(); ++i)
            {
                const char ch = token.parameters[i];
                if (quoted)
                {
                    if (ch == '"' && !escaped)
                        quoted = false;
                    escaped = ch == '\\' && !escaped;
                    if (ch != '\\')
                        escaped = false;
                    continue;
                }
                if (ch == '"')
                    quoted = true;
                else if (ch == '(')
                    ++depth;
                else if (ch == ')')
                    --depth;
                else if (depth == 0 && (ch == ',' || ch == '\n'))
                {
                    commit(i);
                    start = i + 1;
                }
            }
            commit(token.parameters.size());
            return result;
        }

        std::optional<std::string> literal_string(const std::string &expression)
        {
            try
            {
                auto value = nlohmann::json::parse(expression);
                if (value.is_string())
                    return value.get<std::string>();
            }
            catch (...)
            {
            }
            return std::nullopt;
        }

        class Parser
        {
        public:
            Parser(const SourceId source, const std::string_view text,
                   const std::size_t diagnostic_limit)
                : source_(source), diagnostic_limit_(diagnostic_limit)
            {
                auto lexed = lex(source, text, diagnostic_limit);
                tokens_ = std::move(lexed.tokens);
                diagnostics_ = std::move(lexed.diagnostics);
                document_.source = source;
            }

            ParseResult run()
            {
                document_.nodes = nodes({}, true);
                while (peek().kind != TokenKind::end)
                {
                    error("EM0204", "Unexpected closing tag @/" + peek().name + ".", peek().range);
                    advance();
                }
                return {std::move(document_), std::move(diagnostics_)};
            }

        private:
            const Token &peek() const { return tokens_[index_]; }
            Token advance() { return tokens_[index_++]; }
            void error(std::string code, std::string message, const SourceRange range)
            {
                if (diagnostics_.size() < diagnostic_limit_)
                {
                    diagnostics_.push_back({std::move(code), Severity::error,
                                            std::move(message), range});
                }
            }
            bool close(const std::string &name, const SourceRange opened)
            {
                if (peek().kind == TokenKind::close && peek().name == name)
                {
                    advance();
                    return true;
                }
                error("EM0205", "@" + name + " is not closed; expected @/" + name + ".",
                      opened);
                while (peek().kind != TokenKind::end)
                {
                    if (peek().kind == TokenKind::close && peek().name == name)
                    {
                        advance();
                        return false;
                    }
                    if (peek().kind == TokenKind::open || peek().kind == TokenKind::self_closing)
                        break;
                    advance();
                }
                return false;
            }

            std::vector<NodePtr> nodes(const std::unordered_set<std::string> &stops,
                                       const bool top_level = false)
            {
                std::vector<NodePtr> result;
                while (peek().kind != TokenKind::end && peek().kind != TokenKind::close &&
                       !(stops.contains(peek().name) &&
                         (peek().kind == TokenKind::open ||
                          peek().kind == TokenKind::self_closing)))
                {
                    auto token = peek();
                    if (token.kind == TokenKind::text)
                    {
                        advance();
                        auto text = normalize(token.text);
                        if (text.find_first_not_of(" \t\r\n") != std::string::npos)
                            result.push_back(
                                std::make_shared<Node>(Node{token.range, TextNode{std::move(text)}}));
                        continue;
                    }
                    if (token.kind == TokenKind::expression)
                    {
                        advance();
                        result.push_back(std::make_shared<Node>(
                            Node{token.range, ExpressionNode{trim(token.text)}}));
                        continue;
                    }
                    if (token.kind == TokenKind::invalid)
                    {
                        advance();
                        continue;
                    }
                    auto parsed = tag(top_level);
                    if (parsed)
                        result.push_back(std::move(parsed));
                }
                if (!result.empty())
                {
                    if (auto *text = std::get_if<TextNode>(&result.front()->value))
                    {
                        text->text.erase(text->text.begin(),
                                         std::find_if(text->text.begin(), text->text.end(), [](unsigned char ch)
                                                      { return !std::isspace(ch); }));
                    }
                    if (auto *text = std::get_if<TextNode>(&result.back()->value))
                    {
                        text->text.erase(std::find_if(text->text.rbegin(), text->text.rend(),
                                                      [](unsigned char ch)
                                                      { return !std::isspace(ch); })
                                             .base(),
                                         text->text.end());
                    }
                }
                return result;
            }

            NodePtr tag(const bool top_level)
            {
                auto token = advance();
                if (block_keywords.contains(token.name) && token.kind == TokenKind::self_closing)
                {
                    error("EM0210", "@" + token.name + " requires a body.", token.range);
                    return {};
                }
                if (token.name == "If")
                    return parse_if(token);
                if (token.name == "For")
                    return parse_for(token);
                if (token.name == "Slot")
                    return parse_slot(token);
                if (token.name == "Include")
                    return parse_include(token, top_level);
                if (token.name == "DefineComponent")
                {
                    parse_component_definition(token, top_level);
                    return {};
                }
                if (token.name == "DefineStyle")
                {
                    parse_style_definition(token, top_level);
                    return {};
                }
                if (token.name == "DefineToken")
                {
                    parse_token_definition(token, top_level);
                    return {};
                }
                if (token.name == "Media")
                {
                    parse_media(token, top_level);
                    return {};
                }
                if (reserved_keywords.contains(token.name))
                {
                    error("EM0211", "@" + token.name + " is not valid in this context.", token.range);
                    return {};
                }
                auto args = parameters(token, diagnostics_);
                std::vector<NodePtr> children;
                if (token.kind != TokenKind::self_closing)
                {
                    children = nodes({});
                    close(token.name, token.range);
                }
                return std::make_shared<Node>(Node{token.range, ComponentNode{
                                                                    token.name, std::move(args), std::move(children),
                                                                    token.kind == TokenKind::self_closing}});
            }

            NodePtr parse_if(const Token &token)
            {
                if (trim(token.parameters).empty())
                    error("EM0220", "@If requires a condition.", token.range);
                auto then_nodes = nodes({"Else"});
                std::vector<NodePtr> else_nodes;
                if ((peek().kind == TokenKind::open || peek().kind == TokenKind::self_closing) &&
                    peek().name == "Else")
                {
                    advance();
                    else_nodes = nodes({});
                }
                close("If", token.range);
                return std::make_shared<Node>(Node{token.range, IfNode{
                                                                    trim(token.parameters), std::move(then_nodes), std::move(else_nodes)}});
            }

            NodePtr parse_for(const Token &token)
            {
                static const std::regex pattern{
                    R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s+in\s+(.+?)\s*$)"};
                std::smatch match;
                if (!std::regex_match(token.parameters, match, pattern))
                {
                    error("EM0221", "@For requires: name in expression.", token.range);
                }
                auto body = nodes({});
                close("For", token.range);
                return std::make_shared<Node>(Node{token.range, ForNode{
                                                                    match.empty() ? std::string{} : match[1].str(),
                                                                    match.empty() ? std::string{"null"} : match[2].str(), std::move(body)}});
            }

            NodePtr parse_slot(const Token &token)
            {
                auto name = trim(token.parameters);
                if (name.empty())
                    name = "default";
                if (token.kind == TokenKind::self_closing)
                {
                    return std::make_shared<Node>(Node{token.range, SlotNode{name, {}, true}});
                }
                auto body = nodes({});
                close("Slot", token.range);
                return std::make_shared<Node>(
                    Node{token.range, SlotNode{name, std::move(body), false}});
            }

            NodePtr parse_include(const Token &token, const bool top_level)
            {
                if (!top_level || token.kind != TokenKind::self_closing)
                {
                    error("EM0222", "@Include is a top-level void construct.", token.range);
                }
                return std::make_shared<Node>(
                    Node{token.range, IncludeNode{trim(token.parameters)}});
            }

            std::string definition_name(const Token &token)
            {
                const auto args = parameters(token, diagnostics_);
                const auto found = std::find_if(args.begin(), args.end(), [](const auto &arg)
                                                { return arg.name == "name"; });
                if (found == args.end())
                {
                    error("EM0230", "Definition requires name: \"CapitalizedName\".", token.range);
                    return {};
                }
                const auto value = literal_string(found->expression);
                if (!value)
                {
                    error("EM0231", "Definition name must be a string literal.", found->range);
                    return {};
                }
                return *value;
            }

            std::string raw_body(const std::string &name, const Token &opened)
            {
                std::string raw;
                if (peek().kind == TokenKind::text)
                    raw = advance().text;
                close(name, opened.range);
                return raw;
            }

            void parse_component_definition(const Token &token, const bool top_level)
            {
                if (!top_level)
                    error("EM0232", "@DefineComponent is top-level only.", token.range);
                ComponentDefinition definition;
                definition.name = definition_name(token);
                definition.range = token.range;
                static const std::regex component_name{R"(^[A-Z][A-Za-z0-9_]*$)"};
                if (!std::regex_match(definition.name, component_name) ||
                    reserved_keywords.contains(definition.name))
                {
                    error("EM0233", "Invalid or reserved component name “" + definition.name + "”.",
                          token.range);
                }
                bool saw_template = false;
                while (peek().kind != TokenKind::end &&
                       !(peek().kind == TokenKind::close && peek().name == "DefineComponent"))
                {
                    if (peek().kind == TokenKind::text && trim(peek().text).empty())
                    {
                        advance();
                        continue;
                    }
                    if (peek().kind != TokenKind::open)
                    {
                        error("EM0234", "A component definition contains only @Props, @Slots, and @Template.",
                              peek().range);
                        advance();
                        continue;
                    }
                    const auto inner = advance();
                    if (inner.name == "Props")
                    {
                        const auto raw = raw_body("Props", inner);
                        definition.props = parse_prop_declarations(raw,
                                                                   {source_, inner.range.end, inner.range.end + raw.size()}, diagnostics_);
                    }
                    else if (inner.name == "Slots")
                    {
                        const auto raw = raw_body("Slots", inner);
                        definition.slots = parse_slot_declarations(raw,
                                                                   {source_, inner.range.end, inner.range.end + raw.size()}, diagnostics_);
                    }
                    else if (inner.name == "Template")
                    {
                        definition.body = nodes({});
                        close("Template", inner.range);
                        saw_template = true;
                    }
                    else
                    {
                        error("EM0235", "@" + inner.name + " is not valid in a component definition.",
                              inner.range);
                        nodes({});
                        close(inner.name, inner.range);
                    }
                }
                close("DefineComponent", token.range);
                if (!saw_template)
                    error("EM0236", "Component “" + definition.name + "” requires @Template.", token.range);
                if (!definition.name.empty())
                    document_.components[definition.name] = std::move(definition);
            }

            void parse_style_definition(const Token &token, const bool top_level)
            {
                if (!top_level)
                    error("EM0240", "@DefineStyle is top-level only.", token.range);
                const auto name = definition_name(token);
                const auto body = raw_body("DefineStyle", token);
                static const std::regex interpolation{R"(@\{[^{}]*\})"};
                const auto structural = std::regex_replace(body, interpolation, "");
                if (structural.find('{') != std::string::npos ||
                    structural.find('}') != std::string::npos)
                {
                    error("EM0241", "A style body contains declarations, not selectors or braces.",
                          token.range);
                }
                if (!name.empty())
                    document_.styles[name] = {name, body, token.range};
            }

            void parse_token_definition(const Token &token, const bool top_level)
            {
                if (!top_level || token.kind != TokenKind::self_closing)
                {
                    error("EM0242", "@DefineToken is a top-level void construct.", token.range);
                }
                const auto args = parameters(token, diagnostics_);
                std::string name;
                std::string value;
                for (const auto &arg : args)
                {
                    if (arg.name == "name")
                    {
                        if (auto literal = literal_string(arg.expression))
                            name = *literal;
                    }
                    else if (arg.name == "value")
                        value = arg.expression;
                }
                if (name.empty() || value.empty())
                {
                    error("EM0243", "@DefineToken requires string name and value arguments.", token.range);
                }
                else
                    document_.tokens[name] = {name, value, token.range};
            }

            void parse_media(const Token &token, const bool top_level)
            {
                if (!top_level)
                    error("EM0250", "@Media is top-level only.", token.range);
                const auto query = literal_string(trim(token.parameters));
                if (!query)
                    error("EM0251", "@Media requires a quoted media query.", token.range);
                document_.media.push_back({query.value_or(""), raw_body("Media", token), token.range});
            }

            SourceId source_;
            std::size_t diagnostic_limit_;
            std::vector<Token> tokens_;
            std::size_t index_{};
            Document document_;
            std::vector<Diagnostic> diagnostics_;
        };

    } // namespace

    ParseResult parse(const SourceId source, const std::string_view text,
                      const std::size_t diagnostic_limit)
    {
        return Parser{source, text, diagnostic_limit}.run();
    }

} // namespace email_markup
