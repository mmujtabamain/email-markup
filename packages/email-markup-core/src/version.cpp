#include "email-markup/core/version.hpp"

namespace email_markup
{

    std::string_view version() noexcept
    {
        return EMAIL_MARKUP_VERSION;
    }

} // namespace email_markup
