#include "pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

#include "email-markup/core/css.hpp"
#include "email-markup/core/engine.hpp"
#include "email-markup/core/images.hpp"
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
        const auto entry_key = detail::path_key(request.entry_path);
        const auto entry = loader.documents.find(entry_key);

        std::optional<std::filesystem::path> document_engine;
        if (entry != loader.documents.end())
        {
            for (const auto &node : entry->second.nodes)
            {
                const auto *selection = std::get_if<EngineNode>(&node->value);
                if (!selection)
                    continue;
                if (document_engine)
                {
                    loader.issue("EM0840", "An entry document may select only one engine.",
                                 node->range);
                    continue;
                }
                try
                {
                    const auto value = Json::parse(selection->expression);
                    if (!value.is_string())
                        throw std::invalid_argument("not a string");
                    document_engine = value.get<std::string>();
                }
                catch (...)
                {
                    loader.issue("EM0841", "@Engine requires a quoted .emt path.",
                                 node->range);
                }
            }
        }
        for (const auto &[key, document] : loader.documents)
            if (key != entry_key)
                for (const auto &node : document.nodes)
                    if (std::holds_alternative<EngineNode>(node->value))
                        loader.issue("EM0842",
                                     "@Engine is valid only in the entry document.",
                                     node->range);

        std::optional<EngineDefinition> engine;
        const auto resolve_engine = [&](const std::filesystem::path &selection,
                                        const SourceRange range)
            -> std::optional<std::pair<std::filesystem::path, std::string>>
        {
            std::string contents;
            auto requested = selection.string();
            constexpr std::string_view library_prefix{"${EMAIL_MARKUP_LIB}/"};
            if (requested.starts_with(library_prefix))
                requested.erase(0, library_prefix.size());
            auto path = loader.resolve_path(request.entry_path, requested, range,
                                            &contents);
            if (!path)
                return std::nullopt;
            if (path->extension() != ".emt")
            {
                loader.issue("EM0843", "Selected engine must be a .emt file.", range);
                return std::nullopt;
            }
            return std::pair{*path, std::move(contents)};
        };
        std::optional<std::pair<std::filesystem::path, std::string>> selected;
        if (request.engine)
            selected = resolve_engine(*request.engine, {});
        if (document_engine)
        {
            auto authored = resolve_engine(*document_engine, {});
            if (selected && authored && detail::path_key(selected->first) !=
                                            detail::path_key(authored->first))
                loader.issue("EM0844",
                             "Conflicting engine selections resolve to different files.", {});
            else if (!selected)
                selected = std::move(authored);
        }
        if (selected)
        {
            const auto source = loader.sources->add(selected->first, selected->second);
            auto parsed = parse_engine_definition(selected->first, source, selected->second,
                                                  request.limits.maximum_diagnostics);
            loader.diagnostics.insert(loader.diagnostics.end(),
                                      parsed.diagnostics.begin(), parsed.diagnostics.end());
            loader.dependencies.push_back(selected->first);
            if (parsed.ok())
                engine = std::move(parsed.engine);
        }

        const auto tokens =
            detail::resolve_tokens(loader.registry, request.data, loader.diagnostics);
        if (entry != loader.documents.end() && !detail::has_errors(loader.diagnostics))
        {
            detail::DeferredStore deferred;
            auto generated = detail::render_document(
                loader.registry, request.limits, cancellation, loader.diagnostics,
                entry->second.nodes, request.data, tokens, engine ? &deferred : nullptr,
                request.subject);
            if (shell_path && !detail::has_errors(loader.diagnostics))
            {
                const auto shell = loader.documents.find(detail::path_key(*shell_path));
                if (shell != loader.documents.end())
                {
                    generated = detail::render_shell_document(
                        loader.registry, request.limits, cancellation, loader.diagnostics,
                        shell->second.nodes, request.data, tokens, generated,
                        engine ? &deferred : nullptr, request.subject);
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
            if (!request.subject)
            {
                generated = inline_css(std::move(generated), loader.diagnostics);
                embed_remote_images(generated, loader.diagnostics, request.image_fetcher,
                                    100 * 1024, 1024 * 1024,
                                    request.limits.maximum_html_bytes);
            }
            if (engine)
            {
                result.emir = detail::lower_deferred_to_emir(
                    generated, deferred, *engine, *loader.sources,
                    request.entry_path, loader.diagnostics,
                    request.subject);
                if (result.emir)
                {
                    result.output_kind = OutputKind::engine_template;
                    result.target = TargetIdentity{engine->name,
                                                   engine->canonical_path};
                }
            }
            else if (!request.subject)
            {
                auto findings = lint_html(generated,
                                          shell_path ? LintRole::shell : LintRole::content,
                                          {entry->second.source, 0, 0});
                loader.diagnostics.insert(loader.diagnostics.end(), findings.begin(),
                                          findings.end());
            }
            if (request.subject &&
                (generated.html.find('\r') != std::string::npos ||
                 generated.html.find('\n') != std::string::npos ||
                 generated.html.find('\0') != std::string::npos ||
                 generated.html.size() > 998))
                loader.issue("EM0850",
                             "A subject must be one header-safe line of at most 998 bytes.",
                             {entry->second.source, 0, 0});
            result.generated = std::move(generated);
        }

        auto snapshot = std::make_shared<DocumentSnapshot>();
        snapshot->sources = loader.sources;
        snapshot->entry = entry == loader.documents.end() ? 0 : entry->second.source;
        result.snapshot = std::move(snapshot);
        std::sort(loader.dependencies.begin(), loader.dependencies.end());
        loader.dependencies.erase(std::unique(loader.dependencies.begin(),
                                               loader.dependencies.end()),
                                  loader.dependencies.end());
        result.dependencies = std::move(loader.dependencies);
        result.diagnostics = std::move(loader.diagnostics);
        return result;
    }
} // namespace email_markup
