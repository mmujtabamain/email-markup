#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/expr.hpp"

namespace email_markup
{

    enum class DeclarationContext
    {
        component_prop,
        deferred_parameter
    };

    [[nodiscard]] std::vector<Declaration> parse_declarations(
        std::string_view text, SourceRange range, DeclarationContext context,
        std::vector<Diagnostic> &diagnostics);
    [[nodiscard]] std::vector<PropDeclaration> parse_prop_declarations(
        std::string_view text, SourceRange range, std::vector<Diagnostic> &diagnostics);
    [[nodiscard]] std::vector<SlotDeclaration> parse_slot_declarations(
        std::string_view text, SourceRange range, std::vector<Diagnostic> &diagnostics);
    [[nodiscard]] bool validate_prop(const PropDeclaration &declaration,
                                     const Json &value,
                                     std::vector<Diagnostic> &diagnostics,
                                     SourceRange value_range);
    [[nodiscard]] std::string format_declaration(const Declaration &declaration);

} // namespace email_markup
