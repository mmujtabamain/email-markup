#include "workspace.hpp"

#include <optional>
#include <string_view>
#include <utility>

#include "email-markup/core/parser.hpp"
#include "email-markup/core/engine.hpp"
#include "email-markup/platform/system.hpp"
#include "text/positions.hpp"

namespace email_markup::lsp
{
    Workspace::Workspace(const platform::System &system, const runtime::Assets &assets)
        : system_(system), assets_(assets)
    {
        load_library_metadata();
    }

    void Workspace::set_roots(std::vector<std::filesystem::path> roots)
    {
        roots_ = std::move(roots);
    }

    std::string Workspace::read_optional(const std::filesystem::path &path) const
    {
        try
        {
            return system_.read_text_file(path);
        }
        catch (...)
        {
            return {};
        }
    }

    std::optional<std::filesystem::path> Workspace::project_config(
        const std::filesystem::path &file) const
    {
        auto current = file.parent_path();
        std::filesystem::path boundary;
        for (const auto &root : roots_)
        {
            const auto relative = current.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..")
            {
                boundary = root;
                break;
            }
        }
        while (!current.empty())
        {
            const auto candidate = current / "em.json";
            if (std::filesystem::exists(candidate))
                return candidate;
            if (!boundary.empty() && current == boundary)
                break;
            const auto parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }
        return std::nullopt;
    }

    std::filesystem::path Workspace::expand(std::string value,
                                            const std::filesystem::path &base) const
    {
        constexpr std::string_view token{"${EMAIL_MARKUP_LIB}"};
        if (const auto position = value.find(token); position != std::string::npos)
            value.replace(position, token.size(), assets_.library().string());
        std::filesystem::path path{value};
        return path.is_absolute() ? path : base / path;
    }

    email_markup::CompilationRequest Workspace::compilation_request(
        const OpenDocument &document, const Json *preview_data) const
    {
        email_markup::CompilationRequest request;
        request.entry_path = document.path;
        request.source = document.text;
        request.data = preview_data ? *preview_data : Json::object();
        request.include_directories = {assets_.library()};
        request.allowed_roots = {document.path.parent_path(), assets_.library()};
        request.imports = {assets_.library() / "builtins.em"};

        if (const auto config_path = project_config(document.path))
        {
            try
            {
                const auto config = Json::parse(system_.read_text_file(*config_path));
                const auto root = config_path->parent_path();
                request.include_directories.clear();
                request.imports.clear();
                for (const auto &value : config.value("include", Json::array()))
                    request.include_directories.push_back(
                        expand(value.get<std::string>(), root));
                for (const auto &value : config.value("imports", Json::array()))
                    request.imports.push_back(expand(value.get<std::string>(), root));
                request.allowed_roots = request.include_directories;
                request.allowed_roots.push_back(root);
                if (config.contains("shell"))
                    request.shell = expand(config.at("shell").get<std::string>(), root);
                if (config.contains("engine"))
                    request.engine = expand(config.at("engine").get<std::string>(), root);
                if (!preview_data && config.contains("data"))
                {
                    const auto data_path = expand(config.at("data").get<std::string>(), root);
                    const auto raw = read_optional(data_path);
                    if (!raw.empty())
                        request.data = Json::parse(raw);
                }
            }
            catch (...)
            {
            }
        }
        return request;
    }

    email_markup::CompilationResult Workspace::compile(const OpenDocument &document,
                                                       const Json *preview_data) const
    {
        if (document.path.extension() == ".emt")
        {
            email_markup::CompilationResult result;
            auto sources = std::make_shared<email_markup::SourceManager>();
            const auto source = sources->add(document.path, document.text);
            auto parsed = email_markup::parse_engine_definition(
                document.path, source, document.text);
            result.diagnostics = std::move(parsed.diagnostics);
            auto snapshot = std::make_shared<email_markup::DocumentSnapshot>();
            snapshot->sources = std::move(sources);
            snapshot->entry = source;
            result.snapshot = std::move(snapshot);
            return result;
        }
        auto request = compilation_request(document, preview_data);
        email_markup::DiskFileResolver resolver{request.limits.maximum_source_bytes};
        return email_markup::compile(request, resolver);
    }

    Json Workspace::diagnostics(const email_markup::CompilationResult &result,
                                const OpenDocument &document) const
    {
        Json output = Json::array();
        for (const auto &diagnostic : result.diagnostics)
        {
            if (!result.snapshot || !result.snapshot->sources ||
                diagnostic.range.source >= result.snapshot->sources->size())
                continue;
            const auto &source = result.snapshot->sources->get(diagnostic.range.source);
            if (source.path.lexically_normal() != document.path.lexically_normal())
                continue;
            output.push_back(
                {{"range",
                  {{"start", text::position_at(source.text, diagnostic.range.start)},
                   {"end", text::position_at(source.text, diagnostic.range.end)}}},
                 {"severity", diagnostic.severity == email_markup::Severity::error
                                  ? 1
                              : diagnostic.severity == email_markup::Severity::warning ? 2
                                                                                       : 3},
                 {"code", diagnostic.code},
                 {"source", "email-markup"},
                 {"message", diagnostic.message}});
        }
        return output;
    }

    void Workspace::load_library_metadata()
    {
        const auto source = read_optional(assets_.library() / "builtins.em");
        if (source.empty())
            return;
        const auto parsed = email_markup::parse(0, source);
        for (const auto &[name, definition] : parsed.document.components)
            components_[name] = definition;
        for (const auto &[name, definition] : parsed.document.styles)
            styles_.insert(name);
        for (const auto &[name, definition] : parsed.document.tokens)
            tokens_.insert(name);
    }

    std::unordered_map<std::string, email_markup::ComponentDefinition> Workspace::metadata(
        const OpenDocument &document) const
    {
        auto result = components_;
        const auto parsed = email_markup::parse(0, document.text);
        for (const auto &[name, definition] : parsed.document.components)
            result[name] = definition;
        return result;
    }

    const std::set<std::string> &Workspace::styles() const noexcept
    {
        return styles_;
    }

    const std::set<std::string> &Workspace::tokens() const noexcept
    {
        return tokens_;
    }
} // namespace email_markup::lsp
