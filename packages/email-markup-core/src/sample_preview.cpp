#include "email-markup/core/sample_preview.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace email_markup
{
    namespace
    {
        std::string preview_literal(const Declaration &declaration)
        {
            if (declaration.has_default) return declaration.default_expression;
            switch (declaration.value_type)
            {
            case DeclarationType::integer:
                return declaration.range_constraint
                           ? declaration.range_constraint->minimum.spelling
                           : "1";
            case DeclarationType::decimal:
            case DeclarationType::number:
                return declaration.range_constraint
                           ? declaration.range_constraint->minimum.spelling
                           : "1.0";
            case DeclarationType::boolean: return "true";
            case DeclarationType::name: return "\"Name\"";
            case DeclarationType::url: return "\"https://example.invalid/\"";
            case DeclarationType::email: return "\"name@example.invalid\"";
            case DeclarationType::color: return "\"#8e51d0\"";
            case DeclarationType::string:
            default: return "\"String\"";
            }
        }

        std::string escaped_sample(const Json &value)
        {
            const auto raw = value.is_string() ? value.get<std::string>() : value.dump();
            std::string escaped;
            escaped.reserve(raw.size());
            for (const auto character : raw)
            {
                if (character == '&') escaped += "&amp;";
                else if (character == '<') escaped += "&lt;";
                else if (character == '>') escaped += "&gt;";
                else if (character == '"') escaped += "&quot;";
                else if (character == '\'') escaped += "&#39;";
                else escaped += character;
            }
            return escaped;
        }

        std::optional<Json> sample_value(
            const Json &path, const Json &data,
            const std::unordered_map<std::string, Json> &locals)
        {
            const Json *value = &data;
            std::size_t index = 0;
            if (!path.empty())
            {
                const auto first = path.at(0).get<std::string>();
                if (const auto found = locals.find(first); found != locals.end())
                {
                    value = &found->second;
                    index = 1;
                }
            }
            for (; index < path.size(); ++index)
            {
                const auto segment = path.at(index).get<std::string>();
                if (!value->is_object() || !value->contains(segment)) return std::nullopt;
                value = &value->at(segment);
            }
            return std::optional<Json>{std::in_place, *value};
        }

        bool sample_truthy(const Json &value)
        {
            if (value.is_null()) return false;
            if (value.is_boolean()) return value.get<bool>();
            if (value.is_number()) return value.get<double>() != 0;
            if (value.is_string()) return !value.get_ref<const std::string &>().empty();
            return !value.empty();
        }

        Json sample_condition_value(
            const Json &condition, const Json &data,
            const std::unordered_map<std::string, Json> &locals)
        {
            const auto kind = condition.at("kind").get<std::string>();
            if (kind == "literal") return condition.at("value");
            if (kind == "path")
                return sample_value(condition.at("segments"), data, locals).value_or(nullptr);
            if (kind == "not")
                return !sample_truthy(sample_condition_value(condition.at("operand"), data,
                                                              locals));
            if (kind == "and" || kind == "or")
            {
                const auto left = sample_truthy(sample_condition_value(
                    condition.at("operands").at(0), data, locals));
                const auto right = sample_truthy(sample_condition_value(
                    condition.at("operands").at(1), data, locals));
                return kind == "and" ? left && right : left || right;
            }
            const auto left = sample_condition_value(condition.at("left"), data, locals);
            const auto right = sample_condition_value(condition.at("right"), data, locals);
            const auto operation = condition.at("operator").get<std::string>();
            if (operation == "==") return left == right;
            if (operation == "!=") return left != right;
            if (left.is_number() && right.is_number())
            {
                const auto a = left.get<double>();
                const auto b = right.get<double>();
                if (operation == "<") return a < b;
                if (operation == "<=") return a <= b;
                if (operation == ">") return a > b;
                if (operation == ">=") return a >= b;
            }
            return false;
        }

        void render_sample_nodes(const Json &nodes, const Json &data,
                                 std::unordered_map<std::string, Json> &locals,
                                 std::string &output, const std::size_t maximum)
        {
            for (const auto &node : nodes)
            {
                const auto kind = node.at("kind").get<std::string>();
                if (kind == "literal") output += node.at("value").get<std::string>();
                else if (kind == "runtime_value")
                {
                    const auto value = sample_value(node.at("path"), data, locals);
                    if (value)
                        output += escaped_sample(*value);
                    else
                    {
                        std::string path;
                        for (const auto &segment : node.at("path"))
                        {
                            if (!path.empty()) path += ".";
                            path += segment.get<std::string>();
                        }
                        output += escaped_sample(path);
                    }
                }
                else if (kind == "runtime_if")
                {
                    const auto matched = sample_truthy(sample_condition_value(
                        node.at("condition"), data, locals));
                    render_sample_nodes(node.at(matched ? "children" : "else_children"),
                                        data, locals, output, maximum);
                }
                else if (kind == "runtime_for")
                {
                    const auto collection = sample_value(node.at("collection"), data, locals);
                    if (collection && collection->is_array())
                    {
                        const auto count = std::min<std::size_t>(
                            collection->size(), node.at("limit").get<std::size_t>());
                        for (std::size_t index = 0; index < count; ++index)
                        {
                            locals[node.at("binding").get<std::string>()] =
                                collection->at(index);
                            render_sample_nodes(node.at("children"), data, locals,
                                                output, maximum);
                        }
                        locals.erase(node.at("binding").get<std::string>());
                    }
                }
                if (output.size() > maximum)
                    throw std::length_error("sample preview exceeds the output limit");
            }
        }
    }

    std::string definition_preview_source(const ParseResult &parsed,
                                          const std::string_view source)
    {
        if (!parsed.document.nodes.empty() || parsed.document.components.empty())
            return std::string{source};
        std::vector<std::string> names;
        for (const auto &[name, _] : parsed.document.components) names.push_back(name);
        std::sort(names.begin(), names.end());
        std::string preview{source};
        preview += "\n\n<!-- Component preview -->\n";
        for (const auto &name : names)
        {
            const auto &definition = parsed.document.components.at(name);
            preview += "@" + name;
            bool first = true;
            for (const auto &prop : definition.props)
            {
                if (prop.optional) continue;
                preview += first ? "(" : ", ";
                preview += prop.name + ": " + preview_literal(prop);
                first = false;
            }
            if (!first) preview += ")";
            std::vector<SlotDeclaration> required_slots;
            std::copy_if(definition.slots.begin(), definition.slots.end(),
                         std::back_inserter(required_slots),
                         [](const auto &slot) { return slot.required; });
            if (required_slots.empty())
            {
                preview += ";\n";
                continue;
            }
            preview += "\n";
            for (const auto &slot : required_slots)
            {
                if (slot.name == "default")
                    preview += "  Sample content\n";
                else
                    preview += "  @Slot(" + slot.name + ")Sample " + slot.name +
                               "@/Slot\n";
            }
            preview += "@/" + name + "\n";
        }
        return preview;
    }

    std::string render_emir_sample(const EmirArtifact &artifact, const Json &data,
                                   const std::size_t maximum)
    {
        std::string output;
        std::unordered_map<std::string, Json> locals;
        render_sample_nodes(artifact.value.at("document").at("children"), data,
                            locals, output, maximum);
        return output;
    }
}
