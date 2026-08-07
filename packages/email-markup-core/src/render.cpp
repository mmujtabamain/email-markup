#include "email-markup/core/render.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

#include "email-markup/core/css.hpp"
#include "email-markup/core/lint.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/styles.hpp"
#include "email-markup/core/types.hpp"

namespace email_markup {
namespace {

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == Severity::error;
    });
}

std::string quoted_path(const std::string& expression) {
    try {
        auto value = Json::parse(expression);
        return value.is_string() ? value.get<std::string>() : std::string{};
    } catch (...) { return {}; }
}

std::string html_escape(const std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output.push_back(ch);
        }
    }
    return output;
}

struct SlotContent {
    std::vector<NodePtr> nodes;
    EvaluationContext context;
};

struct RenderScope {
    EvaluationContext evaluation;
    std::unordered_map<std::string, SlotContent> slots;
};

class Renderer {
public:
    Renderer(const Registry& registry, const CompilationLimits& limits,
             const CancellationToken cancellation, std::vector<Diagnostic>& diagnostics)
        : registry_(registry), limits_(limits), cancellation_(cancellation),
          diagnostics_(diagnostics) {}

    GeneratedHtml render(const std::vector<NodePtr>& nodes, const Json& data,
                         const std::unordered_map<std::string, Json>& tokens) {
        RenderScope scope;
        scope.evaluation.data = &data;
        scope.evaluation.tokens = tokens;
        render_nodes(nodes, scope, 0);
        return std::move(output_);
    }

    GeneratedHtml render_shell(const std::vector<NodePtr>& nodes, const Json& data,
                               const std::unordered_map<std::string, Json>& tokens,
                               const GeneratedHtml& content) {
        RenderScope scope;
        scope.evaluation.data = &data;
        scope.evaluation.tokens = tokens;
        auto synthetic = std::make_shared<Node>(Node{{}, TextNode{content.html}});
        scope.slots["default"] = {{synthetic}, scope.evaluation};
        shell_raw_ = true;
        render_nodes(nodes, scope, 0);
        return std::move(output_);
    }

private:
    void diagnostic(std::string code, std::string message, const SourceRange range,
                    const Severity severity = Severity::error,
                    std::vector<RelatedLocation> related = {}) {
        if (diagnostics_.size() < limits_.maximum_diagnostics) {
            diagnostics_.push_back({std::move(code), severity, std::move(message), range,
                                    std::move(related)});
        }
    }

    std::optional<Json> evaluate(const std::string& expression,
                                 const EvaluationContext& context,
                                 const SourceRange range) {
        if (expression.size() >= 2 && expression.front() == '"' &&
            expression.back() == '"' && expression.find("@{") != std::string::npos) {
            Json parsed;
            try { parsed = Json::parse(expression); }
            catch (...) {
                diagnostic("EM0701", "Invalid interpolated string literal.", range);
                return std::nullopt;
            }
            auto text = parsed.get<std::string>();
            static const std::regex embedded{R"(@\{([^{}]+)\})"};
            std::string result;
            std::size_t position = 0;
            for (std::sregex_iterator it{text.begin(), text.end(), embedded}, end;
                 it != end; ++it) {
                result += text.substr(position,
                    static_cast<std::size_t>(it->position()) - position);
                auto part = evaluate_expression((*it)[1].str(), context, range);
                diagnostics_.insert(diagnostics_.end(), part.diagnostics.begin(),
                                    part.diagnostics.end());
                if (!part.ok) return std::nullopt;
                try { result += emit_scalar(part.value); }
                catch (...) {
                    diagnostic("EM0702", "String interpolation requires a scalar value.", range);
                    return std::nullopt;
                }
                position = static_cast<std::size_t>(it->position() + it->length());
            }
            result += text.substr(position);
            return Json{result};
        }
        auto result = evaluate_expression(expression, context, range);
        diagnostics_.insert(diagnostics_.end(), result.diagnostics.begin(),
                            result.diagnostics.end());
        return result.ok ? std::optional<Json>{std::move(result.value)} : std::nullopt;
    }

