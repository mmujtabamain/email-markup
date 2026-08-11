#include "compilation/pipeline.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <regex>
#include <unordered_set>

#include "email-markup/core/styles.hpp"
#include "email-markup/core/types.hpp"

namespace email_markup::detail
{
    std::size_t DeferredStore::add(const DeferredCallNode &call, const SourceRange range,
                                   std::string escape)
    {
        const auto id = calls.size();
        calls.push_back({id, call.name, call.payload, range, std::move(escape), call.bare,
                         call.self_closing});
        return id;
    }

    std::string DeferredStore::marker(const char kind, const std::size_t id,
                                      const std::string_view slot)
    {
        auto result = std::string(1, '\x1e') + "EMD:" + kind + ":" +
                      std::to_string(id);
        if (!slot.empty())
            result += ":" + std::string{slot};
        result.push_back('\x1f');
        return result;
    }

    namespace
    {
        std::string html_escape(const std::string_view text)
        {
            std::string output;
            output.reserve(text.size());
            for (const char character : text)
            {
                switch (character)
                {
                case '&':
                    output += "&amp;";
                    break;
                case '<':
                    output += "&lt;";
                    break;
                case '>':
                    output += "&gt;";
                    break;
                case '"':
                    output += "&quot;";
                    break;
                case '\'':
                    output += "&#39;";
                    break;
                default:
                    output.push_back(character);
                }
            }
            return output;
        }

        struct SlotContent
        {
            std::vector<NodePtr> nodes;
            EvaluationContext context;
        };

        struct RenderScope
        {
            EvaluationContext evaluation;
            std::unordered_map<std::string, SlotContent> slots;
        };

        class Renderer final
        {
        public:
            Renderer(const Registry &registry, const CompilationLimits &limits,
                     const CancellationToken cancellation,
                     std::vector<Diagnostic> &diagnostics,
                     DeferredStore *deferred, const bool subject)
                : registry_(registry), limits_(limits), cancellation_(cancellation),
                  diagnostics_(diagnostics), deferred_(deferred), subject_(subject)
            {
            }

            [[nodiscard]] GeneratedHtml render(
                const std::vector<NodePtr> &nodes, const Json &data,
                const std::unordered_map<std::string, Json> &tokens)
            {
                RenderScope scope;
                scope.evaluation.data = &data;
                scope.evaluation.tokens = tokens;
                render_nodes(nodes, scope, 0);
                return std::move(output_);
            }

            [[nodiscard]] GeneratedHtml render_shell(
                const std::vector<NodePtr> &nodes, const Json &data,
                const std::unordered_map<std::string, Json> &tokens,
                const GeneratedHtml &content)
            {
                RenderScope scope;
                scope.evaluation.data = &data;
                scope.evaluation.tokens = tokens;
                shell_content_ = &content;
                render_nodes(nodes, scope, 0);
                return std::move(output_);
            }

        private:
            void diagnostic(std::string code, std::string message, const SourceRange range,
                            const Severity severity = Severity::error,
                            std::vector<RelatedLocation> related = {})
            {
                if (diagnostics_.size() < limits_.maximum_diagnostics)
                    diagnostics_.push_back({std::move(code), severity, std::move(message),
                                            range, std::move(related)});
            }

