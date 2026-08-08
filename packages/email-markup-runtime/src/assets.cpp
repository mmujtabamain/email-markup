#include "email-markup/runtime/assets.hpp"

#include <cstdlib>

#include "email-markup/platform/system.hpp"

namespace email_markup::runtime
{
    Assets::Assets(const platform::System &system)
    {
        const auto binary = system.executable_path().parent_path();
        library_ = binary / "lib";
        schema_ = binary / "schema/em.schema.json";
        const auto installed = binary.parent_path() / "share/email-markup";
        if (!std::filesystem::exists(library_) && std::filesystem::exists(installed / "lib"))
        {
            library_ = installed / "lib";
            schema_ = installed / "schema/em.schema.json";
        }
        if (const char *value = std::getenv("EMAIL_MARKUP_LIB"))
            library_ = value;
    }

    const std::filesystem::path &Assets::library() const noexcept
    {
        return library_;
    }

    const std::filesystem::path &Assets::schema() const noexcept
    {
        return schema_;
    }
} // namespace email_markup::runtime