    void append(const std::string_view text, const SourceRange range) {
        if (output_.html.size() + text.size() > limits_.maximum_html_bytes) {
            if (!output_limited_) {
                diagnostic("EM0901", "Generated HTML exceeds the configured byte limit.", range);
                output_limited_ = true;
            }
            return;
        }
        output_.append(text, range, stack_);
    }

    void render_nodes(const std::vector<NodePtr>& nodes, RenderScope& scope,
                      const std::size_t depth) {
        for (const auto& node : nodes) {
            if (cancellation_.is_cancelled()) {
                diagnostic("EM0902", "Compilation cancelled.", node->range);
                return;
            }
            if (output_limited_) return;
            std::visit([&](const auto& value) { render_node(value, *node, scope, depth); },
                       node->value);
        }
    }

    void render_node(const TextNode& text, const Node& node, RenderScope&,
                     std::size_t) {
        append(shell_raw_ && node.range.empty() ? text.text : text.text, node.range);
    }

    void render_node(const ExpressionNode& expression, const Node& node,
                     RenderScope& scope, std::size_t) {
        auto value = evaluate(expression.expression, scope.evaluation, node.range);
        if (!value) return;
        try { append(html_escape(emit_scalar(*value)), node.range); }
        catch (...) {
            diagnostic("EM0703",
                       "Interpolation cannot emit null, an array, or an object directly.",
                       node.range);
        }
    }

    void render_node(const IncludeNode&, const Node&, RenderScope&, std::size_t) {}

    void render_node(const IfNode& condition, const Node& node, RenderScope& scope,
                     const std::size_t depth) {
        auto value = evaluate(condition.condition, scope.evaluation, node.range);
        if (!value) return;
        render_nodes(truthy(*value) ? condition.then_nodes : condition.else_nodes,
                     scope, depth);
    }

    void render_node(const ForNode& loop, const Node& node, RenderScope& scope,
                     const std::size_t depth) {
        auto value = evaluate(loop.expression, scope.evaluation, node.range);
        if (!value) return;
        if (!value->is_array() && !value->is_object()) {
            diagnostic("EM0710", "@For requires an array or object.", node.range);
            return;
        }
        const auto count = value->size();
        if (loop_iterations_ + count > limits_.maximum_loop_iterations) {
            diagnostic("EM0903", "Compiler loop iteration limit exceeded.", node.range);
            return;
        }
        loop_iterations_ += count;
        if (value->is_array()) {
            for (const auto& item : *value) {
                if (cancellation_.is_cancelled()) return;
                auto nested = scope;
                nested.evaluation.locals[loop.variable] = item;
                render_nodes(loop.body, nested, depth);
            }
        } else {
            for (const auto& [key, item] : value->items()) {
                if (cancellation_.is_cancelled()) return;
                auto nested = scope;
                nested.evaluation.locals[loop.variable] = item;
                nested.evaluation.locals[loop.variable + "_key"] = key;
                render_nodes(loop.body, nested, depth);
            }
        }
    }

    void render_node(const SlotNode& slot, const Node& node, RenderScope& scope,
                     const std::size_t depth) {
        if (!slot.reference) {
            diagnostic("EM0711", "A slot fill is only valid directly inside a component call.",
                       node.range);
            return;
        }
        const auto found = scope.slots.find(slot.name);
        if (found == scope.slots.end()) return;
        auto call_scope = scope;
        call_scope.evaluation = found->second.context;
        render_nodes(found->second.nodes, call_scope, depth);
    }

