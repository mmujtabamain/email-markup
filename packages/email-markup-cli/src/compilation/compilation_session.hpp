#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "email-markup/core/data.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/runtime/assets.hpp"

#include "commands/command_line.hpp"

namespace email_markup::platform
{
    class System;
}

namespace email_markup::cli
{
    struct ProjectConfig
    {
        std::filesystem::path path;
        std::vector<std::filesystem::path> includes;
        std::vector<std::filesystem::path> imports;
        std::optional<std::filesystem::path> data;
        std::optional<std::filesystem::path> shell;
        std::optional<std::filesystem::path> engine;
        std::filesystem::path output{"build"};
    };

    class CompilationSession final
    {
    public:
        CompilationSession(const Options &options, const runtime::Assets &assets,
                           const platform::System &system, bool ignore_shell = false);

        [[nodiscard]] const ProjectConfig &config() const noexcept;
        [[nodiscard]] email_markup::CompilationResult compile(
            const std::filesystem::path &input) const;

    private:
        [[nodiscard]] email_markup::CompilationRequest request_for(
            const std::filesystem::path &input) const;

        const Options &options_;
        const platform::System &system_;
        ProjectConfig config_;
        email_markup::Json data_;
    };
} // namespace email_markup::cli
