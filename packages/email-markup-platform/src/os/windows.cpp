#include "../native.hpp"

#include <stdexcept>
#include <system_error>
#include <vector>

#include <windows.h>

namespace email_markup::platform::detail
{
    std::filesystem::path native_executable_path(const std::string_view invocation)
    {
        std::vector<wchar_t> buffer(1024);
        while (true)
        {
            const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                                   static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return invocation;
            if (length < buffer.size() - 1)
                return std::wstring_view{buffer.data(), length};
            buffer.resize(buffer.size() * 2);
        }
    }

    void normalize_file_uri_path(std::string &path)
    {
        if (path.size() > 2 && path[0] == '/' && path[2] == ':')
            path.erase(0, 1);
    }

    void replace_file(const std::filesystem::path &source,
                      const std::filesystem::path &destination)
    {
        if (!MoveFileExW(source.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace " + destination.string());
    }
} // namespace email_markup::platform::detail