    void render_node(const ComponentNode& call, const Node& node, RenderScope& scope,
                     const std::size_t depth) {
        if (depth >= limits_.maximum_expansion_depth) {
            diagnostic("EM0904", "Component expansion depth limit exceeded.", node.range);
            return;
        }
        const auto found = registry_.components.find(call.name);
        if (found == registry_.components.end()) {
            diagnostic("EM0720", "Unknown component @" + call.name + ".", node.range);
            return;
        }
        const auto& definition = found->second;
        RenderScope nested;
        nested.evaluation = scope.evaluation;
        std::unordered_set<std::string> supplied;
        std::string style_names;
        std::string call_css;
        for (const auto& argument : call.parameters) {
            if (!supplied.insert(argument.name).second) continue;
            auto value = evaluate(argument.expression, scope.evaluation, argument.range);
            if (!value) continue;
            if (argument.name == "style" || argument.name == "css") {
                if (!value->is_string()) {
                    diagnostic("EM0721", argument.name +
                        ": requires a quoted string.", argument.range);
                } else if (argument.name == "style") style_names = value->get<std::string>();
                else call_css = value->get<std::string>();
                continue;
            }
            const auto declaration = std::find_if(
                definition.props.begin(), definition.props.end(), [&](const auto& prop) {
                    return prop.name == argument.name;
                });
            if (declaration == definition.props.end()) {
                diagnostic("EM0722", "@" + call.name + " has no prop “" +
                           argument.name + "”.", argument.range);
                continue;
            }
            if (validate_prop(*declaration, *value, diagnostics_, argument.range)) {
                nested.evaluation.props[argument.name] = std::move(*value);
            }
        }
        for (const auto& declaration : definition.props) {
            if (nested.evaluation.props.contains(declaration.name)) continue;
            if (declaration.has_default) {
                auto value = evaluate(declaration.default_expression, nested.evaluation,
                                      declaration.range);
                if (value) nested.evaluation.props[declaration.name] = std::move(*value);
            } else if (declaration.optional) {
                nested.evaluation.props[declaration.name] = nullptr;
            } else {
                diagnostic("EM0723", "@" + call.name + " requires prop “" +
                           declaration.name + "”.", node.range,
                           Severity::error,
                           {{declaration.range, "Required prop is declared here."}});
            }
        }

        std::vector<NodePtr> default_nodes;
        for (const auto& child : call.children) {
            if (const auto* fill = std::get_if<SlotNode>(&child->value);
                fill && !fill->reference) {
                if (nested.slots.contains(fill->name)) {
                    diagnostic("EM0724", "Slot “" + fill->name +
                               "” is filled more than once.", child->range);
                }
                nested.slots[fill->name] = {fill->body, scope.evaluation};
            } else default_nodes.push_back(child);
        }
        if (!default_nodes.empty()) nested.slots["default"] = {default_nodes, scope.evaluation};
        for (const auto& [name, content] : nested.slots) {
            const auto declaration = std::find_if(
                definition.slots.begin(), definition.slots.end(), [&](const auto& slot) {
                    return slot.name == name;
                });
            if (declaration == definition.slots.end()) {
                diagnostic("EM0725", "@" + call.name + " has no slot “" + name + "”.",
                           node.range);
            }
        }
        for (const auto& declaration : definition.slots) {
            if (declaration.required && !nested.slots.contains(declaration.name)) {
                diagnostic("EM0726", "@" + call.name + " requires slot “" +
                           declaration.name + "”.", node.range);
            }
        }
        if (definition.slots.empty() && !call.children.empty()) {
            diagnostic("EM0727", "@" + call.name + " does not accept a body.", node.range);
        }
        if (!definition.slots.empty() && call.self_closing &&
            std::any_of(definition.slots.begin(), definition.slots.end(),
                        [](const auto& slot) { return slot.required; })) {
            diagnostic("EM0728", "@" + call.name +
                       " cannot be self-closing because it has required slots.", node.range);
        }

        stack_.push_back({call.name, node.range, definition.range});
        const auto output_start = output_.html.size();
        render_nodes(definition.body, nested, depth + 1);
        if (!style_names.empty() || !call_css.empty()) {
            auto layers = resolve_style_layers(style_names, call_css, registry_.styles,
                                               nested.evaluation, node.range, diagnostics_);
            auto fragment = output_.html.substr(output_start);
            output_.html.resize(output_start);
            while (!output_.segments.empty() &&
                   output_.segments.back().output_start >= output_start) {
                output_.segments.pop_back();
            }
            append(apply_root_styles(std::move(fragment), layers), node.range);
        }
        stack_.pop_back();
    }

