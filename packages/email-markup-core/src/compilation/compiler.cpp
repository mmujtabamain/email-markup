#include "pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

#include "email-markup/core/css.hpp"
#include "email-markup/core/lint.hpp"
#include "email-markup/core/styles.hpp"

namespace email_markup::detail
{
    bool has_errors(const std::vector<Diagnostic> &diagnostics)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [](const auto &diagnostic)
                           { return diagnostic.severity == Severity::error; });
    }
} // namespace email_markup::detail

namespace email_markup
{
    CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> flag)
        : flag_(std::move(flag))
    {
    }

    bool CancellationToken::is_cancelled() const noexcept
    {
        return flag_ && flag_->load(std::memory_order_relaxed);
    }

    bool CompilationResult::ok() const noexcept
    {
        return !detail::has_errors(diagnostics);
    }

    CompilationResult compile(const CompilationRequest &request, FileResolver &files,
                              const CancellationToken cancellation)
    {
        CompilationResult result;
        if (request.entry_path.extension() != ".em")
        {
            result.diagnostics.push_back({"EM0001", Severity::error, "Email Markup 1 accepts only .em source files.", {}});
            return result;
        }

        detail::Loader loader{request, files, cancellation};
        std::error_code error;
        const auto project_root =
            std::filesystem::weakly_canonical(request.entry_path.parent_path(), error);
        loader.allowed_roots.push_back(error ? request.entry_path.parent_path() : project_root);
        loader.allowed_roots.insert(loader.allowed_roots.end(), request.allowed_roots.begin(),
                                    request.allowed_roots.end());
        loader.allowed_roots.insert(loader.allowed_roots.end(),
                                    request.include_directories.begin(),
                                    request.include_directories.end());

        for (const auto &import : request.imports)
        {
            std::string contents;
            if (auto path = loader.resolve_path(request.entry_path, import.string(), {},
                                                &contents))
                loader.load(*path, std::move(contents), 0);
        }

        std::optional<std::filesystem::path> shell_path;
        if (request.shell)
        {
            std::string contents;
            shell_path = loader.resolve_path(request.entry_path, request.shell->string(), {},
                                             &contents);
            if (shell_path)
                loader.load(*shell_path, std::move(contents), 0);
        }

        loader.load(request.entry_path, request.source, 0, true);
        const auto tokens =
            detail::resolve_tokens(loader.registry, request.data, loader.diagnostics);
        const auto entry_key = detail::path_key(request.entry_path);
        const auto entry = loader.documents.find(entry_key);
        if (entry != loader.documents.end() && !detail::has_errors(loader.diagnostics))
        {
            auto generated = detail::render_document(
                loader.registry, request.limits, cancellation, loader.diagnostics,
                entry->second.nodes, request.data, tokens);
            if (shell_path && !detail::has_errors(loader.diagnostics))
            {
                const auto shell = loader.documents.find(detail::path_key(*shell_path));
                if (shell != loader.documents.end())
                {
                    generated = detail::render_shell_document(
                        loader.registry, request.limits, cancellation, loader.diagnostics,
                        shell->second.nodes, request.data, tokens, generated);
                    for (const auto &media : loader.registry.media)
                    {
                        auto media_diagnostics = validate_media(media);
                        loader.diagnostics.insert(loader.diagnostics.end(),
                                                  media_diagnostics.begin(),
                                                  media_diagnostics.end());
                    }
                    if (!loader.registry.media.empty())
                    {
                        const auto head = generated.html.find("</head>");
                        const auto position = head == std::string::npos ? 0 : head;
                        generated.insert(position, "</style>",
                                         loader.registry.media.back().range);
                        for (auto media = loader.registry.media.rbegin();
                             media != loader.registry.media.rend(); ++media)
                            generated.insert(position,
                                             "@media " + media->query + "{" + media->css +
                                                 "}",
                                             media->range);
                        generated.insert(position, "<style>",
                                         loader.registry.media.front().range);
                    }
                }
            }
            generated = inline_css(std::move(generated));
            auto findings = lint_html(generated,
                                      shell_path ? LintRole::shell : LintRole::content,
                                      {entry->second.source, 0, 0});
            loader.diagnostics.insert(loader.diagnostics.end(), findings.begin(),
                                      findings.end());
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
} // namespace email_markup
