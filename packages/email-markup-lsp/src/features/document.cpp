#include "server/server.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "email-markup/core/format.hpp"
#include "email-markup/core/lexer.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"
#include "text/positions.hpp"

namespace email_markup::lsp
{
    void Server::symbols(const Json &id, const Json &params)
    {
        const auto *open = document(params);
        if (!open)
        {
            respond(id, Json::array());
            return;
        }
        const auto parsed = email_markup::parse(0, open->text);
        Json result = Json::array();
        const auto add = [&](const std::string &name, const email_markup::SourceRange range,
                             const int kind)
        {
            result.push_back(
                {{"name", name},
                 {"kind", kind},
                 {"range",
                  {{"start", text::position_at(open->text, range.start)},
                   {"end", text::position_at(open->text, range.end)}}},
                 {"selectionRange",
                  {{"start", text::position_at(open->text, range.start)},
                   {"end", text::position_at(open->text, range.end)}}}});
        };
        for (const auto &[name, definition] : parsed.document.components)
            add(name, definition.range, 5);
        for (const auto &[name, definition] : parsed.document.styles)
            add(name, definition.range, 13);
        for (const auto &[name, definition] : parsed.document.tokens)
            add(name, definition.range, 14);
        respond(id, std::move(result));
    }

    void Server::folding(const Json &id, const Json &params)
    {
        const auto *open = document(params);
        if (!open)
        {
            respond(id, Json::array());
            return;
        }
        const auto lexed = email_markup::lex(0, open->text);
        std::vector<email_markup::Token> stack;
        Json result = Json::array();
        for (const auto &token : lexed.tokens)
        {
            if (token.kind == email_markup::TokenKind::open)
            {
                stack.push_back(token);
            }
            else if (token.kind == email_markup::TokenKind::close)
            {
                const auto found = std::find_if(
                    stack.rbegin(), stack.rend(),
                    [&](const auto &candidate)
                    { return candidate.name == token.name; });
                if (found == stack.rend())
                    continue;
                const auto start = text::position_at(open->text, found->range.start);
                const auto end = text::position_at(open->text, token.range.end);
                if (start.at("line") < end.at("line"))
                    result.push_back({{"startLine", start.at("line")},
                                      {"endLine", end.at("line")},
                                      {"kind", "region"}});
                stack.erase(std::next(found).base(), stack.end());
            }
        }
        respond(id, std::move(result));
    }

    void Server::signature(const Json &id, const Json &params)
    {
        const auto *open = document(params);
        if (!open)
        {
            respond(id, nullptr);
            return;
        }
        const auto &position = params.at("position");
        const auto offset = text::offset_at(open->text, position.value("line", 0),
                                            position.value("character", 0));
        const auto prefix = open->text.substr(0, offset);
        const auto at = prefix.find_last_of('@');
        if (at == std::string::npos)
        {
            respond(id, nullptr);
            return;
        }
        const auto paren = prefix.find('(', at);
        if (paren == std::string::npos)
        {
            respond(id, nullptr);
            return;
        }
        const auto name = text::word_at(prefix, at + 1);
        const auto definitions = workspace_.metadata(*open);
        const auto found = definitions.find(name);
        if (found == definitions.end())
        {
            respond(id, nullptr);
            return;
        }

        std::string label = "@" + name + "(";
        Json parameters = Json::array();
        for (std::size_t index = 0; index < found->second.props.size(); ++index)
        {
            const auto &prop = found->second.props[index];
            if (index)
                label += ", ";
            const auto part = format_declaration(prop);
            label += part;
            parameters.push_back({{"label", part}});
        }
        label += ")";
        const auto commas = static_cast<int>(std::count(
            prefix.begin() + static_cast<std::ptrdiff_t>(paren), prefix.end(), ','));
        respond(id,
                {{"signatures",
                  Json::array({{{"label", label}, {"parameters", parameters}}})},
                 {"activeSignature", 0},
                 {"activeParameter", commas}});
    }

    void Server::formatting(const Json &id, const Json &params)
    {
        const auto *open = document(params);
        if (!open)
        {
            respond(id, Json::array());
            return;
        }
        const auto formatted = email_markup::format_source(open->text);
        if (formatted == open->text)
        {
            respond(id, Json::array());
            return;
        }
        respond(id,
                Json::array({{{"range",
                               {{"start", {{"line", 0}, {"character", 0}}},
                                {"end", text::position_at(open->text, open->text.size())}}},
                              {"newText", formatted}}}));
    }
} // namespace email_markup::lsp
