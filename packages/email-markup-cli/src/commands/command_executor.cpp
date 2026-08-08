#include "command_executor.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "compilation/compilation_session.hpp"
#include "diagnostics/diagnostic_reporter.hpp"
#include "email-markup/core/format.hpp"
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
        }
        throw std::invalid_argument("unknown command");
    }

    int CommandExecutor::compile(const Options &options) const
    {
        const CompilationSession session{options, assets_, system_};
        const auto result = session.compile(options.input);
        DiagnosticReporter{options.json}.print(result);
        if (!result.ok())
            return compilation_failed;
        system_.write_text_file_atomically(*options.output, result.generated.html);
        return success;
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
} // namespace email_markup::cli
