#include "context.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>

#include "email-markup/core/lexer.hpp"

namespace email_markup::lsp::analysis
{
    bool directive_name_at(const std::string_view text, const std::size_t offset,
                           const std::string_view word)
    {
        if (word.empty() || !std::isupper(static_cast<unsigned char>(word.front())))
            return false;
        auto start = std::min(offset, text.size());
        while (start > 0 &&
               (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
                text[start - 1] == '_'))
            --start;
        return (start > 0 && text[start - 1] == '@') ||
               (start > 1 && text[start - 2] == '@' && text[start - 1] == '/');
    }

    PropsCompletionContext props_context_at(const std::string_view text,
                                            const std::size_t offset)
    {
        const auto cursor = std::min(offset, text.size());
        const auto open = text.rfind("@Props", cursor);
        if (open == std::string_view::npos)
            return PropsCompletionContext::none;
        const auto close = text.rfind("@/Props", cursor);
        if (close != std::string_view::npos && close > open)
            return PropsCompletionContext::none;

        const auto newline = text.rfind('\n', cursor == 0 ? 0 : cursor - 1);
        const auto line_start = newline == std::string_view::npos ? 0 : newline + 1;
        const auto line = text.substr(line_start, cursor - line_start);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return PropsCompletionContext::none;
        return line.find('=', colon + 1) == std::string_view::npos
                   ? PropsCompletionContext::type
                   : PropsCompletionContext::default_value;
    }

    bool slot_requirement_context_at(const std::string_view text, const std::size_t offset)
    {
        const auto cursor = std::min(offset, text.size());
        const auto open = text.rfind("@Slots", cursor);
        if (open == std::string_view::npos)
            return false;
        const auto close = text.rfind("@/Slots", cursor);
        if (close != std::string_view::npos && close > open)
            return false;

        const auto newline = text.rfind('\n', cursor == 0 ? 0 : cursor - 1);
        const auto line_start = newline == std::string_view::npos ? 0 : newline + 1;
        return text.substr(line_start, cursor - line_start).find(':') !=
               std::string_view::npos;
    }

    std::optional<std::string> containing_component_at(
        const std::string_view text, const std::size_t offset,
        const std::unordered_map<std::string, email_markup::ComponentDefinition> &definitions)
    {
        std::optional<std::size_t> definition_start;
        for (const auto &token : email_markup::lex(0, text).tokens)
        {
            if (token.range.start > offset)
                break;
            if (token.kind == email_markup::TokenKind::open &&
                token.name == "DefineComponent")
                definition_start = token.range.start;
            else if (token.kind == email_markup::TokenKind::close &&
                     token.name == "DefineComponent")
                definition_start.reset();
        }
        if (!definition_start)
            return std::nullopt;
        for (const auto &[name, definition] : definitions)
            if (definition.range.start == *definition_start)
                return name;
        return std::nullopt;
    }

    email_markup::SourceRange identifier_range(const std::string_view text,
                                               const email_markup::SourceRange declaration,
                                               const std::string_view name)
    {
        const auto start = text.find(name, declaration.start);
        if (start == std::string_view::npos || start >= declaration.end)
            return declaration;
        return {declaration.source, start, start + name.size()};
    }

    std::pair<std::size_t, std::size_t> component_span(
        const std::string_view text, const std::size_t definition_start)
    {
        bool inside = false;
        for (const auto &token : email_markup::lex(0, text).tokens)
        {
            if (!inside && token.kind == email_markup::TokenKind::open &&
                token.name == "DefineComponent" && token.range.start == definition_start)
            {
                inside = true;
            }
            else if (inside && token.kind == email_markup::TokenKind::close &&
                     token.name == "DefineComponent")
            {
                return {definition_start, token.range.end};
            }
        }
        return {definition_start, text.size()};
    }