            [[nodiscard]] std::optional<Json> evaluate(
                const std::string &expression, const EvaluationContext &context,
                const SourceRange range)
            {
                if (expression.size() >= 2 && expression.front() == '"' &&
                    expression.back() == '"' &&
                    expression.find("@{") != std::string::npos)
                {
                    Json parsed;
                    try
                    {
                        parsed = Json::parse(expression);
                    }
                    catch (...)
                    {
                        diagnostic("EM0701", "Invalid interpolated string literal.", range);
                        return std::nullopt;
                    }
                    const auto source = parsed.get<std::string>();
                    static const std::regex embedded{R"(@\{([^{}]+)\})"};
                    std::string result;
                    std::size_t position = 0;
                    for (std::sregex_iterator it{source.begin(), source.end(), embedded}, end;
                         it != end; ++it)
                    {
                        result += source.substr(
                            position, static_cast<std::size_t>(it->position()) - position);
                        auto part = evaluate_expression((*it)[1].str(), context, range);
                        diagnostics_.insert(diagnostics_.end(), part.diagnostics.begin(),
                                            part.diagnostics.end());
                        if (!part.ok)
                            return std::nullopt;
                        try
                        {
                            result += emit_scalar(part.value);
                        }
                        catch (...)
                        {
                            diagnostic("EM0702",
                                       "String interpolation requires a scalar value.", range);
                            return std::nullopt;
                        }
                        position = static_cast<std::size_t>(it->position() + it->length());
                    }
                    result += source.substr(position);
                    return Json{result};
                }

                auto result = evaluate_expression(expression, context, range);
                diagnostics_.insert(diagnostics_.end(), result.diagnostics.begin(),
                                    result.diagnostics.end());
                return result.ok ? std::optional<Json>{std::move(result.value)} : std::nullopt;
            }

            void append(const std::string_view text, const SourceRange range)
            {
                if (output_.html.size() + text.size() > limits_.maximum_html_bytes)
                {
                    if (!output_limited_)
                    {
                        diagnostic("EM0901",
                                   "Generated HTML exceeds the configured byte limit.", range);
                        output_limited_ = true;
                    }
                    return;
                }
                output_.append(text, range, stack_);
            }

            void render_nodes(const std::vector<NodePtr> &nodes, RenderScope &scope,
                              const std::size_t depth)
            {
                for (const auto &node : nodes)
                {
                    if (cancellation_.is_cancelled())
                    {
                        diagnostic("EM0902", "Compilation cancelled.", node->range);
                        return;
                    }
                    if (output_limited_)
                        return;
                    std::visit(
                        [&](const auto &value)
                        { render_node(value, *node, scope, depth); },
                        node->value);
                }
            }

            void render_node(const TextNode &text, const Node &node, RenderScope &,
                             std::size_t)
            {
                append(text.text, node.range);
            }

            void render_node(const ExpressionNode &expression, const Node &node,
                             RenderScope &scope, std::size_t)
            {
                auto value = evaluate(expression.expression, scope.evaluation, node.range);
                if (!value)
                    return;
                try
                {
                    append(subject_ ? emit_scalar(*value)
                                    : html_escape(emit_scalar(*value)),
                           node.range);
                }
                catch (...)
                {
                    diagnostic("EM0703",
                               "Interpolation cannot emit null, an array, or an object directly.",
                               node.range);
                }
            }

            void render_node(const IncludeNode &, const Node &, RenderScope &, std::size_t)
            {
            }

            void render_node(const EngineNode &, const Node &, RenderScope &, std::size_t)
            {
            }

            void render_node(const DeferredCallNode &call, const Node &node,
                             RenderScope &scope, const std::size_t depth)
            {
                if (!deferred_)
                {
                    diagnostic("EM0801",
                               "Deferred square-bracket syntax requires a selected engine.",
                               node.range);
                    return;
                }
                auto prepared = call;
                prepared.payload = expand_deferred_payload(call.payload, scope, node.range);
                const auto id = deferred_->add(prepared, node.range,
                                               deferred_escape_context());
                append(DeferredStore::marker('B', id), node.range);
                for (const auto &child : call.children)
                {
                    if (const auto *slot = std::get_if<SlotNode>(&child->value);
                        slot && !slot->reference)
                    {
                        append(DeferredStore::marker('S', id, slot->name), child->range);
                        render_nodes(slot->body, scope, depth);
                    }
                    else
                    {
                        append(DeferredStore::marker('S', id, "default"), child->range);
                        render_nodes({child}, scope, depth);
                    }
                }
                append(DeferredStore::marker('E', id), node.range);
            }

