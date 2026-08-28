#include "email-markup/core/diagnostic.hpp"

namespace email_markup
{

    std::string_view severity_name(const Severity severity) noexcept
    {
        switch (severity)
        {
        case Severity::note:
            return "note";
        case Severity::warning:
            return "warning";
        case Severity::error:
            return "error";
        }
        return "error";
    }

} // namespace email_markup