    std::optional<InvocationContext> invocation_at(const std::string_view text,
                                                   const std::size_t offset)
    {
        std::optional<InvocationContext> result;
        for (std::size_t at = 0; at < offset; ++at)
        {
            if (text[at] != '@' || at + 1 >= offset ||
                !std::isupper(static_cast<unsigned char>(text[at + 1])))
                continue;
            std::size_t cursor = at + 2;
            while (cursor < offset &&
                   (std::isalnum(static_cast<unsigned char>(text[cursor])) ||
                    text[cursor] == '_'))
                ++cursor;
            const auto name = std::string{text.substr(at + 1, cursor - at - 1)};
            while (cursor < offset &&
                   std::isspace(static_cast<unsigned char>(text[cursor])))
                ++cursor;
            if (cursor >= offset || text[cursor] != '(')
                continue;

            const auto open = cursor++;
            std::size_t segment = cursor;
            int depth = 1;
            char quote = 0;
            for (; cursor < offset; ++cursor)
            {
                const auto character = text[cursor];
                if (quote)
                {
                    if (character == '\\' && cursor + 1 < offset)
                        ++cursor;
                    else if (character == quote)
                        quote = 0;
                    continue;
                }
                if (character == '"' || character == '\'')
                    quote = character;
                else if (character == '(')
                    ++depth;
                else if (character == ')' && --depth == 0)
                    break;
                else if (character == ',' && depth == 1)
                    segment = cursor + 1;
            }
            if (depth == 0)
                continue;

            InvocationContext context;
            context.name = name;
            std::size_t invocation_end = offset;
            auto forward_depth = depth;
            auto forward_quote = quote;
            for (; invocation_end < text.size(); ++invocation_end)
            {
                const auto character = text[invocation_end];
                if (forward_quote)
                {
                    if (character == '\\' && invocation_end + 1 < text.size())
                        ++invocation_end;
                    else if (character == forward_quote)
                        forward_quote = 0;
                }
                else if (character == '"' || character == '\'')
                    forward_quote = character;
                else if (character == '(')
                    ++forward_depth;
                else if (character == ')' && --forward_depth == 0)
                    break;
            }
            const auto arguments =
                std::string{text.substr(open + 1, invocation_end - open - 1)};
            static const std::regex named{R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*:)"};
            for (std::sregex_iterator it{arguments.begin(), arguments.end(), named}, end;
                 it != end; ++it)
                context.used_arguments.insert((*it)[1].str());

            const auto current = std::string{text.substr(segment, offset - segment)};
            std::size_t colon = std::string::npos;
            depth = 0;
            quote = 0;
            for (std::size_t index = 0; index < current.size(); ++index)
            {
                const auto character = current[index];
                if (quote)
                {
                    if (character == '\\' && index + 1 < current.size())
                        ++index;
                    else if (character == quote)
                        quote = 0;
                }
                else if (character == '"' || character == '\'')
                    quote = character;
                else if (character == '(' || character == '{' || character == '[')
                    ++depth;
                else if (character == ')' || character == '}' || character == ']')
                    --depth;
                else if (character == ':' && depth == 0)
                {
                    colon = index;
                    break;
                }
            }
            context.expects_name = colon == std::string::npos;
            if (context.expects_name)
            {
                const auto remaining =
                    std::string{text.substr(segment, invocation_end - segment)};
                std::smatch current_name;
                if (std::regex_search(remaining, current_name, named))
                    context.used_arguments.erase(current_name[1].str());
            }
            if (colon != std::string::npos)
            {
                const auto raw = current.substr(0, colon);
                const auto begin = raw.find_first_not_of(" \t\r\n");
                const auto end = raw.find_last_not_of(" \t\r\n");
                if (begin != std::string::npos)
                    context.current_argument = raw.substr(begin, end - begin + 1);
            }
            result = std::move(context);
        }
        return result;
    }

    bool interpolation_at(const std::string_view text, const std::size_t offset)
    {
        const auto open = text.rfind("@{", offset);
        if (open == std::string_view::npos)
            return false;
        const auto close = text.rfind('}', offset);
        return close == std::string_view::npos || close < open;
    }

    bool sigil_at(const std::string_view text, const std::size_t offset, bool &closing)
    {
        const auto line = text.rfind('\n', offset == 0 ? 0 : offset - 1);
        const auto start = line == std::string_view::npos ? 0 : line + 1;
        const auto prefix = std::string{text.substr(start, offset - start)};
        static const std::regex pattern{R"((?:^|\s)@(/?)[A-Za-z0-9_]*$)"};
        std::smatch match;
        if (!std::regex_search(prefix, match, pattern))
            return false;
        closing = match[1].str() == "/";
        return true;
    }
} // namespace email_markup::lsp::analysis
