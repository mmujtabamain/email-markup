#pragma once

#include <filesystem>

namespace email_markup::platform
{
    class System;
}

namespace email_markup::runtime
{
    class Assets final
    {
    public:
        explicit Assets(const platform::System &system);

        [[nodiscard]] const std::filesystem::path &library() const noexcept;
        [[nodiscard]] const std::filesystem::path &schema() const noexcept;

    private:
        std::filesystem::path library_;
        std::filesystem::path schema_;
    };
} // namespace email_markup::runtime
