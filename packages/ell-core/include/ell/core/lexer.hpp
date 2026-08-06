#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ell/core/diagnostic.hpp"

namespace ell {

enum class TokenKind { text, expression, open, self_closing, close, end, invalid };

struct Token {
    TokenKind kind{TokenKind::invalid};
    std::string name;
    std::string parameters;
    std::string text;
    SourceRange range;
};

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] LexResult lex(SourceId source, std::string_view text,
                            std::size_t diagnostic_limit = 100);

}  // namespace ell