    const Registry& registry_;
    const CompilationLimits& limits_;
    CancellationToken cancellation_;
    std::vector<Diagnostic>& diagnostics_;
    GeneratedHtml output_;
    std::vector<ExpansionFrame> stack_;
    std::size_t loop_iterations_{};
    bool output_limited_{};
    bool shell_raw_{};
};

struct Loader {
    const CompilationRequest& request;
    FileResolver& resolver;
    CancellationToken cancellation;
    std::shared_ptr<SourceManager> sources{std::make_shared<SourceManager>()};
    Registry registry;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::filesystem::path> dependencies;
    std::unordered_map<std::string, Document> documents;
    std::unordered_set<std::string> loaded;
    std::vector<std::string> stack;
    std::vector<std::filesystem::path> allowed_roots;
    std::size_t ast_nodes{};

    void issue(std::string code, std::string message, SourceRange range) {
        if (diagnostics.size() < request.limits.maximum_diagnostics) {
            diagnostics.push_back({std::move(code), Severity::error,
                                   std::move(message), range});
        }
    }

    std::optional<std::filesystem::path> resolve_path(
        const std::filesystem::path& including, const std::string_view requested,
        SourceRange range, std::string* contents = nullptr) {
        std::vector<std::filesystem::path> attempted;
        auto resolved = resolver.resolve(including, requested, request.include_directories,
                                         allowed_roots, attempted);
        if (!resolved) {
            std::ostringstream message;
            message << "Cannot resolve Email Markup file “" << requested << "”. Tried:";
            for (const auto& path : attempted) message << "\n  " << path.string();
            issue("EM0301", message.str(), range);
            return std::nullopt;
        }
        if (contents) *contents = std::move(resolved->contents);
        return resolved->canonical_path;
    }

    void load(const std::filesystem::path& path, std::string contents,
              const std::size_t depth, const bool entry = false) {
        if (cancellation.is_cancelled()) {
            if (diagnostics.empty()) issue("EM0902", "Compilation cancelled.", {});
            return;
        }
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        const auto key = (error ? path.lexically_normal() : canonical).string();
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
            std::string cycle;
            for (const auto& item : stack) cycle += item + " -> ";
            issue("EM0302", "Include cycle: " + cycle + key, {});
            return;
        }
        if (loaded.contains(key)) return;
        if (depth > request.limits.maximum_include_depth) {
            issue("EM0905", "Include depth limit exceeded.", {});
            return;
        }
        if (loaded.size() >= request.limits.maximum_includes) {
            issue("EM0906", "Include count limit exceeded.", {});
            return;
        }
        if (contents.size() > request.limits.maximum_source_bytes) {
            issue("EM0907", "Email Markup source exceeds the configured byte limit.", {});
            return;
        }
        if (!is_valid_utf8(contents)) {
            issue("EM0303", "Email Markup source is not valid UTF-8.", {});
            return;
        }
        stack.push_back(key);
        const auto source = sources->add(canonical, std::move(contents));
        auto parsed = parse(source, sources->get(source).text,
                            request.limits.maximum_diagnostics);
        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(),
                           parsed.diagnostics.end());
        std::function<std::size_t(const std::vector<NodePtr>&)> count_nodes =
            [&](const std::vector<NodePtr>& nodes) {
                std::size_t count = nodes.size();
                for (const auto& node : nodes) {
                    if (const auto* component = std::get_if<ComponentNode>(&node->value))
                        count += count_nodes(component->children);
                    else if (const auto* conditional = std::get_if<IfNode>(&node->value))
                        count += count_nodes(conditional->then_nodes) +
                                 count_nodes(conditional->else_nodes);
                    else if (const auto* loop = std::get_if<ForNode>(&node->value))
                        count += count_nodes(loop->body);
                    else if (const auto* slot = std::get_if<SlotNode>(&node->value))
                        count += count_nodes(slot->body);
                }
                return count;
            };
        ast_nodes += count_nodes(parsed.document.nodes);
        for (const auto& [_, definition] : parsed.document.components)
            ast_nodes += count_nodes(definition.body);
        if (ast_nodes > request.limits.maximum_ast_nodes) {
            issue("EM0908", "AST node limit exceeded.", {source, 0, 0});
            stack.pop_back();
            return;
        }
        for (const auto& node : parsed.document.nodes) {
            const auto* include = std::get_if<IncludeNode>(&node->value);
            if (!include) continue;
            const auto requested = quoted_path(include->expression);
            if (requested.empty()) {
                issue("EM0304", "@Include requires a quoted .em path.", node->range);
                continue;
            }
            std::string included_contents;
            auto included_path = resolve_path(canonical, requested, node->range,
                                              &included_contents);
            if (included_path) load(*included_path, std::move(included_contents), depth + 1);
        }
        merge_registry(registry, parsed.document, entry, diagnostics);
        dependencies.push_back(canonical);
        documents[key] = std::move(parsed.document);
        loaded.insert(key);
        stack.pop_back();
    }
};

