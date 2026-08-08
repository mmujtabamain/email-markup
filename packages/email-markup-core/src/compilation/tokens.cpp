#include "pipeline.hpp"

#include <functional>
#include <regex>

namespace email_markup::detail
{
    std::unordered_map<std::string, Json> resolve_tokens(
        const Registry &registry, const Json &data, std::vector<Diagnostic> &diagnostics)
    {
        std::unordered_map<std::string, Json> output;
        std::unordered_set<std::string> visiting;
        std::function<std::optional<Json>(const std::string &)> resolve =
            [&](const std::string &name) -> std::optional<Json>
        {
            if (const auto found = output.find(name); found != output.end())
                return found->second;
            const auto definition = registry.tokens.find(name);
            if (definition == registry.tokens.end())
                return std::nullopt;
            if (!visiting.insert(name).second)
            {
                diagnostics.push_back({"EM0602", Severity::error,
                                       "Design-token cycle involving “" + name + "”.",
                                       definition->second.range});
                return std::nullopt;
            }

            EvaluationContext context;
            context.data = &data;
            static const std::regex reference{R"(token\.([A-Za-z_][A-Za-z0-9_]*))"};
            const auto &expression = definition->second.expression;
            for (std::sregex_iterator it{expression.begin(), expression.end(), reference}, end;
                 it != end; ++it)
            {
                const auto dependency = (*it)[1].str();
                if (auto value = resolve(dependency))
                    context.tokens[dependency] = *value;
            }

            EvaluationResult value;
            if (expression.size() >= 2 && expression.front() == '"' &&
                expression.back() == '"' && expression.find("@{") != std::string::npos)
            {
                try
                {
                    const auto source = Json::parse(expression).get<std::string>();
                    std::string interpolated;
                    std::size_t position = 0;
                    static const std::regex embedded{R"(@\{([^{}]+)\})"};
                    bool ok = true;
                    for (std::sregex_iterator it{source.begin(), source.end(), embedded}, end;
                         it != end; ++it)
                    {
                        interpolated += source.substr(
                            position, static_cast<std::size_t>(it->position()) - position);
                        auto part = evaluate_expression((*it)[1].str(), context,
                                                        definition->second.range);
                        diagnostics.insert(diagnostics.end(), part.diagnostics.begin(),
                                           part.diagnostics.end());
                        if (!part.ok)
                        {
                            ok = false;
                            break;
                        }
                        try
                        {
                            interpolated += emit_scalar(part.value);
                        }
                        catch (...)
                        {
                            ok = false;
                            break;
                        }
                        position = static_cast<std::size_t>(it->position() + it->length());
                    }
                    interpolated += source.substr(position);
                    value = {interpolated, {}, ok};
                }
                catch (...)
                {
                    value.diagnostics.push_back({"EM0604", Severity::error,
                                                 "Invalid token string expression.",
                                                 definition->second.range});
                }
            }
            else
            {
                value = evaluate_expression(expression, context, definition->second.range);
            }

            diagnostics.insert(diagnostics.end(), value.diagnostics.begin(),
                               value.diagnostics.end());
            visiting.erase(name);
            if (!value.ok || value.value.is_array() || value.value.is_object())
            {
                if (value.ok)
                    diagnostics.push_back({"EM0603", Severity::error,
                                           "Design token “" + name + "” must be scalar.",
                                           definition->second.range});
                return std::nullopt;
            }
            output[name] = value.value;
            return value.value;
        };

        for (const auto &[name, definition] : registry.tokens)
            resolve(name);
        return output;
    }
} // namespace email_markup::detail
