#include "pipeline.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace email_markup::detail
{
    namespace
    {
        std::string quoted_path(const std::string &expression)
        {
            try
            {
                const auto value = Json::parse(expression);
                return value.is_string() ? value.get<std::string>() : std::string{};
            }
            catch (...)
            {
                return {};
            }
        }

        std::size_t count_nodes(const std::vector<NodePtr> &nodes)
        {
            std::size_t count = nodes.size();
            for (const auto &node : nodes)
            {
                if (const auto *component = std::get_if<ComponentNode>(&node->value))
                    count += count_nodes(component->children);
                else if (const auto *conditional = std::get_if<IfNode>(&node->value))
                    count += count_nodes(conditional->then_nodes) +
                             count_nodes(conditional->else_nodes);
                else if (const auto *loop = std::get_if<ForNode>(&node->value))
                    count += count_nodes(loop->body);
                else if (const auto *slot = std::get_if<SlotNode>(&node->value))
                    count += count_nodes(slot->body);
            }
            return count;
        }
    } // namespace

    std::string path_key(const std::filesystem::path &path)
    {
        return portable_path_string(path.lexically_normal());
    }

    void Loader::issue(std::string code, std::string message, const SourceRange range)
    {
        if (diagnostics.size() < request.limits.maximum_diagnostics)
            diagnostics.push_back(
                {std::move(code), Severity::error, std::move(message), range});
    }

    std::optional<std::filesystem::path> Loader::resolve_path(
        const std::filesystem::path &including, const std::string_view requested,
        const SourceRange range, std::string *contents)
    {
        std::vector<std::filesystem::path> attempted;
        auto resolved = resolver.resolve(including, requested, request.include_directories,
                                         allowed_roots, attempted);
        if (!resolved)
        {
            std::ostringstream message;
            message << "Cannot resolve Email Markup file “" << requested << "”. Tried:";
            for (const auto &path : attempted)
                message << "\n  " << path.string();
            issue("EM0301", message.str(), range);
            return std::nullopt;
        }
        if (contents)
            *contents = std::move(resolved->contents);
        return resolved->canonical_path;
    }

    void Loader::load(const std::filesystem::path &path, std::string contents,
                      const std::size_t depth, const bool entry)
    {
        if (cancellation.is_cancelled())
        {
            if (diagnostics.empty())
                issue("EM0902", "Compilation cancelled.", {});
            return;
        }
        const auto canonical = path.lexically_normal();
        const auto key = path_key(canonical);
        if (std::find(stack.begin(), stack.end(), key) != stack.end())
        {
            std::string cycle;
            for (const auto &item : stack)
                cycle += item + " -> ";
            issue("EM0302", "Include cycle: " + cycle + key, {});
            return;
        }
        if (loaded.contains(key))
            return;
        if (depth > request.limits.maximum_include_depth)
        {
            issue("EM0905", "Include depth limit exceeded.", {});
            return;
        }
        if (loaded.size() >= request.limits.maximum_includes)
        {
            issue("EM0906", "Include count limit exceeded.", {});
            return;
        }
        if (contents.size() > request.limits.maximum_source_bytes)
        {
            issue("EM0907", "Email Markup source exceeds the configured byte limit.", {});
            return;
        }
        if (!is_valid_utf8(contents))
        {
            issue("EM0303", "Email Markup source is not valid UTF-8.", {});
            return;
        }

        stack.push_back(key);
        const auto source = sources->add(canonical, std::move(contents));
        auto parsed = parse(source, sources->get(source).text,
                            request.limits.maximum_diagnostics);
        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(),
                           parsed.diagnostics.end());
        ast_nodes += count_nodes(parsed.document.nodes);
        for (const auto &[name, definition] : parsed.document.components)
            ast_nodes += count_nodes(definition.body);
        if (ast_nodes > request.limits.maximum_ast_nodes)
        {
            issue("EM0908", "AST node limit exceeded.", {source, 0, 0});
            stack.pop_back();
            return;
        }

        for (const auto &node : parsed.document.nodes)
        {
            const auto *include = std::get_if<IncludeNode>(&node->value);
            if (!include)
                continue;
            const auto requested = quoted_path(include->expression);
            if (requested.empty())
            {
                issue("EM0304", "@Include requires a quoted .em path.", node->range);
                continue;
            }
            std::string included_contents;
            auto included_path =
                resolve_path(canonical, requested, node->range, &included_contents);
            if (included_path)
                load(*included_path, std::move(included_contents), depth + 1);
        }

        merge_registry(registry, parsed.document, entry, diagnostics);
        dependencies.push_back(canonical);
        documents[key] = std::move(parsed.document);
        loaded.insert(key);
        stack.pop_back();
    }
} // namespace email_markup::detail
