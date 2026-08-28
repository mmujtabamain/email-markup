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
#include "email-markup/core/emir.hpp"
#include "email-markup/core/engine.hpp"

namespace email_markup::detail
{
    [[nodiscard]] bool has_errors(const std::vector<Diagnostic> &diagnostics);
    [[nodiscard]] std::string path_key(const std::filesystem::path &path);

    struct DeferredCallRecord
    {
        std::size_t id{};
        std::string name;
        std::string payload;
        SourceRange range;
        std::string escape{"html_text"};
        bool bare{};
        bool self_closing{};
    };

    struct DeferredStore
    {
        std::vector<DeferredCallRecord> calls;

        [[nodiscard]] std::size_t add(const DeferredCallNode &call, SourceRange range,
                                      std::string escape);
        [[nodiscard]] static std::string marker(char kind, std::size_t id,
                                                std::string_view slot = {});
    };

    [[nodiscard]] std::optional<EmirArtifact> lower_deferred_to_emir(
        GeneratedHtml &generated, const DeferredStore &deferred,
        const EngineDefinition &engine, const SourceManager &sources,
        const std::filesystem::path &entry_path,
        std::vector<Diagnostic> &diagnostics, bool subject = false);

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
        const std::unordered_map<std::string, Json> &tokens,
        DeferredStore *deferred = nullptr, bool subject = false);
    [[nodiscard]] GeneratedHtml render_shell_document(
        const Registry &registry, const CompilationLimits &limits,
        CancellationToken cancellation, std::vector<Diagnostic> &diagnostics,
        const std::vector<NodePtr> &nodes, const Json &data,
        const std::unordered_map<std::string, Json> &tokens, const GeneratedHtml &content,
        DeferredStore *deferred = nullptr, bool subject = false);
} // namespace email_markup::detail
