#include "compilation_session.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "email-markup/platform/system.hpp"
#include "email-markup/core/images.hpp"

namespace email_markup::cli
{
    namespace
    {
        using Json = nlohmann::json;

        std::string replace_all(std::string value, const std::string_view needle,
                                const std::string_view replacement)
        {
            std::size_t position = 0;
            while ((position = value.find(needle, position)) != std::string::npos)
            {
                value.replace(position, needle.size(), replacement);
                position += replacement.size();
            }
            return value;
        }

        std::optional<std::filesystem::path> find_config(std::filesystem::path from)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(from, error))
                from = from.parent_path();
            from = std::filesystem::absolute(from, error);
            while (!from.empty())
            {
                const auto candidate = from / "em.json";
                if (std::filesystem::is_regular_file(candidate, error))
                    return candidate;
                const auto parent = from.parent_path();
                if (parent == from)
                    break;
                from = parent;
            }
            return std::nullopt;
        }

        std::filesystem::path expand_path(const std::string &raw,
                                          const std::filesystem::path &base,
                                          const runtime::Assets &assets)
        {
            auto expanded = replace_all(raw, "${EMAIL_MARKUP_LIB}", assets.library().string());
            std::filesystem::path path{expanded};
            if (path.is_relative())
                path = base / path;
            return path.lexically_normal();
        }

        ProjectConfig load_config(const std::filesystem::path &input,
                                  const runtime::Assets &assets,
                                  const platform::System &system)
        {
            ProjectConfig config;
            if (const auto found = find_config(input))
            {
                config.path = *found;
                const auto root = found->parent_path();
                const auto json = Json::parse(system.read_text_file(*found));
                if (!json.is_object())
                    throw std::runtime_error("em.json must contain an object");
                for (const auto &value : json.value("include", Json::array()))
                    config.includes.push_back(
                        expand_path(value.get<std::string>(), root, assets));
                for (const auto &value : json.value("imports", Json::array()))
                    config.imports.push_back(
                        expand_path(value.get<std::string>(), root, assets));
                if (json.contains("data"))
                    config.data = expand_path(json.at("data").get<std::string>(), root, assets);
                if (json.contains("shell"))
                    config.shell = expand_path(json.at("shell").get<std::string>(), root, assets);
                if (json.contains("engine"))
                    config.engine = expand_path(json.at("engine").get<std::string>(), root, assets);
                if (json.contains("out"))
                    config.output = expand_path(json.at("out").get<std::string>(), root, assets);
                else
                    config.output = root / "build";
                return config;
            }

            config.path = std::filesystem::absolute(input).parent_path() / "em.json";
            config.includes = {assets.library()};
            config.imports = {assets.library() / "builtins.em"};
            config.output = std::filesystem::absolute(input).parent_path() / "build";
            return config;
        }

        email_markup::Json load_data(const Options &options, const ProjectConfig &config,
                                     const platform::System &system)
        {
            std::optional<std::string> raw;
            if (options.data_json)
                raw = *options.data_json;
            else if (options.data_stdin)
                raw = system.read_standard_input();
            else if (options.data_file)
                raw = system.read_text_file(*options.data_file);
            else if (config.data && std::filesystem::exists(*config.data))
                raw = system.read_text_file(*config.data);
            if (!raw)
                return email_markup::Json::object();

            auto result = email_markup::parse_data(*raw, 1024 * 1024);
            if (!result.ok)
                throw std::runtime_error(result.diagnostics.front().message);
            return std::move(result.data);
        }
    } // namespace

    CompilationSession::CompilationSession(const Options &options,
                                           const runtime::Assets &assets,
                                           const platform::System &system,
                                           const bool ignore_shell)
        : options_(options), system_(system), config_(load_config(options.input, assets, system))
    {
        if (ignore_shell)
            config_.shell.reset();
        data_ = load_data(options_, config_, system_);
    }

    const ProjectConfig &CompilationSession::config() const noexcept
    {
        return config_;
    }

    email_markup::CompilationRequest CompilationSession::request_for(
        const std::filesystem::path &input) const
    {
        email_markup::CompilationRequest request;
        request.entry_path = std::filesystem::absolute(input).lexically_normal();
        request.source = system_.read_text_file(request.entry_path);
        request.data = data_;
        request.include_directories = config_.includes;
        request.include_directories.insert(request.include_directories.end(),
                                           options_.includes.begin(), options_.includes.end());
        request.allowed_roots = request.include_directories;
        request.allowed_roots.push_back(config_.path.parent_path());
        request.imports = config_.imports;
        request.imports.insert(request.imports.end(), options_.imports.begin(),
                               options_.imports.end());
        request.shell = options_.subject
                            ? std::nullopt
                            : (options_.shell ? options_.shell : config_.shell);
        request.engine = options_.engine
                             ? std::optional<std::filesystem::path>{
                                   std::filesystem::absolute(*options_.engine).lexically_normal()}
                             : config_.engine;
        request.subject = options_.subject;
        if (options_.command == Command::compile || options_.command == Command::build)
        {
            request.image_fetcher =
                [this](const std::string_view url, const std::size_t maximum_bytes)
            {
                auto resource = system_.fetch_http(url, maximum_bytes);
                return email_markup::ImageResource{std::move(resource.media_type),
                                                   std::move(resource.bytes)};
            };
        }
        return request;
    }

    email_markup::CompilationResult CompilationSession::compile(
        const std::filesystem::path &input) const
    {
        auto request = request_for(input);
        email_markup::DiskFileResolver resolver{request.limits.maximum_source_bytes};
        return email_markup::compile(request, resolver);
    }
} // namespace email_markup::cli
