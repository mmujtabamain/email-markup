#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "email-markup/core/diagnostic.hpp"

namespace email_markup
{

    inline constexpr int emir_version = 1;
    inline constexpr std::string_view emir_format = "email-markup-ir";

    struct EmirArtifact
    {
        nlohmann::json value;
    };

    struct EmirParseResult
    {
        std::optional<EmirArtifact> artifact;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const noexcept;
    };

    struct EmirEmissionResult
    {
        std::string output;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const noexcept;
    };

    [[nodiscard]] std::string canonical_emir_json(const EmirArtifact &artifact);
    [[nodiscard]] EmirParseResult parse_emir(std::string_view source);
    [[nodiscard]] nlohmann::json inspect_emir(const EmirArtifact &artifact);
    [[nodiscard]] EmirEmissionResult emit_emir(const EmirArtifact &artifact,
                                               std::string_view target);

} // namespace email_markup
