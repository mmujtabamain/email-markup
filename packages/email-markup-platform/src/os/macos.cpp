#include "../native.hpp"

#include <cstdint>
#include <system_error>
#include <vector>

#include <mach-o/dyld.h>

namespace email_markup::platform::detail
{
    std::filesystem::path native_executable_path(const std::string_view invocation)
    {
        std::vector<char> buffer(1024);
        auto size = static_cast<std::uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            buffer.resize(size);
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
                return invocation;
        }
        return buffer.data();
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