std::unordered_map<std::string, Json> resolve_tokens(
    const Registry& registry, const Json& data, std::vector<Diagnostic>& diagnostics) {
    std::unordered_map<std::string, Json> output;
    std::unordered_set<std::string> visiting;
    std::function<std::optional<Json>(const std::string&)> resolve = [&](const std::string& name)
        -> std::optional<Json> {
        if (const auto found = output.find(name); found != output.end()) return found->second;
        const auto definition = registry.tokens.find(name);
        if (definition == registry.tokens.end()) return std::nullopt;
        if (!visiting.insert(name).second) {
            diagnostics.push_back({"EM0602", Severity::error,
                                   "Design-token cycle involving “" + name + "”.",
                                   definition->second.range});
            return std::nullopt;
        }
        EvaluationContext context;
        context.data = &data;
        static const std::regex reference{R"(token\.([A-Za-z_][A-Za-z0-9_]*))"};
        const auto& expression = definition->second.expression;
        for (std::sregex_iterator it{expression.begin(), expression.end(), reference}, end;
             it != end; ++it) {
            const auto dependency = (*it)[1].str();
            if (auto value = resolve(dependency)) context.tokens[dependency] = *value;
        }
        EvaluationResult value;
        if (expression.size() >= 2 && expression.front() == '"' &&
            expression.back() == '"' && expression.find("@{") != std::string::npos) {
            try {
                auto text = Json::parse(expression).get<std::string>();
                std::string interpolated;
                std::size_t position = 0;
                static const std::regex embedded{R"(@\{([^{}]+)\})"};
                bool ok = true;
                for (std::sregex_iterator it{text.begin(), text.end(), embedded}, end;
                     it != end; ++it) {
                    interpolated += text.substr(position,
                        static_cast<std::size_t>(it->position()) - position);
                    auto part = evaluate_expression((*it)[1].str(), context,
                                                    definition->second.range);
                    diagnostics.insert(diagnostics.end(), part.diagnostics.begin(),
                                       part.diagnostics.end());
                    if (!part.ok) { ok = false; break; }
                    try { interpolated += emit_scalar(part.value); }
                    catch (...) { ok = false; break; }
                    position = static_cast<std::size_t>(it->position() + it->length());
                }
                interpolated += text.substr(position);
                value = {interpolated, {}, ok};
            } catch (...) {
                value.diagnostics.push_back({"EM0604", Severity::error,
                    "Invalid token string expression.", definition->second.range});
            }
        } else {
            value = evaluate_expression(expression, context, definition->second.range);
        }
        diagnostics.insert(diagnostics.end(), value.diagnostics.begin(),
                           value.diagnostics.end());
        visiting.erase(name);
        if (!value.ok || value.value.is_array() || value.value.is_object()) {
            if (value.ok) diagnostics.push_back({"EM0603", Severity::error,
                "Design token “" + name + "” must be scalar.", definition->second.range});
            return std::nullopt;
        }
        output[name] = value.value;
        return value.value;
    };
    for (const auto& [name, _] : registry.tokens) resolve(name);
    return output;
}

}  // namespace

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> flag)
    : flag_(std::move(flag)) {}

