#include "command_executor.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "compilation/compilation_session.hpp"
#include "diagnostics/diagnostic_reporter.hpp"
#include "email-markup/core/format.hpp"
#include "email-markup/core/emir.hpp"
#include "email-markup/core/images.hpp"
#include "email-markup/core/include.hpp"
#include "email-markup/core/lint.hpp"
#include "email-markup/core/version.hpp"
#include "email-markup/platform/system.hpp"
#include "email-markup/runtime/assets.hpp"

namespace email_markup::cli
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr int success = 0;
        constexpr int compilation_failed = 1;
        constexpr int protocol_failed = 2;
        constexpr std::string_view protocol = "email-markup.compile";
        constexpr int protocol_version = 1;

        Json protocol_error(const std::string &message)
        {
            return {{"protocol", protocol},
                    {"version", protocol_version},
                    {"compiler_version", email_markup::version()},
                    {"success", false},
                    {"html", ""},
                    {"dependencies", Json::array()},
                    {"diagnostics", Json::array({{{"code", "EMPROTO"},
                                                  {"severity", "error"},
                                                  {"message", message}}})}};
        }

        std::filesystem::path required_virtual_path(const Json &value,
                                                    const std::string_view field,
                                                    const std::string_view extension = ".em")
        {
            if (!value.is_string())
                throw std::invalid_argument(std::string{field} + " must be a string");
            const auto raw = value.get<std::string>();
            if (!raw.starts_with('/'))
                throw std::invalid_argument(std::string{field} +
                                            " must be an absolute virtual source path");
            const auto path = email_markup::normalize_virtual_path(raw);
            if (!path || path->extension() != extension)
                throw std::invalid_argument(std::string{field} +
                                            " has an invalid virtual source extension");
            return *path;
        }

        std::vector<std::filesystem::path> virtual_paths(const Json &request,
                                                         const std::string_view field,
                                                         const bool require_em)
        {
            const auto found = request.find(field);
            if (found == request.end())
                return {};
            if (!found->is_array())
                throw std::invalid_argument(std::string{field} + " must be an array");
            std::vector<std::filesystem::path> paths;
            for (const auto &value : *found)
            {
                if (!value.is_string())
                    throw std::invalid_argument(std::string{field} +
                                                " entries must be strings");
                const auto raw = value.get<std::string>();
                if (!raw.starts_with('/'))
                    throw std::invalid_argument(std::string{field} +
                                                " contains a relative virtual path");
                const auto path = email_markup::normalize_virtual_path(raw);
                if (!path || (require_em && path->extension() != ".em"))
                    throw std::invalid_argument(std::string{field} +
                                                " contains an invalid virtual path");
                paths.push_back(*path);
            }
            return paths;
        }
    }

    CommandExecutor::CommandExecutor(const platform::System &system,
                                     const runtime::Assets &assets) noexcept
        : system_(system), assets_(assets)
    {
    }

    int CommandExecutor::execute(const Options &options) const
    {
        switch (options.command)
        {
        case Command::version:
            fmt::print("emc {}\n", email_markup::version());
            return success;
        case Command::schema:
            fmt::print("{}", system_.read_text_file(assets_.schema()));
            return success;
        case Command::compile:
            return compile(options);
        case Command::check:
        case Command::lint:
            return check_or_lint(options);
        case Command::format:
            return format(options);
        case Command::build:
            return build(options);
        case Command::check_ir:
            return check_ir(options);
        case Command::inspect_ir:
            return inspect_ir(options);
        case Command::emit:
            return emit(options);
        }
        throw std::invalid_argument("unknown command");
    }

    int CommandExecutor::compile(const Options &options) const
    {
        if (options.request_stdin)
            return compile_request();
        const CompilationSession session{options, assets_, system_};
        const auto result = session.compile(options.input);
        DiagnosticReporter{options.json}.print(result);
        if (!result.ok())
            return compilation_failed;
        if (options.emit_ir)
        {
            if (!result.emir)
            {
                fmt::print(stderr,
                           "emc: --emit-ir requires an engine-template compilation\n");
                return compilation_failed;
            }
            system_.write_text_file_atomically(
                *options.output, email_markup::canonical_emir_json(*result.emir));
        }
        else
            system_.write_text_file_atomically(*options.output, result.generated.html);
        return success;
    }

    int CommandExecutor::compile_request() const
    {
        try
        {
            const auto input = system_.read_standard_input(1024 * 1024);
            if (input.size() > 1024 * 1024)
                throw std::invalid_argument("request exceeds the 1 MiB protocol limit");
            const auto envelope = Json::parse(input);
            if (!envelope.is_object())
                throw std::invalid_argument("request must be a JSON object");
            if (envelope.value("protocol", std::string{}) != protocol)
                throw std::invalid_argument("unsupported request protocol");
            if (envelope.value("version", 0) != protocol_version)
                throw std::invalid_argument("unsupported request protocol version");

            email_markup::CompilationRequest request;
            request.entry_path = required_virtual_path(envelope.at("entry_path"),
                                                       "entry_path");
            request.source = envelope.at("source").get<std::string>();
            request.include_directories = virtual_paths(envelope, "include_directories", false);
            request.allowed_roots = request.include_directories;
            request.allowed_roots.emplace_back("/");
            request.imports = virtual_paths(envelope, "imports", true);
            request.data = envelope.value("recipient", Json::object());
            if (!request.data.is_object())
                throw std::invalid_argument("recipient must be a JSON object");
            const auto output_context = envelope.value("output_context", std::string{"html"});
            if (output_context != "html" && output_context != "subject")
                throw std::invalid_argument("output_context must be html or subject");
            request.subject = output_context == "subject";

            std::vector<email_markup::ResolvedFile> files;
            const auto file_values = envelope.value("files", Json::array());
            if (!file_values.is_array())
                throw std::invalid_argument("files must be an array");
            for (const auto &file : file_values)
            {
                if (!file.is_object())
                    throw std::invalid_argument("files entries must be objects");
                const auto path = required_virtual_path(file.at("path"), "files.path");
                if (path == request.entry_path)
                    throw std::invalid_argument("files contains the entry_path");
                files.push_back({path, file.at("source").get<std::string>()});
            }
            if (const auto shell = envelope.find("shell"); shell != envelope.end())
            {
                if (!shell->is_object())
                    throw std::invalid_argument("shell must be an object");
                request.shell = required_virtual_path(shell->at("path"), "shell.path");
                files.push_back({*request.shell, shell->at("source").get<std::string>()});
            }
            if (const auto engine = envelope.find("engine"); engine != envelope.end())
            {
                if (!engine->is_object())
                    throw std::invalid_argument("engine must be an object");
                request.engine = required_virtual_path(engine->at("path"), "engine.path",
                                                       ".emt");
                files.push_back({*request.engine,
                                 engine->at("source").get<std::string>()});
            }

            email_markup::MemoryFileResolver resolver{std::move(files),
                                                      request.limits.maximum_source_bytes};
            request.image_fetcher =
                [this](const std::string_view url, const std::size_t maximum_bytes)
            {
                auto resource = system_.fetch_http(url, maximum_bytes);
                return email_markup::ImageResource{std::move(resource.media_type),
                                                   std::move(resource.bytes)};
            };
            const auto result = email_markup::compile(request, resolver);
            Json dependencies = Json::array();
            for (const auto &dependency : result.dependencies)
                dependencies.push_back(email_markup::portable_path_string(dependency));
            Json response{{"protocol", protocol},
                            {"version", protocol_version},
                            {"compiler_version", email_markup::version()},
                            {"success", result.ok()},
                            {"html", result.ok() ? result.generated.html : ""},
                            {"output_kind", result.output_kind == email_markup::OutputKind::engine_template
                                                ? "engine-template"
                                                : "final-html"},
                            {"dependencies", dependencies},
                            {"diagnostics", DiagnosticReporter::serialize(result)}};
            if (result.target)
                response["target"] = {{"name", result.target->name},
                                      {"engine", email_markup::portable_path_string(
                                                     result.target->engine)}};
            if (result.emir)
                response["emir"] = result.emir->value;
            fmt::print("{}\n", response.dump());
            return result.ok() ? success : compilation_failed;
        }
        catch (const std::exception &error)
        {
            fmt::print("{}\n", protocol_error(error.what()).dump());
            return protocol_failed;
        }
    }

    int CommandExecutor::check_or_lint(const Options &options) const
    {
        const bool lint_shell = options.command == Command::lint &&
                                options.lint_role == email_markup::LintRole::shell;
        const CompilationSession session{options, assets_, system_, lint_shell};
        auto result = session.compile(options.input);
        if (lint_shell && result.ok())
        {
            auto findings = email_markup::lint_html(result.generated.html,
                                                    email_markup::LintRole::shell,
                                                    {result.snapshot->entry, 0, 0});
            result.diagnostics.insert(result.diagnostics.end(), findings.begin(), findings.end());
        }
        DiagnosticReporter{options.json}.print(result);
        return result.ok() ? success : compilation_failed;
    }

    int CommandExecutor::format(const Options &options) const
    {
        const auto source = system_.read_text_file(options.input);
        const auto formatted = email_markup::format_source(source);
        if (options.write)
            system_.write_text_file_atomically(options.input, formatted);
        else
            fmt::print("{}", formatted);
        return success;
    }

    int CommandExecutor::build(const Options &options) const
    {
        const auto root = std::filesystem::absolute(options.input).lexically_normal();
        const CompilationSession session{options, assets_, system_};
        const auto &config = session.config();

        std::vector<std::filesystem::path> sources;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it{root, error}, end;
             !error && it != end; it.increment(error))
        {
            if (it->is_directory() &&
                (it->path() == config.output || it->path().filename() == ".git" ||
                 it->path().filename() == "external"))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file() || it->path().extension() != ".em")
                continue;
            const auto relative = std::filesystem::relative(it->path(), root, error);
            if (!relative.empty() &&
                (relative.begin()->string() == "lib" ||
                 relative.begin()->string() == "brand" ||
                 relative.begin()->string() == "components"))
                continue;
            sources.push_back(it->path());
        }

        std::sort(sources.begin(), sources.end());
        std::set<std::filesystem::path> outputs;
        bool failed = false;
        Json machine = Json::array();
        for (const auto &source : sources)
        {
            auto relative = std::filesystem::relative(source, root);
            relative.replace_extension(".html");
            const auto output = config.output / relative;
            if (!outputs.insert(output.lexically_normal()).second)
            {
                fmt::print(stderr, "duplicate output path: {}\n", output.string());
                failed = true;
                continue;
            }

            const auto result = session.compile(source);
            if (options.json)
            {
                machine.push_back({{"input", source.string()},
                                   {"output", output.string()},
                                   {"ok", result.ok()},
                                   {"diagnostics", DiagnosticReporter::serialize(result)}});
            }
            else
                DiagnosticReporter{false}.print(result);
            if (!result.ok())
            {
                failed = true;
                continue;
            }
            system_.write_text_file_atomically(output, result.generated.html);
        }

        if (options.json)
            fmt::print("{}\n", Json{{"ok", !failed}, {"files", machine}}.dump());
        return failed ? compilation_failed : success;
    }

    int CommandExecutor::check_ir(const Options &options) const
    {
        const auto parsed = email_markup::parse_emir(system_.read_text_file(options.input));
        for (const auto &diagnostic : parsed.diagnostics)
            fmt::print(stderr, "{}: {}\n", diagnostic.code, diagnostic.message);
        return parsed.ok() ? success : compilation_failed;
    }

    int CommandExecutor::inspect_ir(const Options &options) const
    {
        const auto parsed = email_markup::parse_emir(system_.read_text_file(options.input));
        for (const auto &diagnostic : parsed.diagnostics)
            fmt::print(stderr, "{}: {}\n", diagnostic.code, diagnostic.message);
        if (!parsed.ok())
            return compilation_failed;
        fmt::print("{}\n", email_markup::inspect_emir(*parsed.artifact).dump(2));
        return success;
    }

    int CommandExecutor::emit(const Options &options) const
    {
        const auto parsed = email_markup::parse_emir(system_.read_text_file(options.input));
        for (const auto &diagnostic : parsed.diagnostics)
            fmt::print(stderr, "{}: {}\n", diagnostic.code, diagnostic.message);
        if (!parsed.ok())
            return compilation_failed;
        const auto emitted = email_markup::emit_emir(*parsed.artifact, *options.target);
        for (const auto &diagnostic : emitted.diagnostics)
            fmt::print(stderr, "{}: {}\n", diagnostic.code, diagnostic.message);
        if (!emitted.ok())
            return compilation_failed;
        if (options.output)
            system_.write_text_file_atomically(*options.output, emitted.output);
        else
            fmt::print("{}", emitted.output);
        return success;
    }
} // namespace email_markup::cli