            [[nodiscard]] std::string expand_deferred_payload(
                const std::string_view payload, RenderScope &scope,
                const SourceRange range)
            {
                std::string output;
                for (std::size_t position = 0; position < payload.size();)
                {
                    if (payload[position] != '@' || position + 1 >= payload.size())
                    {
                        output.push_back(payload[position++]);
                        continue;
                    }
                    if (payload[position + 1] == '@')
                    {
                        output.push_back('@');
                        position += 2;
                        continue;
                    }
                    if (payload[position + 1] != '{')
                    {
                        output.push_back(payload[position++]);
                        continue;
                    }
                    const auto end = payload.find('}', position + 2);
                    if (end == std::string_view::npos)
                    {
                        diagnostic("EM0860",
                                   "Unclosed interpolation in deferred payload.", range);
                        break;
                    }
                    auto value = evaluate(std::string{payload.substr(
                                              position + 2, end - position - 2)},
                                          scope.evaluation, range);
                    if (value)
                    {
                        try
                        {
                            output += emit_scalar(*value);
                        }
                        catch (...)
                        {
                            diagnostic("EM0861",
                                       "Deferred payload interpolation requires a scalar value.",
                                       range);
                        }
                    }
                    position = end + 1;
                }
                return output;
            }

            [[nodiscard]] std::string deferred_escape_context() const
            {
                if (subject_)
                    return "subject";
                const auto open = output_.html.rfind('<');
                const auto close = output_.html.rfind('>');
                if (open == std::string::npos ||
                    (close != std::string::npos && close > open))
                    return "html_text";
                const auto head = output_.html.substr(open);
                const auto double_quote = head.rfind("=\"");
                const auto single_quote = head.rfind("='");
                const auto assignment = double_quote == std::string::npos
                                            ? single_quote
                                        : single_quote == std::string::npos
                                            ? double_quote
                                            : std::max(double_quote, single_quote);
                if (assignment == std::string::npos)
                    return "html_attribute";
                auto name_end = assignment;
                while (name_end > 0 &&
                       std::isspace(static_cast<unsigned char>(head[name_end - 1])))
                    --name_end;
                auto name_start = name_end;
                while (name_start > 0 &&
                       (std::isalnum(static_cast<unsigned char>(head[name_start - 1])) ||
                        head[name_start - 1] == '-' || head[name_start - 1] == '_'))
                    --name_start;
                const auto name = head.substr(name_start, name_end - name_start);
                return name == "href" || name == "src" || name == "action"
                           ? "url"
                           : "html_attribute";
            }

            void render_node(const IfNode &condition, const Node &node, RenderScope &scope,
                             const std::size_t depth)
            {
                auto value = evaluate(condition.condition, scope.evaluation, node.range);
                if (value)
                    render_nodes(truthy(*value) ? condition.then_nodes : condition.else_nodes,
                                 scope, depth);
            }

            void render_node(const ForNode &loop, const Node &node, RenderScope &scope,
                             const std::size_t depth)
            {
                auto value = evaluate(loop.expression, scope.evaluation, node.range);
                if (!value)
                    return;
                if (!value->is_array() && !value->is_object())
                {
                    diagnostic("EM0710", "@For requires an array or object.", node.range);
                    return;
                }
                const auto count = value->size();
                if (loop_iterations_ + count > limits_.maximum_loop_iterations)
                {
                    diagnostic("EM0903", "Compiler loop iteration limit exceeded.",
                               node.range);
                    return;
                }
                loop_iterations_ += count;
                if (value->is_array())
                {
                    for (const auto &item : *value)
                    {
                        if (cancellation_.is_cancelled())
                            return;
                        auto nested = scope;
                        nested.evaluation.locals[loop.variable] = item;
                        render_nodes(loop.body, nested, depth);
                    }
                }
                else
                {
                    for (const auto &[key, item] : value->items())
                    {
                        if (cancellation_.is_cancelled())
                            return;
                        auto nested = scope;
                        nested.evaluation.locals[loop.variable] = item;
                        nested.evaluation.locals[loop.variable + "_key"] = key;
                        render_nodes(loop.body, nested, depth);
                    }
                }
            }

