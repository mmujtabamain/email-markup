#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace email_markup::platform::detail
{
    [[nodiscard]] std::filesystem::path native_executable_path(std::string_view invocation);
    void normalize_file_uri_path(std::string &path);
    void replace_file(const std::filesystem::path &source,
                      const std::filesystem::path &destination);
} // namespace email_markup::platform::detail
