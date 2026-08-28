#include "commands/command_line.hpp"

#include <stdexcept>
#include <string_view>

#include <fmt/core.h>

#include "email-markup/core/version.hpp"

namespace email_markup::cli
{
    namespace
    {
        Command parse_command(const std::string_view command)
        {
            if (command == "--version")
                return Command::version;
            if (command == "schema")
                return Command::schema;
            if (command == "compile")
                return Command::compile;
            if (command == "check")
                return Command::check;
            if (command == "lint")
                return Command::lint;
            if (command == "fmt")
                return Command::format;
            if (command == "build")
                return Command::build;
            if (command == "check-ir")
                return Command::check_ir;
            if (command == "inspect-ir")
                return Command::inspect_ir;
            if (command == "emit")
                return Command::emit;
            throw std::invalid_argument("unknown command " + std::string{command});
        }

        bool requires_input(const Command command)
        {
            return command != Command::version && command != Command::schema &&
                   command != Command::build;
        }
    } // namespace

    Options CommandLineParser::parse(const int argc, const char *const argv[])
    {
        if (argc < 2)
            throw std::invalid_argument("missing command");

        Options options;
        options.command = parse_command(argv[1]);
        if (options.command == Command::version || options.command == Command::schema)
            return options;

        int index = 2;
        if (index < argc && argv[index][0] != '-')
            options.input = argv[index++];
        if (options.command == Command::build && options.input.empty())
            options.input = ".";

        while (index < argc)
        {
            const std::string_view argument = argv[index++];
            const auto value = [&]() -> std::string
            {
                if (index >= argc)
                    throw std::invalid_argument(std::string{argument} + " requires a value");
                return argv[index++];
            };

            if (!argument.starts_with('-') && options.input.empty())
                options.input = argument;
            else if (argument == "-o")
                options.output = value();
            else if (argument == "-I")
                options.includes.emplace_back(value());
            else if (argument == "--import")
                options.imports.emplace_back(value());
            else if (argument == "--shell")
                options.shell = value();
            else if (argument == "--engine")
                options.engine = value();
            else if (argument == "--target")
                options.target = value();
            else if (argument == "--emit-ir")
                options.emit_ir = true;
            else if (argument == "--subject")
                options.subject = true;
            else if (argument == "--data-json")
                options.data_json = value();
            else if (argument == "--data-file")
                options.data_file = value();
            else if (argument == "--data-stdin")
                options.data_stdin = true;
            else if (argument == "--request-stdin")
                options.request_stdin = true;
            else if (argument == "--json")
                options.json = true;
            else if (argument == "--write")
                options.write = true;
            else if (argument == "--role")
            {
                const auto role = value();
                if (role == "content")
                    options.lint_role = email_markup::LintRole::content;
                else if (role == "shell")
                    options.lint_role = email_markup::LintRole::shell;
                else
                    throw std::invalid_argument("--role must be content or shell");
            }
            else
                throw std::invalid_argument("unknown option " + std::string{argument});
        }

        const int data_sources = static_cast<int>(options.data_json.has_value()) +
                                 static_cast<int>(options.data_file.has_value()) +
                                 static_cast<int>(options.data_stdin);
        if (data_sources > 1)
            throw std::invalid_argument(
                "--data-json, --data-file, and --data-stdin are mutually exclusive");
        if (options.request_stdin &&
            (options.command != Command::compile || !options.input.empty() || options.output ||
             !options.includes.empty() || !options.imports.empty() || options.shell ||
             options.data_json || options.data_file || options.data_stdin || options.json))
            throw std::invalid_argument(
                "--request-stdin is exclusive to `emc compile --request-stdin`");
        if (requires_input(options.command) && options.input.empty() && !options.request_stdin)
            throw std::invalid_argument("command requires an input file");
        if (options.command == Command::compile && !options.output && !options.request_stdin)
            throw std::invalid_argument("compile requires -o <file>");
        if (options.emit_ir && options.command != Command::compile)
            throw std::invalid_argument("--emit-ir is valid only with compile");
        if (options.command == Command::emit && !options.target)
            throw std::invalid_argument("emit requires --target <name>");
        return options;
    }

    void CommandLineParser::print_usage()
    {
        fmt::print(stderr,
                   "Email Markup compiler {}\n\n"
                   "Usage:\n"
                   "  emc build [dir] [options]\n"
                   "  emc compile <file> -o <file> [options]\n"
                   "  emc compile --request-stdin\n"
                   "  emc check <file> [options]\n"
                   "  emc lint <file> [--role content|shell] [options]\n"
                   "  emc fmt <file> [--write]\n"
                   "  emc check-ir <file>\n"
                   "  emc inspect-ir <file>\n"
                   "  emc emit --target django <file> [-o <file>]\n"
                   "  emc schema\n"
                   "  emc --version\n\n"
                   "Options:\n"
                   "  -I <dir>            Add an include and allowed-root directory\n"
                   "  --import <file>     Pre-import an Email Markup definition file\n"
                   "  --data-json <json>  Compile with a JSON object\n"
                   "  --data-stdin        Read the JSON object from standard input\n"
                   "  --data-file <file>  Read the JSON object from a file\n"
                   "  --request-stdin      Compile a versioned virtual-source JSON request\n"
                   "  --shell <file>      Select the final HTML shell\n"
                   "  --engine <file>     Select a deferred .emt engine\n"
                   "  --emit-ir           Write canonical EMIR v1 instead of target output\n"
                   "  --subject           Compile a text-only header-safe subject\n"
                   "  --target <name>     Select the target for `emc emit`\n"
                   "  --json              Emit machine-readable diagnostics\n",
                   email_markup::version());
    }
} // namespace email_markup::cli
