#pragma once

#include <string_view>
#include <vector>

#include "ell/core/ast.hpp"
#include "ell/core/diagnostic.hpp"

namespace ell {

struct ParseResult {
    Document document;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] ParseResult parse(SourceId source, std::string_view text,
                                std::size_t diagnostic_limit = 100);

}  // namespace ell
