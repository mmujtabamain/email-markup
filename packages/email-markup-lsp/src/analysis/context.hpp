#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "email-markup/core/ast.hpp"

namespace email_markup::lsp::analysis
{
    struct InvocationContext
    {
        std::string name;
        std::string current_argument;
        std::set<std::string> used_arguments;
        bool expects_name{};
    };

    enum class PropsCompletionContext
    {
        none,
        type,
        default_value,
    };

    [[nodiscard]] bool directive_name_at(std::string_view text, std::size_t offset,
                                         std::string_view word);
    [[nodiscard]] PropsCompletionContext props_context_at(std::string_view text,
                                                          std::size_t offset);
    [[nodiscard]] bool slot_requirement_context_at(std::string_view text,
                                                   std::size_t offset);
    [[nodiscard]] std::optional<std::string> containing_component_at(
        std::string_view text, std::size_t offset,
        const std::unordered_map<std::string, email_markup::ComponentDefinition> &definitions);
    [[nodiscard]] email_markup::SourceRange identifier_range(
        std::string_view text, email_markup::SourceRange declaration, std::string_view name);
    [[nodiscard]] std::pair<std::size_t, std::size_t> component_span(
        std::string_view text, std::size_t definition_start);
    [[nodiscard]] std::optional<InvocationContext> invocation_at(std::string_view text,
                                                                 std::size_t offset);
    [[nodiscard]] bool interpolation_at(std::string_view text, std::size_t offset);
    [[nodiscard]] bool sigil_at(std::string_view text, std::size_t offset, bool &closing);
} // namespace email_markup::lsp::analysis
