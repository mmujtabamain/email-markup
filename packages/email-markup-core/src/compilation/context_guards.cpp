#include "context_guards.hpp"

#include <map>
#include <set>
#include <string>

namespace email_markup::detail
{
    namespace
    {
        struct Field
        {
            bool required{};
            bool nullable{};
            bool can_be_missing{};
        };

        using Fields = std::map<std::string, Field>;

        void flatten(const Json &fields, const std::string &prefix, bool parent_missing,
                     Fields &out)
        {
            if (!fields.is_object())
                return;
            for (auto entry = fields.begin(); entry != fields.end(); ++entry)
            {
                if (!entry.value().is_object())
                    continue;
                const auto &spec = entry.value();
                const auto path = prefix.empty() ? entry.key() : prefix + "." + entry.key();
                const bool required = spec.value("required", false);
                const bool nullable = spec.value("nullable", false);
                const bool missing = parent_missing || !required || nullable;
                out[path] = {required, nullable, missing};
                if (spec.contains("fields"))
                    flatten(spec.at("fields"), path, missing, out);
            }
        }

        std::string joined(const Json &segments)
        {
            std::string path;
            for (const auto &segment : segments)
            {
                if (!segment.is_string())
                    continue;
                if (!path.empty())
                    path += '.';
                path += segment.get<std::string>();
            }
            return path;
        }

        /// The paths a condition proves present when it has the given value.
        void proven(const Json &condition, bool holds, std::set<std::string> &present)
        {
            if (!condition.is_object() || !condition.contains("kind"))
                return;
            const auto kind = condition.at("kind").get<std::string>();
            if (kind == "path")
            {
                if (holds)
                    present.insert(joined(condition.at("segments")));
            }
            else if (kind == "not")
                proven(condition.at("operand"), !holds, present);
            else if (kind == "and" && holds)
            {
                for (const auto &operand : condition.at("operands"))
                    proven(operand, true, present);
            }
            else if (kind == "or" && !holds)
            {
                for (const auto &operand : condition.at("operands"))
                    proven(operand, false, present);
            }
        }

        bool covered(const std::string &path, const std::set<std::string> &present,
                     const Fields &fields)
        {
            if (present.contains(path))
                return true;
            const auto found = fields.find(path);
            const auto dot = path.rfind('.');
            if (dot == std::string::npos)
                return found == fields.end() || !found->second.can_be_missing;
            // Present with its parent only when the schema requires it there.
            if (found == fields.end() || !found->second.required || found->second.nullable)
                return false;
            const auto parent = path.substr(0, dot);
            const auto parent_field = fields.find(parent);
            if (parent_field == fields.end() || !parent_field->second.can_be_missing)
                return true;
            return covered(parent, present, fields);
        }

        struct Walker
        {
            const Fields &fields;
            const std::function<bool(SourceId)> &checked;
            std::vector<Diagnostic> findings;

            void walk(const Json &nodes, const std::set<std::string> &present,
                      const std::set<std::string> &bindings)
            {
                if (!nodes.is_array())
                    return;
                for (const auto &node : nodes)
                {
                    if (!node.is_object() || !node.contains("kind"))
                        continue;
                    const auto kind = node.at("kind").get<std::string>();
                    if (kind == "runtime_value")
                        value(node, present, bindings);
                    else if (kind == "runtime_if")
                    {
                        auto then_present = present;
                        proven(node.at("condition"), true, then_present);
                        walk(node.at("children"), then_present, bindings);
                        auto else_present = present;
                        proven(node.at("condition"), false, else_present);
                        walk(node.value("else_children", Json::array()), else_present, bindings);
                    }
                    else if (kind == "runtime_for")
                    {
                        auto inner = bindings;
                        inner.insert(node.value("binding", std::string{}));
                        walk(node.at("children"), present, inner);
                    }
                    else if (node.contains("children"))
                        walk(node.at("children"), present, bindings);
                }
            }

            void value(const Json &node, const std::set<std::string> &present,
                       const std::set<std::string> &bindings)
            {
                const auto &segments = node.at("path");
                if (segments.empty() || !segments.front().is_string() ||
                    bindings.contains(segments.front().get<std::string>()))
                    return;
                const auto path = joined(segments);
                const auto found = fields.find(path);
                if (found == fields.end() || !found->second.can_be_missing)
                    return;
                if (covered(path, present, fields))
                    return;
                if (!node.contains("source"))
                    return;
                const auto &span = node.at("source");
                const auto source = span.at("id").get<SourceId>();
                if (!checked(source))
                    return;
                Diagnostic diagnostic{std::string{context_guard_code}, Severity::error,
                                      path + " can be missing",
                                      {source, span.at("start").get<std::size_t>(),
                                       span.at("end").get<std::size_t>()}};
                diagnostic.json_path = path;
                findings.push_back(std::move(diagnostic));
            }
        };
    }

    std::vector<Diagnostic> check_context_guards(const Json &document,
                                                 const ContextSchema &schema,
                                                 const std::function<bool(SourceId)> &checked)
    {
        Fields fields;
        flatten(schema.fields, "", false, fields);
        Walker walker{fields, checked, {}};
        walker.walk(document, {}, {});
        return std::move(walker.findings);
    }
}
