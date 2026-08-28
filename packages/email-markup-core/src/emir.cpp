#include "email-markup/core/emir.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <functional>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "compilation/pipeline.hpp"
#include "email-markup/core/engine.hpp"
#include "email-markup/core/include.hpp"
#include "email-markup/core/types.hpp"

namespace email_markup
{
    namespace
    {
        using Json = nlohmann::json;

        bool has_error(const std::vector<Diagnostic> &diagnostics)
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(),
                               [](const auto &diagnostic)
                               { return diagnostic.severity == Severity::error; });
        }

        void issue(std::vector<Diagnostic> &diagnostics, std::string code,
                   std::string message, const SourceRange range = {})
        {
            diagnostics.push_back({std::move(code), Severity::error,
                                   std::move(message), range});
        }

        std::string trim(std::string value)
        {
            const auto keep = [](const unsigned char ch)
            { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
            value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
            return value;
        }

        std::optional<std::vector<std::string>> parse_path(const std::string_view raw)
        {
            static const std::regex path_pattern{
                R"(^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$)"};
            const auto source = trim(std::string{raw});
            if (!std::regex_match(source, path_pattern))
                return std::nullopt;
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= source.size())
            {
                const auto end = source.find('.', start);
                result.push_back(source.substr(start, end - start));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return result;
        }

        enum class ConditionTokenKind
        {
            end,
            path,
            literal,
            op,
            lparen,
            rparen
        };

        struct ConditionToken
        {
            ConditionTokenKind kind{ConditionTokenKind::end};
            std::string text;
        };

        class ConditionScanner
        {
        public:
            explicit ConditionScanner(const std::string_view source) : source_(source) {}

            ConditionToken next()
            {
                while (position_ < source_.size() &&
                       std::isspace(static_cast<unsigned char>(source_[position_])))
                    ++position_;
                if (position_ == source_.size())
                    return {};
                const auto start = position_;
                const char ch = source_[position_++];
                if (ch == '(')
                    return {ConditionTokenKind::lparen, "("};
                if (ch == ')')
                    return {ConditionTokenKind::rparen, ")"};
                if (ch == '"')
                {
                    bool escaped = false;
                    while (position_ < source_.size())
                    {
                        const auto current = source_[position_++];
                        if (current == '"' && !escaped)
                            break;
                        escaped = current == '\\' && !escaped;
                        if (current != '\\')
                            escaped = false;
                    }
                    return {ConditionTokenKind::literal,
                            std::string{source_.substr(start, position_ - start)}};
                }
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+')
                {
                    while (position_ < source_.size() &&
                           (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
                            source_[position_] == '.'))
                        ++position_;
                    return {ConditionTokenKind::literal,
                            std::string{source_.substr(start, position_ - start)}};
                }
                if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    while (position_ < source_.size() &&
                           (std::isalnum(static_cast<unsigned char>(source_[position_])) ||
                            source_[position_] == '_' || source_[position_] == '.'))
                        ++position_;
                    auto text = std::string{source_.substr(start, position_ - start)};
                    if (text == "and" || text == "or" || text == "not")
                        return {ConditionTokenKind::op, std::move(text)};
                    if (text == "true" || text == "false" || text == "null")
                        return {ConditionTokenKind::literal, std::move(text)};
                    return {ConditionTokenKind::path, std::move(text)};
                }
                if (position_ < source_.size())
                {
                    const auto pair = std::string{source_.substr(start, 2)};
                    if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=")
                    {
                        ++position_;
                        return {ConditionTokenKind::op, pair};
                    }
                }
                if (ch == '<' || ch == '>')
                    return {ConditionTokenKind::op, std::string(1, ch)};
                return {ConditionTokenKind::op, std::string(1, ch)};
            }

        private:
            std::string_view source_;
            std::size_t position_{};
        };

        class ConditionParser
        {
        public:
            explicit ConditionParser(const std::string_view source) : scanner_(source)
            {
                advance();
            }

            std::optional<Json> run()
            {
                auto value = parse_or();
                if (!value || current_.kind != ConditionTokenKind::end)
                    return std::nullopt;
                return value;
            }

        private:
            void advance() { current_ = scanner_.next(); }
            bool take(const std::string_view op)
            {
                if (current_.kind == ConditionTokenKind::op && current_.text == op)
                {
                    advance();
                    return true;
                }
                return false;
            }
            std::optional<Json> parse_or()
            {
                auto lhs = parse_and();
                while (lhs && take("or"))
                {
                    auto rhs = parse_and();
                    if (!rhs)
                        return std::nullopt;
                    lhs = Json{{"kind", "or"}, {"operands", Json::array({*lhs, *rhs})}};
                }
                return lhs;
            }
            std::optional<Json> parse_and()
            {
                auto lhs = parse_comparison();
                while (lhs && take("and"))
                {
                    auto rhs = parse_comparison();
                    if (!rhs)
                        return std::nullopt;
                    lhs = Json{{"kind", "and"}, {"operands", Json::array({*lhs, *rhs})}};
                }
                return lhs;
            }
            std::optional<Json> parse_comparison()
            {
                auto lhs = parse_unary();
                if (!lhs)
                    return std::nullopt;
                if (current_.kind == ConditionTokenKind::op &&
                    (current_.text == "==" || current_.text == "!=" ||
                     current_.text == "<" || current_.text == "<=" ||
                     current_.text == ">" || current_.text == ">="))
                {
                    const auto op = current_.text;
                    advance();
                    auto rhs = parse_unary();
                    if (!rhs)
                        return std::nullopt;
                    return Json{{"kind", "comparison"}, {"operator", op},
                                {"left", *lhs}, {"right", *rhs}};
                }
                return lhs;
            }
            std::optional<Json> parse_unary()
            {
                if (take("not"))
                {
                    auto operand = parse_unary();
                    if (!operand)
                        return std::nullopt;
                    return Json{{"kind", "not"}, {"operand", *operand}};
                }
                if (current_.kind == ConditionTokenKind::lparen)
                {
                    advance();
                    auto value = parse_or();
                    if (!value || current_.kind != ConditionTokenKind::rparen)
                        return std::nullopt;
                    advance();
                    return value;
                }
                if (current_.kind == ConditionTokenKind::path)
                {
                    const auto parsed = parse_path(current_.text);
                    if (!parsed)
                        return std::nullopt;
                    advance();
                    return Json{{"kind", "path"}, {"segments", *parsed}};
                }
                if (current_.kind == ConditionTokenKind::literal)
                {
                    const auto raw = current_.text;
                    advance();
                    Json value;
                    try
                    {
                        value = Json::parse(raw);
                    }
                    catch (...)
                    {
                        return std::nullopt;
                    }
                    if (!value.is_primitive())
                        return std::nullopt;
                    return Json{{"kind", "literal"}, {"value", std::move(value)}};
                }
                return std::nullopt;
            }

            ConditionScanner scanner_;
            ConditionToken current_;
        };

        std::string path_text(const Json &path)
        {
            std::string result;
            for (const auto &part : path)
            {
                if (!result.empty())
                    result.push_back('.');
                result += part.get<std::string>();
            }
            return result;
        }

        std::string emit_condition(const Json &condition)
        {
            const auto kind = condition.at("kind").get<std::string>();
            if (kind == "path")
                return path_text(condition.at("segments"));
            if (kind == "literal")
            {
                const auto &value = condition.at("value");
                if (value.is_boolean())
                    return value.get<bool>() ? "True" : "False";
                if (value.is_null())
                    return "None";
                return value.dump();
            }
            if (kind == "not")
                return "not (" + emit_condition(condition.at("operand")) + ")";
            if (kind == "and" || kind == "or")
            {
                const auto &operands = condition.at("operands");
                return "(" + emit_condition(operands.at(0)) + " " + kind + " " +
                       emit_condition(operands.at(1)) + ")";
            }
            if (kind == "comparison")
                return "(" + emit_condition(condition.at("left")) + " " +
                       condition.at("operator").get<std::string>() + " " +
                       emit_condition(condition.at("right")) + ")";
            throw std::invalid_argument("unknown condition node");
        }

        std::string replace_all(std::string source, const std::string_view needle,
                                const std::string_view replacement)
        {
            std::size_t position = 0;
            while ((position = source.find(needle, position)) != std::string::npos)
            {
                source.replace(position, needle.size(), replacement);
                position += replacement.size();
            }
            return source;
        }

        std::string render_nodes(const Json &nodes);

        std::string render_node(const Json &node)
        {
            const auto kind = node.at("kind").get<std::string>();
            if (kind == "literal")
                return node.at("value").get<std::string>();
            if (kind == "runtime_value")
                return "{{ " + path_text(node.at("path")) + " }}";
            if (kind == "runtime_if")
            {
                auto output = "{% if " + emit_condition(node.at("condition")) + " %}" +
                              render_nodes(node.at("children"));
                if (!node.at("else_children").empty())
                    output += "{% else %}" + render_nodes(node.at("else_children"));
                return output + "{% endif %}";
            }
            if (kind == "runtime_for")
            {
                const auto limit = node.at("limit").get<std::int64_t>();
                return "{% for " + node.at("binding").get<std::string>() + " in " +
                       path_text(node.at("collection")) + "|slice:\":" +
                       std::to_string(limit) + "\" %}" + render_nodes(node.at("children")) +
                       "{% endfor %}";
            }
            throw std::invalid_argument("unsupported EMIR node kind " + kind);
        }

        std::string render_nodes(const Json &nodes)
        {
            std::string output;
            for (const auto &node : nodes)
                output += render_node(node);
            return output;
        }

        void render_nodes_mapped(const Json &nodes, std::string &output, Json &mappings)
        {
            for (const auto &node : nodes)
            {
                const auto start = output.size();
                const auto kind = node.at("kind").get<std::string>();
                if (kind == "literal")
                    output += node.at("value").get<std::string>();
                else if (kind == "runtime_value")
                    output += "{{ " + path_text(node.at("path")) + " }}";
                else if (kind == "runtime_if")
                {
                    output += "{% if " + emit_condition(node.at("condition")) + " %}";
                    render_nodes_mapped(node.at("children"), output, mappings);
                    if (!node.at("else_children").empty())
                    {
                        output += "{% else %}";
                        render_nodes_mapped(node.at("else_children"), output, mappings);
                    }
                    output += "{% endif %}";
                }
                else if (kind == "runtime_for")
                {
                    output += "{% for " + node.at("binding").get<std::string>() + " in " +
                              path_text(node.at("collection")) + "|slice:\":" +
                              std::to_string(node.at("limit").get<std::int64_t>()) +
                              "\" %}";
                    render_nodes_mapped(node.at("children"), output, mappings);
                    output += "{% endfor %}";
                }
                if (node.contains("source") && output.size() > start)
                    mappings.push_back({{"output_start", start},
                                        {"output_end", output.size()},
                                        {"source", node.at("source")}});
            }
        }

        bool valid_path(const Json &path)
        {
            static const std::regex name{R"(^[A-Za-z_][A-Za-z0-9_]*$)"};
            if (!path.is_array() || path.empty())
                return false;
            return std::all_of(path.begin(), path.end(), [&](const auto &part)
                               { return part.is_string() &&
                                        std::regex_match(part.template get<std::string>(),
                                                         name); });
        }

        bool only_keys(const Json &value,
                       const std::initializer_list<std::string_view> allowed)
        {
            if (!value.is_object())
                return false;
            for (auto item = value.begin(); item != value.end(); ++item)
                if (std::find(allowed.begin(), allowed.end(), item.key()) ==
                    allowed.end())
                    return false;
            return true;
        }

        bool valid_condition(const Json &condition)
        {
            if (!condition.is_object() || !condition.contains("kind") ||
                !condition.at("kind").is_string())
                return false;
            const auto kind = condition.at("kind").get<std::string>();
            if (kind == "path")
                return only_keys(condition, {"kind", "segments"}) &&
                       condition.contains("segments") &&
                       valid_path(condition.at("segments"));
            if (kind == "literal")
                return only_keys(condition, {"kind", "value"}) &&
                       condition.contains("value") &&
                       condition.at("value").is_primitive();
            if (kind == "not")
                return only_keys(condition, {"kind", "operand"}) &&
                       condition.contains("operand") &&
                       valid_condition(condition.at("operand"));
            if (kind == "and" || kind == "or")
                return only_keys(condition, {"kind", "operands"}) &&
                       condition.contains("operands") &&
                       condition.at("operands").is_array() &&
                       condition.at("operands").size() == 2 &&
                       valid_condition(condition.at("operands").at(0)) &&
                       valid_condition(condition.at("operands").at(1));
            if (kind == "comparison")
                return only_keys(condition,
                                 {"kind", "operator", "left", "right"}) &&
                       condition.contains("operator") &&
                       condition.at("operator").is_string() &&
                       std::set<std::string>{"==", "!=", "<", "<=", ">", ">="}.contains(
                           condition.at("operator").get<std::string>()) &&
                       condition.contains("left") && condition.contains("right") &&
                       valid_condition(condition.at("left")) &&
                       valid_condition(condition.at("right"));
            return false;
        }

        bool valid_nodes(const Json &nodes);

        bool valid_source(const Json &source)
        {
            return only_keys(source, {"id", "start", "end"}) &&
                   source.contains("id") &&
                   source.at("id").is_number_unsigned() &&
                   source.contains("start") && source.at("start").is_number_unsigned() &&
                   source.contains("end") && source.at("end").is_number_unsigned() &&
                   source.at("start").get<std::size_t>() <=
                       source.at("end").get<std::size_t>();
        }

        bool valid_node(const Json &node)
        {
            if (!node.is_object() || !node.contains("kind") || !node.at("kind").is_string())
                return false;
            if (node.contains("source") && !valid_source(node.at("source")))
                return false;
            const auto kind = node.at("kind").get<std::string>();
            if (kind == "literal")
                return only_keys(node, {"kind", "value", "source"}) &&
                       node.contains("value") && node.at("value").is_string();
            if (kind == "runtime_value")
                return only_keys(node, {"kind", "path", "value_type", "escape",
                                        "source"}) &&
                       node.contains("path") && valid_path(node.at("path")) &&
                       node.value("value_type", std::string{}) == "string" &&
                       node.contains("escape") && node.at("escape").is_string() &&
                       std::set<std::string>{"html_text", "html_attribute", "url",
                                             "subject"}.contains(
                           node.at("escape").get<std::string>());
            if (kind == "runtime_if")
                return only_keys(node, {"kind", "condition", "children",
                                        "else_children", "source"}) &&
                       node.contains("condition") &&
                       valid_condition(node.at("condition")) &&
                       node.contains("children") && valid_nodes(node.at("children")) &&
                       node.contains("else_children") &&
                       valid_nodes(node.at("else_children"));
            if (kind == "runtime_for")
            {
                static const std::regex binding{R"(^[A-Za-z_][A-Za-z0-9_]*$)"};
                return only_keys(node, {"kind", "collection", "binding", "limit",
                                        "children", "source"}) &&
                       node.contains("collection") &&
                       valid_path(node.at("collection")) &&
                       node.contains("binding") && node.at("binding").is_string() &&
                       std::regex_match(node.at("binding").get<std::string>(), binding) &&
                       node.contains("limit") && node.at("limit").is_number_integer() &&
                       node.at("limit").get<std::int64_t>() >= 1 &&
                       node.at("limit").get<std::int64_t>() <= 100 &&
                       node.contains("children") && valid_nodes(node.at("children"));
            }
            return false;
        }

        bool valid_nodes(const Json &nodes)
        {
            return nodes.is_array() &&
                   std::all_of(nodes.begin(), nodes.end(), valid_node);
        }

        bool valid_requirements(const Json &requirements)
        {
            if (!only_keys(requirements, {"recipient"}) ||
                !requirements.contains("recipient") ||
                !requirements.at("recipient").is_object())
                return false;
            for (const auto &[path, requirement] : requirements.at("recipient").items())
            {
                if (!parse_path(path) ||
                    !only_keys(requirement, {"type", "required", "header_safe"}) ||
                    !requirement.contains("type") || !requirement.at("type").is_string() ||
                    !std::set<std::string>{"string", "url", "scalar", "array"}.contains(
                        requirement.at("type").get<std::string>()) ||
                    !requirement.contains("required") ||
                    !requirement.at("required").is_boolean() ||
                    (requirement.contains("header_safe") &&
                     !requirement.at("header_safe").is_boolean()))
                    return false;
            }
            return true;
        }

        bool valid_source_map(const Json &source_map)
        {
            if (!only_keys(source_map, {"version", "sources", "mappings"}) ||
                source_map.value("version", 0) != 1 ||
                !source_map.contains("sources") ||
                !source_map.at("sources").is_array() ||
                !source_map.contains("mappings") ||
                !source_map.at("mappings").is_array())
                return false;
            std::set<std::size_t> ids;
            for (const auto &source : source_map.at("sources"))
            {
                if (!only_keys(source, {"id", "path"}) ||
                    !source.contains("id") ||
                    !source.at("id").is_number_unsigned() ||
                    !source.contains("path") || !source.at("path").is_string() ||
                    source.at("path").get<std::string>().empty() ||
                    !ids.insert(source.at("id").get<std::size_t>()).second)
                    return false;
            }
            for (const auto &mapping : source_map.at("mappings"))
            {
                if (!only_keys(mapping,
                               {"output_start", "output_end", "source"}) ||
                    !mapping.contains("output_start") ||
                    !mapping.at("output_start").is_number_unsigned() ||
                    !mapping.contains("output_end") ||
                    !mapping.at("output_end").is_number_unsigned() ||
                    mapping.at("output_start").get<std::size_t>() >
                        mapping.at("output_end").get<std::size_t>() ||
                    !mapping.contains("source") ||
                    !valid_source(mapping.at("source")) ||
                    !ids.contains(mapping.at("source").at("id").get<std::size_t>()))
                    return false;
            }
            return true;
        }
    } // namespace

    bool EmirParseResult::ok() const noexcept
    {
        return artifact.has_value() && !has_error(diagnostics);
    }

    bool EmirEmissionResult::ok() const noexcept
    {
        return !has_error(diagnostics);
    }

    std::string canonical_emir_json(const EmirArtifact &artifact)
    {
        return artifact.value.dump(2, ' ', false, Json::error_handler_t::strict) + "\n";
    }

    EmirParseResult parse_emir(const std::string_view source)
    {
        EmirParseResult result;
        Json value;
        try
        {
            value = Json::parse(source);
        }
        catch (const std::exception &error)
        {
            issue(result.diagnostics, "EMIR0001",
                  "Invalid EMIR JSON: " + std::string{error.what()});
            return result;
        }
        if (!value.is_object() || value.value("format", std::string{}) != emir_format)
        {
            issue(result.diagnostics, "EMIR0002", "Unsupported or missing EMIR format.");
            return result;
        }
        if (value.value("version", 0) != emir_version)
        {
            issue(result.diagnostics, "EMIR0003",
                  "Unsupported EMIR version " + std::to_string(value.value("version", 0)) +
                      "; this compiler supports version 1.");
            return result;
        }
        if (!only_keys(value, {"format", "version", "output_kind", "target",
                               "document", "requirements", "source_map"}) ||
            value.value("output_kind", std::string{}) != "engine-template" ||
            !value.contains("target") ||
            !only_keys(value.at("target"), {"name", "engine"}) ||
            !value.at("target").contains("name") ||
            !value.at("target").at("name").is_string() ||
            value.at("target").at("name").get<std::string>().empty() ||
            !value.at("target").contains("engine") ||
            !value.at("target").at("engine").is_string() ||
            value.at("target").at("engine").get<std::string>().empty() ||
            !value.contains("document") ||
            !only_keys(value.at("document"), {"kind", "children"}) ||
            value.at("document").value("kind", std::string{}) != "document" ||
            !value.at("document").contains("children") ||
            !value.at("document").at("children").is_array())
        {
            issue(result.diagnostics, "EMIR0004", "EMIR document shape is invalid.");
            return result;
        }
        if (!valid_nodes(value.at("document").at("children")))
        {
            issue(result.diagnostics, "EMIR0005", "EMIR contains an invalid node.");
            return result;
        }
        if (!value.contains("requirements") ||
            !valid_requirements(value.at("requirements")) ||
            !value.contains("source_map") ||
            !valid_source_map(value.at("source_map")))
        {
            issue(result.diagnostics, "EMIR0006",
                  "EMIR requirements or source map shape is invalid.");
            return result;
        }
        result.artifact = EmirArtifact{std::move(value)};
        return result;
    }

    Json inspect_emir(const EmirArtifact &artifact)
    {
        std::unordered_map<std::string, std::size_t> counts;
        std::function<void(const Json &)> visit = [&](const Json &nodes)
        {
            for (const auto &node : nodes)
            {
                const auto kind = node.at("kind").get<std::string>();
                ++counts[kind];
                if (node.contains("children"))
                    visit(node.at("children"));
                if (node.contains("else_children"))
                    visit(node.at("else_children"));
            }
        };
        visit(artifact.value.at("document").at("children"));
        Json kinds = Json::object();
        for (const auto &[kind, count] : counts)
            kinds[kind] = count;
        return {{"format", artifact.value.at("format")},
                {"version", artifact.value.at("version")},
                {"output_kind", artifact.value.at("output_kind")},
                {"target", artifact.value.at("target")},
                {"node_counts", std::move(kinds)},
                {"requirements", artifact.value.value("requirements", Json::object())}};
    }

    EmirEmissionResult emit_emir(const EmirArtifact &artifact, const std::string_view target)
    {
        EmirEmissionResult result;
        if (target != "django")
        {
            issue(result.diagnostics, "EMIR0010",
                  "Unsupported EMIR target “" + std::string{target} + "”.");
            return result;
        }
        if (artifact.value.at("target").value("name", std::string{}) != "django")
        {
            issue(result.diagnostics, "EMIR0011",
                  "EMIR target identity does not match django.");
            return result;
        }
        try
        {
            result.output = render_nodes(artifact.value.at("document").at("children"));
        }
        catch (const std::exception &error)
        {
            issue(result.diagnostics, "EMIR0012",
                  "Cannot emit EMIR: " + std::string{error.what()});
        }
        return result;
    }

} // namespace email_markup