bool CancellationToken::is_cancelled() const noexcept {
    return flag_ && flag_->load(std::memory_order_relaxed);
}

bool CompilationResult::ok() const noexcept { return !has_errors(diagnostics); }

CompilationResult compile(const CompilationRequest& request, FileResolver& files,
                          const CancellationToken cancellation) {
    CompilationResult result;
    if (request.entry_path.extension() != ".em") {
        result.diagnostics.push_back({"EM0001", Severity::error,
            "Email Markup 1 accepts only .em source files.", {}});
        return result;
    }
    Loader loader{request, files, cancellation};
    std::error_code error;
    const auto project_root = std::filesystem::weakly_canonical(
        request.entry_path.parent_path(), error);
    loader.allowed_roots.push_back(error ? request.entry_path.parent_path() : project_root);
    loader.allowed_roots.insert(loader.allowed_roots.end(), request.allowed_roots.begin(),
                                request.allowed_roots.end());
    loader.allowed_roots.insert(loader.allowed_roots.end(), request.include_directories.begin(),
                                request.include_directories.end());

    for (const auto& import : request.imports) {
        std::string contents;
        if (auto path = loader.resolve_path(request.entry_path, import.string(), {}, &contents)) {
            loader.load(*path, std::move(contents), 0);
        }
    }

    std::optional<std::filesystem::path> shell_path;
    if (request.shell) {
        std::string contents;
        shell_path = loader.resolve_path(request.entry_path, request.shell->string(), {}, &contents);
        if (shell_path) loader.load(*shell_path, std::move(contents), 0);
    }

    loader.load(request.entry_path, request.source, 0, true);
    auto tokens = resolve_tokens(loader.registry, request.data, loader.diagnostics);
    const auto entry_key = std::filesystem::weakly_canonical(request.entry_path, error).string();
    const auto entry = loader.documents.find(entry_key);
    if (entry != loader.documents.end() && !has_errors(loader.diagnostics)) {
        Renderer renderer{loader.registry, request.limits, cancellation, loader.diagnostics};
        auto generated = renderer.render(entry->second.nodes, request.data, tokens);
        if (shell_path && !has_errors(loader.diagnostics)) {
            const auto shell = loader.documents.find(shell_path->string());
            if (shell != loader.documents.end()) {
                Renderer shell_renderer{loader.registry, request.limits, cancellation,
                                        loader.diagnostics};
                generated = shell_renderer.render_shell(shell->second.nodes, request.data,
                                                        tokens, generated);
                std::string media_css;
                for (const auto& media : loader.registry.media) {
                    auto media_diagnostics = validate_media(media);
                    loader.diagnostics.insert(loader.diagnostics.end(),
                                              media_diagnostics.begin(),
                                              media_diagnostics.end());
                    media_css += "@media " + media.query + "{" + media.css + "}";
                }
                if (!media_css.empty()) {
                    const auto head = generated.html.find("</head>");
                    const auto block = "<style>" + media_css + "</style>";
                    if (head == std::string::npos) generated.html.insert(0, block);
                    else generated.html.insert(head, block);
                }
            }
        }
        generated.html = inline_css(std::move(generated.html));
        auto findings = lint_html(generated.html,
                                  shell_path ? LintRole::shell : LintRole::content,
                                  {entry->second.source, 0, 0});
        loader.diagnostics.insert(loader.diagnostics.end(), findings.begin(), findings.end());
        result.generated = std::move(generated);
    }
    auto snapshot = std::make_shared<DocumentSnapshot>();
    snapshot->sources = loader.sources;
    snapshot->entry = entry == loader.documents.end() ? 0 : entry->second.source;
    result.snapshot = std::move(snapshot);
    result.dependencies = std::move(loader.dependencies);
    result.diagnostics = std::move(loader.diagnostics);
    return result;
}

}  // namespace email_markup
