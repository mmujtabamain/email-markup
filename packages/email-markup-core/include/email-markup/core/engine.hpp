#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/diagnostic.hpp"

namespace email_markup
{

    struct EngineMacro
    {
        std::string name;
        bool bare{};
        std::vector<Declaration> parameters;
        std::vector<SlotDeclaration> slots;
        std::string template_source;
        SourceRange range;
    };

    struct EngineDefinition
    {
        std::string name;
        std::filesystem::path canonical_path;
        std::optional<EngineMacro> bare;
        std::unordered_map<std::string, EngineMacro> macros;
    };

    struct EngineParseResult
    {
        EngineDefinition engine;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool ok() const noexcept;
    };

    [[nodiscard]] EngineParseResult parse_engine_definition(
        const std::filesystem::path &canonical_path, SourceId source,
        std::string_view contents, std::size_t diagnostic_limit = 100);

} // namespace email_markup
