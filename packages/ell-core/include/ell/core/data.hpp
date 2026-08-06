#pragma once

#include <string_view>

#include "ell/core/diagnostic.hpp"
#include "ell/core/expr.hpp"

namespace ell {

struct DataResult {
    Json data;
    std::vector<Diagnostic> diagnostics;
    bool ok{};
};

[[nodiscard]] DataResult parse_data(std::string_view text,
                                    std::size_t maximum_bytes);

}  // namespace ell
