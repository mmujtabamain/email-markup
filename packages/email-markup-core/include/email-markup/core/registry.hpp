#pragma once

#include <string>
#include <unordered_map>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/diagnostic.hpp"

namespace email_markup {

struct Registry {
    std::unordered_map<std::string, ComponentDefinition> components;
    std::unordered_map<std::string, StyleDefinition> styles;
    std::unordered_map<std::string, TokenDefinition> tokens;
    std::vector<MediaDefinition> media;
};

void merge_registry(Registry& target, const Document& source,
                    bool entry_document,
                    std::vector<Diagnostic>& diagnostics);

}  // namespace email_markup