            void render_node(const SlotNode &slot, const Node &node, RenderScope &scope,
                             const std::size_t depth)
            {
                if (!slot.reference)
                {
                    diagnostic("EM0711",
                               "A slot fill is only valid directly inside a component call.",
                               node.range);
                    return;
                }
                if (shell_content_ && slot.name == "default")
                {
                    for (const auto &segment : shell_content_->segments)
                    {
                        const auto text = std::string_view{shell_content_->html}.substr(
                            segment.output_start, segment.output_end - segment.output_start);
                        if (output_.html.size() + text.size() > limits_.maximum_html_bytes)
                        {
                            diagnostic("EM0901",
                                       "Generated HTML exceeds the configured byte limit.",
                                       segment.origin);
                            output_limited_ = true;
                            return;
                        }
                        output_.append(text, segment.origin, segment.expansion_stack);
                    }
                    return;
                }
                const auto found = scope.slots.find(slot.name);
                if (found == scope.slots.end())
                    return;
                auto call_scope = scope;
                call_scope.evaluation = found->second.context;
                render_nodes(found->second.nodes, call_scope, depth);
            }

            void render_node(const ComponentNode &call, const Node &node, RenderScope &scope,
                             const std::size_t depth)
            {
                if (depth >= limits_.maximum_expansion_depth)
                {
                    diagnostic("EM0904", "Component expansion depth limit exceeded.",
                               node.range);
                    return;
                }
                const auto found = registry_.components.find(call.name);
                if (found == registry_.components.end())
                {
                    diagnostic("EM0720", "Unknown component @" + call.name + ".", node.range);
                    return;
                }
                const auto &definition = found->second;
                RenderScope nested;
                nested.evaluation = scope.evaluation;
                std::unordered_set<std::string> supplied;
                std::string style_names;
                std::string call_css;
                for (const auto &argument : call.parameters)
                {
                    if (!supplied.insert(argument.name).second)
                        continue;
                    auto value = evaluate(argument.expression, scope.evaluation,
                                          argument.range);
                    if (!value)
                        continue;
                    if (argument.name == "style" || argument.name == "css")
                    {
                        if (!value->is_string())
                        {
                            diagnostic("EM0721", argument.name + ": requires a quoted string.",
                                       argument.range);
                        }
                        else if (argument.name == "style")
                        {
                            style_names = value->get<std::string>();
                        }
                        else
                        {
                            call_css = value->get<std::string>();
                        }
                        continue;
                    }
                    const auto declaration = std::find_if(
                        definition.props.begin(), definition.props.end(),
                        [&](const auto &prop)
                        { return prop.name == argument.name; });
                    if (declaration == definition.props.end())
                    {
                        diagnostic("EM0722", "@" + call.name + " has no prop “" + argument.name + "”.",
                                   argument.range);
                        continue;
                    }
                    if (validate_prop(*declaration, *value, diagnostics_, argument.range))
                        nested.evaluation.props[argument.name] = std::move(*value);
                }

                for (const auto &declaration : definition.props)
                {
                    if (nested.evaluation.props.contains(declaration.name))
                        continue;
                    if (declaration.has_default)
                    {
                        auto value = evaluate(declaration.default_expression,
                                              nested.evaluation, declaration.default_range);
                        if (value && validate_prop(declaration, *value, diagnostics_,
                                                   declaration.default_range))
                            nested.evaluation.props[declaration.name] = std::move(*value);
                    }
                    else if (declaration.optional)
                    {
                        nested.evaluation.props[declaration.name] = nullptr;
                    }
                    else
                    {
                        diagnostic("EM0723", "@" + call.name + " requires prop “" + declaration.name + "”.",
                                   node.range, Severity::error,
                                   {{declaration.range, "Required prop is declared here."}});
                    }
                }

                std::vector<NodePtr> default_nodes;
                for (const auto &child : call.children)
                {
                    if (const auto *fill = std::get_if<SlotNode>(&child->value);
                        fill && !fill->reference)
                    {
                        if (nested.slots.contains(fill->name))
                            diagnostic("EM0724", "Slot “" + fill->name + "” is filled more than once.",
                                       child->range);
                        nested.slots[fill->name] = {fill->body, scope.evaluation};
                    }
                    else
                    {
                        default_nodes.push_back(child);
                    }
                }
                if (!default_nodes.empty())
                    nested.slots["default"] = {default_nodes, scope.evaluation};
                for (const auto &[name, content] : nested.slots)
                {
                    const auto declaration = std::find_if(
                        definition.slots.begin(), definition.slots.end(),
                        [&](const auto &slot)
                        { return slot.name == name; });
                    if (declaration == definition.slots.end())
                        diagnostic("EM0725", "@" + call.name + " has no slot “" + name + "”.",
                                   node.range);
                }
                for (const auto &declaration : definition.slots)
                    if (declaration.required && !nested.slots.contains(declaration.name))
                        diagnostic("EM0726", "@" + call.name + " requires slot “" + declaration.name + "”.",
                                   node.range);
                if (definition.slots.empty() && !call.children.empty())
                    diagnostic("EM0727", "@" + call.name + " does not accept a body.",
                               node.range);
                if (!definition.slots.empty() && call.self_closing &&
                    std::any_of(definition.slots.begin(), definition.slots.end(),
                                [](const auto &slot)
                                { return slot.required; }))
                    diagnostic("EM0728", "@" + call.name + " cannot be self-closing because it has required slots.",
                               node.range);

                stack_.push_back({call.name, node.range, definition.range});
                const auto output_start = output_.html.size();
                render_nodes(definition.body, nested, depth + 1);
                if (!style_names.empty() || !call_css.empty())
                {
                    auto layers = resolve_style_layers(style_names, call_css, registry_.styles,
                                                       nested.evaluation, node.range,
                                                       diagnostics_);
                    auto fragment = output_.html.substr(output_start);
                    output_.html.resize(output_start);
                    while (!output_.segments.empty() &&
                           output_.segments.back().output_start >= output_start)
                        output_.segments.pop_back();
                    append(apply_root_styles(std::move(fragment), layers), node.range);
                }
                stack_.pop_back();
            }

