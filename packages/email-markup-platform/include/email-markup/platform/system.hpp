#pragma once

#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace email_markup::platform
{
    class System final
    {
    public:
        explicit System(std::string_view invocation);

        System(const System &) = delete;
        System &operator=(const System &) = delete;
        System(System &&) noexcept = default;
        System &operator=(System &&) noexcept = default;

        [[nodiscard]] const std::filesystem::path &executable_path() const noexcept;
        [[nodiscard]] std::filesystem::path path_from_file_uri(std::string_view uri) const;
        [[nodiscard]] std::string read_text_file(const std::filesystem::path &path) const;
        [[nodiscard]] std::string read_standard_input(
            std::size_t maximum_bytes = std::numeric_limits<std::size_t>::max()) const;
        void write_text_file_atomically(const std::filesystem::path &path,
                                        std::string_view contents) const;

    private:
        std::filesystem::path executable_path_;
    };
} // namespace email_markup::platform
