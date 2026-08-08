#include "../native.hpp"

#include <system_error>

namespace email_markup::platform::detail
{
    std::filesystem::path native_executable_path(const std::string_view invocation)
    {
        std::error_code error;
        const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path{invocation} : executable;
    }

    void normalize_file_uri_path(std::string &)
    {
    }

    void replace_file(const std::filesystem::path &source,
                      const std::filesystem::path &destination)
    {
        std::filesystem::rename(source, destination);
    }
} // namespace email_markup::platform::detail
