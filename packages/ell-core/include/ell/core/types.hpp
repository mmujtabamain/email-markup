#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "ell/core/ast.hpp"
#include "ell/core/diagnostic.hpp"
#include "ell/core/expr.hpp"

namespace ell {

[[nodiscard]] std::vector<PropDeclaration> parse_prop_declarations(
    std::string_view text, SourceRange range, std::vector<Diagnostic>& diagnostics);
[[nodiscard]] std::vector<SlotDeclaration> parse_slot_declarations(
    std::string_view text, SourceRange range, std::vector<Diagnostic>& diagnostics);
[[nodiscard]] bool validate_prop(const PropDeclaration& declaration,
                                 const Json& value,
                                 std::vector<Diagnostic>& diagnostics,
                                 SourceRange value_range);

}  // namespace ell