namespace email_markup::detail
{
    namespace
    {
        using Json = nlohmann::json;

        struct BoundCall
        {
            const EngineMacro *macro{};
            std::unordered_map<std::string, std::string> parameters;
        };

        std::string trim_copy(std::string value)
        {
            const auto keep = [](const unsigned char ch)
            { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
            value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
            return value;
        }

        std::vector<std::string> split_payload(const std::string_view payload)
        {
            std::vector<std::string> parts;
            std::string current;
            bool escaped = false;
            int interpolation_depth = 0;
            bool quoted = false;
            for (std::size_t index = 0; index < payload.size(); ++index)
            {
                const char ch = payload[index];
                if (escaped)
                {
                    if (ch == ']' || ch == ',' || ch == '\\')
                        current.push_back(ch);
                    else
                    {
                        current.push_back('\\');
                        current.push_back(ch);
                    }
                    escaped = false;
                    continue;
                }
                if (ch == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (ch == '"' && (index == 0 || payload[index - 1] != '\\'))
                    quoted = !quoted;
                if (!quoted && ch == '@' && index + 1 < payload.size() &&
                    payload[index + 1] == '{')
                {
                    ++interpolation_depth;
                    current += "@{";
                    ++index;
                    continue;
                }
                if (!quoted && interpolation_depth > 0 && ch == '}')
                    --interpolation_depth;
                if (!quoted && interpolation_depth == 0 && ch == ',')
                {
                    parts.push_back(trim_copy(std::move(current)));
                    current.clear();
                    continue;
                }
                if (ch == '@' && index + 1 < payload.size() && payload[index + 1] == '@')
                {
                    current.push_back('@');
                    ++index;
                    continue;
                }
                current.push_back(ch);
            }
            if (escaped)
                current.push_back('\\');
            parts.push_back(trim_copy(std::move(current)));
            return parts;
        }

        std::optional<std::string> decode_payload(const std::string_view payload)
        {
            std::string output;
            for (std::size_t index = 0; index < payload.size(); ++index)
            {
                const char ch = payload[index];
                if (ch != '\\')
                {
                    output.push_back(ch);
                    continue;
                }
                if (++index == payload.size())
                    return std::nullopt;
                const char escaped = payload[index];
                if (escaped == ']' || escaped == ',' || escaped == '\\')
                    output.push_back(escaped);
                else
                {
                    output.push_back('\\');
                    output.push_back(escaped);
                }
            }
            return trim_copy(std::move(output));
        }

        std::optional<BoundCall> bind_call(const DeferredCallRecord &call,
                                           const EngineDefinition &engine,
                                           std::vector<Diagnostic> &diagnostics)
        {
            const EngineMacro *macro = nullptr;
            if (call.bare)
            {
                if (engine.bare)
                    macro = &*engine.bare;
            }
            else if (const auto found = engine.macros.find(call.name);
                     found != engine.macros.end())
                macro = &found->second;
            if (!macro)
            {
                diagnostics.push_back({"EM0820", Severity::error,
                                       call.bare ? "The selected engine has no bare template."
                                                 : "Unknown deferred macro @" + call.name + ".",
                                       call.range});
                return std::nullopt;
            }
            BoundCall result;
            result.macro = macro;
            if (macro->parameters.empty())
            {
                if (!trim_copy(call.payload).empty())
                    diagnostics.push_back({"EM0821", Severity::error,
                                           "This deferred macro accepts no payload.", call.range});
            }
            else if (macro->parameters.size() == 1)
            {
                const auto decoded = decode_payload(call.payload);
                if (!decoded)
                    diagnostics.push_back({"EM0826", Severity::error,
                                           "A deferred payload cannot end with a backslash.",
                                           call.range});
                else
                    result.parameters[macro->parameters.front().name] = *decoded;
            }
            else
            {
                for (const auto &part : split_payload(call.payload))
                {
                    const auto colon = part.find(':');
                    if (colon == std::string::npos)
                    {
                        diagnostics.push_back({"EM0822", Severity::error,
                                               "Deferred macros with multiple parameters require name: value entries.",
                                               call.range});
                        continue;
                    }
                    auto name = trim_copy(part.substr(0, colon));
                    auto value = trim_copy(part.substr(colon + 1));
                    if (!result.parameters.emplace(std::move(name), std::move(value)).second)
                        diagnostics.push_back({"EM0823", Severity::error,
                                               "A deferred parameter is supplied more than once.",
                                               call.range});
                }
            }
            for (const auto &parameter : macro->parameters)
            {
                if (!result.parameters.contains(parameter.name))
                {
                    if (parameter.has_default)
                        result.parameters[parameter.name] = parameter.default_expression;
                    else if (!parameter.optional)
                        diagnostics.push_back({"EM0824", Severity::error,
                                               "Deferred macro requires parameter “" +
                                                   parameter.name + "”.",
                                               call.range});
                }
            }
            for (const auto &[name, _] : result.parameters)
                if (std::none_of(macro->parameters.begin(), macro->parameters.end(),
                                 [&](const auto &parameter)
                                 { return parameter.name == name; }))
                    diagnostics.push_back({"EM0825", Severity::error,
                                           "Unknown deferred parameter “" + name + "”.",
                                           call.range});
            for (const auto &parameter : macro->parameters)
                if (const auto found = result.parameters.find(parameter.name);
                    found != result.parameters.end())
                    (void)validate_deferred_parameter(parameter, found->second,
                                                      diagnostics, call.range);
            return result;
        }

        std::optional<std::int64_t> parse_positive_integer(const std::string &source)
        {
            std::int64_t value{};
            const auto [end, error] = std::from_chars(source.data(),
                                                      source.data() + source.size(), value);
            if (error != std::errc{} || end != source.data() + source.size() ||
                value < 1 || value > 10000)
                return std::nullopt;
            return value;
        }

        struct MarkerParser
        {
            const GeneratedHtml &generated;
            const DeferredStore &store;
            const EngineDefinition &engine;
            std::vector<Diagnostic> &diagnostics;
            std::set<std::string> requirements;
            bool subject{};
            std::size_t position{};

            Json literal_node(std::string value, const std::size_t offset) const
            {
                Json node{{"kind", "literal"}, {"value", std::move(value)}};
                const auto found = std::find_if(
                    generated.segments.begin(), generated.segments.end(),
                    [&](const auto &segment)
                    { return segment.output_start <= offset && offset < segment.output_end; });
                if (found != generated.segments.end())
                    node["source"] = {{"id", found->origin.source},
                                      {"start", found->origin.start},
                                      {"end", found->origin.end}};
                return node;
            }

            std::optional<std::tuple<char, std::size_t, std::string, std::size_t>> marker_at(
                const std::size_t at) const
            {
                if (at >= generated.html.size() || generated.html[at] != '\x1e' ||
                    generated.html.substr(at, 5) !=
                        std::string(1, '\x1e') + "EMD:")
                    return std::nullopt;
                const auto end = generated.html.find('\x1f', at + 5);
                if (end == std::string::npos)
                    return std::nullopt;
                const auto body = generated.html.substr(at + 5, end - at - 5);
                if (body.size() < 3 || body[1] != ':')
                    return std::nullopt;
                const auto colon = body.find(':', 2);
                const auto id_text = body.substr(2, colon - 2);
                std::size_t id{};
                const auto [parsed, error] = std::from_chars(
                    id_text.data(), id_text.data() + id_text.size(), id);
                if (error != std::errc{} || parsed != id_text.data() + id_text.size())
                    return std::nullopt;
                return std::tuple{body[0], id,
                                  colon == std::string::npos ? std::string{}
                                                             : body.substr(colon + 1),
                                  end + 1};
            }

            Json parse_nodes(const std::optional<std::size_t> until = std::nullopt,
                             std::string *stopped_slot = nullptr)
            {
                Json nodes = Json::array();
                while (position < generated.html.size())
                {
                    const auto marker_position = generated.html.find('\x1e', position);
                    if (marker_position == std::string::npos)
                    {
                        if (position < generated.html.size())
                            nodes.push_back(literal_node(generated.html.substr(position),
                                                         position));
                        position = generated.html.size();
                        break;
                    }
                    if (marker_position > position)
                        nodes.push_back(literal_node(
                            generated.html.substr(position, marker_position - position),
                            position));
                    const auto marker = marker_at(marker_position);
                    if (!marker)
                    {
                        position = marker_position + 1;
                        continue;
                    }
                    const auto [kind, id, slot, end] = *marker;
                    position = end;
                    if (until && id == *until && (kind == 'E' || kind == 'S'))
                    {
                        if (stopped_slot && kind == 'S')
                            *stopped_slot = slot;
                        return nodes;
                    }
                    if (kind != 'B' || id >= store.calls.size())
                    {
                        diagnostics.push_back({"EM0830", Severity::error,
                                               "Deferred marker stream is invalid.", {}});
                        continue;
                    }
                    nodes.push_back(parse_call(store.calls[id]));
                }
                return nodes;
            }

            Json parse_call(const DeferredCallRecord &call)
            {
                std::unordered_map<std::string, Json> slots;
                std::string next_slot;
                auto content = parse_nodes(call.id, &next_slot);
                if (!content.empty())
                    slots["default"] = std::move(content);
                while (!next_slot.empty())
                {
                    const auto name = std::move(next_slot);
                    next_slot.clear();
                    auto part = parse_nodes(call.id, &next_slot);
                    auto &slot_nodes = slots[name];
                    if (slot_nodes.is_null())
                        slot_nodes = Json::array();
                    for (auto &node : part)
                        slot_nodes.push_back(std::move(node));
                }

                const auto bound = bind_call(call, engine, diagnostics);
                if (!bound)
                    return {{"kind", "literal"}, {"value", ""}};
                const auto &macro = *bound->macro;
                for (const auto &slot : macro.slots)
                {
                    if (slot.required && !slots.contains(slot.name))
                        diagnostics.push_back({"EM0831", Severity::error,
                                               "Deferred macro requires slot “" + slot.name + "”.",
                                               call.range});
                }
                for (const auto &[name, _] : slots)
                    if (std::none_of(macro.slots.begin(), macro.slots.end(),
                                     [&](const auto &slot)
                                     { return slot.name == name; }))
                        diagnostics.push_back({"EM0832", Severity::error,
                                               "Deferred macro has no slot “" + name + "”.",
                                               call.range});

                if (call.bare && macro.parameters.size() == 1 &&
                    macro.parameters.front().value_type == DeclarationType::path)
                {
                    const auto parsed = parse_path(bound->parameters.at(
                        macro.parameters.front().name));
                    if (!parsed)
                    {
                        diagnostics.push_back({"EM0833", Severity::error,
                                               "Deferred value requires a dotted recipient path.",
                                               call.range});
                        return {{"kind", "literal"}, {"value", ""}};
                    }
                    const auto requirement = [&]
                    {
                        std::string value;
                        for (const auto &part : *parsed)
                        {
                            if (!value.empty())
                                value.push_back('.');
                            value += part;
                        }
                        return value;
                    }();
                    return {{"kind", "runtime_value"},
                            {"path", *parsed},
                            {"value_type", "string"},
                            {"escape", call.escape},
                            {"source", {{"id", call.range.source},
                                        {"start", call.range.start},
                                        {"end", call.range.end}}}};
                }

                if (call.name == "If")
                {
                    const auto parameter = std::find_if(
                        macro.parameters.begin(), macro.parameters.end(),
                        [](const auto &item)
                        { return item.value_type == DeclarationType::condition; });
                    if (parameter != macro.parameters.end())
                    {
                        const auto condition = ConditionParser{
                            bound->parameters.at(parameter->name)}.run();
                        if (!condition)
                        {
                            diagnostics.push_back({"EM0834", Severity::error,
                                                   "Deferred If condition uses unsupported syntax.",
                                                   call.range});
                            return {{"kind", "literal"}, {"value", ""}};
                        }
                        return {{"kind", "runtime_if"},
                                {"condition", *condition},
                                {"children", slots.contains("default")
                                                 ? slots["default"]
                                                 : Json::array()},
                                {"else_children", slots.contains("else")
                                                      ? slots["else"]
                                                      : Json::array()},
                                {"source", {{"id", call.range.source},
                                            {"start", call.range.start},
                                            {"end", call.range.end}}}};
                    }
                }

                if (call.name == "For")
                {
                    const Declaration *path_parameter = nullptr;
                    const Declaration *name_parameter = nullptr;
                    const Declaration *limit_parameter = nullptr;
                    for (const auto &parameter : macro.parameters)
                    {
                        if (parameter.value_type == DeclarationType::path)
                            path_parameter = &parameter;
                        else if (parameter.value_type == DeclarationType::name)
                            name_parameter = &parameter;
                        else if (parameter.value_type == DeclarationType::integer)
                            limit_parameter = &parameter;
                    }
                    const auto path = path_parameter
                                          ? parse_path(bound->parameters.at(path_parameter->name))
                                          : std::nullopt;
                    const auto binding = name_parameter
                                             ? bound->parameters.at(name_parameter->name)
                                             : std::string{};
                    static const std::regex name_pattern{R"(^[A-Za-z_][A-Za-z0-9_]*$)"};
                    const auto limit = limit_parameter
                                           ? parse_positive_integer(bound->parameters.at(
                                                 limit_parameter->name))
                                           : std::optional<std::int64_t>{100};
                    if (!path || !std::regex_match(binding, name_pattern) || !limit)
                    {
                        diagnostics.push_back({"EM0835", Severity::error,
                                               "Deferred For requires a collection path, local binding name, and limit from 1 to 100.",
                                               call.range});
                        return {{"kind", "literal"}, {"value", ""}};
                    }
                    return {{"kind", "runtime_for"},
                            {"collection", *path},
                            {"binding", binding},
                            {"limit", *limit},
                            {"children", slots.contains("default")
                                             ? slots["default"]
                                             : Json::array()},
                            {"source", {{"id", call.range.source},
                                        {"start", call.range.start},
                                        {"end", call.range.end}}}};
                }

                diagnostics.push_back({"EM0836", Severity::error,
                                       "The restricted Django target rejects raw deferred macros.",
                                       call.range});
                return {{"kind", "literal"}, {"value", ""}};
            }
        };

        std::string joined_path(const Json &path)
        {
            std::string output;
            for (const auto &part : path)
            {
                if (!output.empty())
                    output.push_back('.');
                output += part.get<std::string>();
            }
            return output;
        }

        void collect_condition_requirements(const Json &condition,
                                            const std::set<std::string> &locals,
                                            Json &recipient)
        {
            const auto kind = condition.at("kind").get<std::string>();
            if (kind == "path")
            {
                const auto &path = condition.at("segments");
                if (!path.empty() && !locals.contains(path.at(0).get<std::string>()))
                    recipient[joined_path(path)] = {{"type", "scalar"},
                                                   {"required", true}};
            }
            else if (kind == "not")
                collect_condition_requirements(condition.at("operand"), locals,
                                               recipient);
            else if (kind == "and" || kind == "or")
                for (const auto &operand : condition.at("operands"))
                    collect_condition_requirements(operand, locals, recipient);
            else if (kind == "comparison")
            {
                collect_condition_requirements(condition.at("left"), locals, recipient);
                collect_condition_requirements(condition.at("right"), locals, recipient);
            }
        }

        void collect_requirements(const Json &nodes, const std::set<std::string> &locals,
                                  Json &recipient, const bool subject)
        {
            for (const auto &node : nodes)
            {
                const auto kind = node.at("kind").get<std::string>();
                if (kind == "runtime_value")
                {
                    const auto &path = node.at("path");
                    if (!path.empty() &&
                        !locals.contains(path.at(0).get<std::string>()))
                        recipient[joined_path(path)] = {{"type", node.at("escape") == "url"
                                                                    ? "url"
                                                                    : "string"},
                                                       {"required", true},
                                                       {"header_safe", subject}};
                }
                else if (kind == "runtime_if")
                {
                    collect_condition_requirements(node.at("condition"), locals,
                                                   recipient);
                    collect_requirements(node.at("children"), locals, recipient, subject);
                    collect_requirements(node.at("else_children"), locals, recipient,
                                         subject);
                }
                else if (kind == "runtime_for")
                {
                    const auto &path = node.at("collection");
                    if (!locals.contains(path.at(0).get<std::string>()))
                        recipient[joined_path(path)] = {{"type", "array"},
                                                       {"required", true}};
                    auto nested = locals;
                    nested.insert(node.at("binding").get<std::string>());
                    collect_requirements(node.at("children"), nested, recipient, subject);
                }
            }
        }
    } // namespace

    std::optional<EmirArtifact> lower_deferred_to_emir(
        GeneratedHtml &generated, const DeferredStore &deferred,
        const EngineDefinition &engine, const SourceManager &sources,
        const std::filesystem::path &entry_path,
        std::vector<Diagnostic> &diagnostics, const bool subject)
    {
        MarkerParser parser{generated, deferred, engine, diagnostics, {}, subject};
        auto nodes = parser.parse_nodes();
        if (has_error(diagnostics))
            return std::nullopt;
        Json recipient = Json::object();
        collect_requirements(nodes, {}, recipient, subject);
        auto engine_identity = portable_path_string(engine.canonical_path);
        const auto packaged = engine_identity.rfind("/engines/");
        if (packaged != std::string::npos && engine.name == "django")
            engine_identity = "${EMAIL_MARKUP_LIB}" + engine_identity.substr(packaged);
        Json source_files = Json::array();
        const auto entry_parent = entry_path.lexically_normal().parent_path();
        for (SourceId id = 0; id < sources.size(); ++id)
        {
            const auto &file = sources.get(id);
            auto path = file.path.lexically_normal();
            auto portable = portable_path_string(path);
            const auto library = portable.rfind("/lib/");
            if (library != std::string::npos)
                portable = "${EMAIL_MARKUP_LIB}" + portable.substr(library + 4);
            else
            {
                const auto relative = path.lexically_relative(entry_parent);
                const auto relative_text = portable_path_string(relative);
                if (!relative_text.empty() && relative_text != "." &&
                    !relative_text.starts_with("../"))
                    portable = relative_text;
                else if (path.is_absolute())
                    portable = "${EXTERNAL}/" + path.filename().string();
            }
            source_files.push_back({{"id", id}, {"path", std::move(portable)}});
        }
        EmirArtifact artifact{{{"format", emir_format},
                               {"version", emir_version},
                               {"output_kind", "engine-template"},
                               {"target", {{"name", engine.name},
                                           {"engine", std::move(engine_identity)}}},
                               {"document", {{"kind", "document"},
                                             {"children", std::move(nodes)}}},
                               {"requirements", {{"recipient", std::move(recipient)}}},
                               {"source_map", {{"version", 1},
                                               {"sources", std::move(source_files)},
                                               {"mappings", Json::array()}}}}};
        std::string emitted;
        auto &mappings = artifact.value["source_map"]["mappings"];
        render_nodes_mapped(artifact.value["document"]["children"], emitted, mappings);
        std::sort(mappings.begin(), mappings.end(), [](const auto &left, const auto &right)
                  {
                      if (left.at("output_start") != right.at("output_start"))
                          return left.at("output_start") < right.at("output_start");
                      return left.at("output_end") < right.at("output_end");
                  });
        generated.html = std::move(emitted);
        generated.segments.clear();
        return artifact;
    }
} // namespace email_markup::detail
