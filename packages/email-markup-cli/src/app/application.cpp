#include "application.hpp"

#include <exception>
#include <stdexcept>

#include <fmt/core.h>

#include "commands/command_executor.hpp"
#include "commands/command_line.hpp"
#include "email-markup/platform/system.hpp"
#include "email-markup/runtime/assets.hpp"

namespace email_markup::cli
{
    namespace
    {
        constexpr int usage_failed = 2;
    }

    Application::Application(const int argc, const char *const argv[]) noexcept
        : argc_(argc), argv_(argv)
    {
    }

    int Application::run() const noexcept
    {
        try
        {
            const auto options = CommandLineParser::parse(argc_, argv_);
            platform::System system{argc_ > 0 ? argv_[0] : "emc"};
            const runtime::Assets assets{system};
            return CommandExecutor{system, assets}.execute(options);
        }
        catch (const std::invalid_argument &error)
        {
            fmt::print(stderr, "emc: {}\n\n", error.what());
            CommandLineParser::print_usage();
            return usage_failed;
        }
        catch (const std::exception &error)
        {
            fmt::print(stderr, "emc: {}\n", error.what());
            return usage_failed;
        }
    }
} // namespace email_markup::cli
