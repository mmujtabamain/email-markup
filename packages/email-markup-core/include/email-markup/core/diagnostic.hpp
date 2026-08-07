#pragma once

#include <string>
#include <vector>

#include "email-markup/core/source.hpp"

namespace email_markup {

enum class Severity { note, warning, error };

struct RelatedLocation {
    SourceRange range;
    std::string message;
};

struct Diagnostic {
    std::string code;
    Severity severity{Severity::error};
    std::string message;
    SourceRange range;
    std::vector<RelatedLocation> related;
    std::string json_path;
};

[[nodiscard]] std::string_view severity_name(Severity severity) noexcept;

}  // namespace email_markup
