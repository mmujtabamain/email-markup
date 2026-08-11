#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "email-markup/core/emir.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"

namespace email_markup
{
    [[nodiscard]] std::string definition_preview_source(
        const ParseResult &parsed, std::string_view source);

    [[nodiscard]] std::string render_emir_sample(
        const EmirArtifact &artifact, const Json &data, std::size_t maximum);
}
