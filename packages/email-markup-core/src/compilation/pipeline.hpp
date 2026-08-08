#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "email-markup/core/parser.hpp"
#include "email-markup/core/render.hpp"

namespace email_markup::detail
{
    [[nodiscard]] bool has_errors(const std::vector<Diagnostic> &diagnostics);
    [[nodiscard]] std::string path_key(const std::filesystem::path &path);

    struct Loader
    {
        const CompilationRequest &request;
        FileResolver &resolver;
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

        void issue(std::string code, std::string message, SourceRange range);
        [[nodiscard]] std::optional<std::filesystem::path> resolve_path(
            const std::filesystem::path &including, std::string_view requested,
            SourceRange range, std::string *contents = nullptr);
        void load(const std::filesystem::path &path, std::string contents,
                  std::size_t depth, bool entry = false);
    };

    [[nodiscard]] std::unordered_map<std::string, Json> resolve_tokens(
        const Registry &registry, const Json &data, std::vector<Diagnostic> &diagnostics);

    [[nodiscard]] GeneratedHtml render_document(
        const Registry &registry, const CompilationLimits &limits,
        CancellationToken cancellation, std::vector<Diagnostic> &diagnostics,
        const std::vector<NodePtr> &nodes, const Json &data,
        const std::unordered_map<std::string, Json> &tokens);
    [[nodiscard]] GeneratedHtml render_shell_document(
        const Registry &registry, const CompilationLimits &limits,
        CancellationToken cancellation, std::vector<Diagnostic> &diagnostics,
        const std::vector<NodePtr> &nodes, const Json &data,
        const std::unordered_map<std::string, Json> &tokens, const GeneratedHtml &content);
} // namespace email_markup::detail