            const Registry &registry_;
            const CompilationLimits &limits_;
            CancellationToken cancellation_;
            std::vector<Diagnostic> &diagnostics_;
            GeneratedHtml output_;
            std::vector<ExpansionFrame> stack_;
            std::size_t loop_iterations_{};
            bool output_limited_{};
            const GeneratedHtml *shell_content_{};
            DeferredStore *deferred_{};
            bool subject_{};
        };
    } // namespace

    GeneratedHtml render_document(
        const Registry &registry, const CompilationLimits &limits,
        const CancellationToken cancellation, std::vector<Diagnostic> &diagnostics,
        const std::vector<NodePtr> &nodes, const Json &data,
        const std::unordered_map<std::string, Json> &tokens,
        DeferredStore *deferred, const bool subject)
    {
        return Renderer{registry, limits, cancellation, diagnostics, deferred, subject}.render(nodes, data, tokens);
    }

    GeneratedHtml render_shell_document(
        const Registry &registry, const CompilationLimits &limits,
        const CancellationToken cancellation, std::vector<Diagnostic> &diagnostics,
        const std::vector<NodePtr> &nodes, const Json &data,
        const std::unordered_map<std::string, Json> &tokens, const GeneratedHtml &content,
        DeferredStore *deferred, const bool subject)
    {
        return Renderer{registry, limits, cancellation, diagnostics, deferred, subject}.render_shell(
            nodes, data, tokens, content);
    }
} // namespace email_markup::detail
