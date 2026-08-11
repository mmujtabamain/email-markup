#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "email-markup/core/lint.hpp"

namespace email_markup::cli
{
    enum class Command
    {
        version,
        schema,
        compile,
        check,
        lint,
        format,
        build,
        check_ir,
        inspect_ir,
        emit,
    };

    struct Options
    {
        Command command{Command::version};
        std::filesystem::path input;
        std::optional<std::filesystem::path> output;
        std::vector<std::filesystem::path> includes;
        std::vector<std::filesystem::path> imports;
        std::optional<std::filesystem::path> shell;
        std::optional<std::filesystem::path> engine;
        std::optional<std::string> target;
        std::optional<std::string> data_json;
        std::optional<std::filesystem::path> data_file;
        bool data_stdin{};
        bool request_stdin{};
        bool json{};
        bool write{};
        bool emit_ir{};
        bool subject{};
        email_markup::LintRole lint_role{email_markup::LintRole::content};
    };

    class CommandLineParser final
    {
    public:
        CommandLineParser() = delete;

        [[nodiscard]] static Options parse(int argc, const char *const argv[]);
        static void print_usage();
    };
} // namespace email_markup::cli
