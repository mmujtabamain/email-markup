#pragma once

#include "commands/command_line.hpp"

namespace email_markup::platform
{
    class System;
}

namespace email_markup::runtime
{
    class Assets;
}

namespace email_markup::cli
{
    class CommandExecutor final
    {
    public:
        CommandExecutor(const platform::System &system, const runtime::Assets &assets) noexcept;

        [[nodiscard]] int execute(const Options &options) const;

    private:
        [[nodiscard]] int compile(const Options &options) const;
        [[nodiscard]] int check_or_lint(const Options &options) const;
        [[nodiscard]] int format(const Options &options) const;
        [[nodiscard]] int build(const Options &options) const;

        const platform::System &system_;
        const runtime::Assets &assets_;
    };
} // namespace email_markup::cli
