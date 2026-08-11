#include "server/server.hpp"

#include <algorithm>
#include <regex>
#include <string>

#include "analysis/context.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"
#include "text/positions.hpp"

namespace email_markup::lsp
{
    void Server::hover(const Json &id, const Json &params)
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
        const auto word = text::word_at(open->text, offset);
        if (!analysis::directive_name_at(open->text, offset, word))
        {
            respond(id, nullptr);
            return;
        }
        const auto definitions = workspace_.metadata(*open);
        const auto found = definitions.find(word);
        if (found == definitions.end())
        {
            respond(id, nullptr);
            return;
        }
        std::string markdown = "**@" + word + "**";
        for (const auto &prop : found->second.props)
            markdown += "\n\n`" + format_declaration(prop) + "`";
        respond(id, {{"contents", {{"kind", "markdown"}, {"value", markdown}}}});
    }

    void Server::definition(const Json &id, const Json &params)
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
        const auto word = text::word_at(open->text, offset);
        const auto parsed = email_markup::parse(0, open->text);
        const auto location = [&](const email_markup::SourceRange range)
        {
            respond(id,
                    {{"uri", params.at("textDocument").at("uri")},
                     {"range",
                      {{"start", text::position_at(open->text, range.start)},
                       {"end", text::position_at(open->text, range.end)}}}});
        };

        if (const auto component = analysis::containing_component_at(
                open->text, offset, parsed.document.components))
        {
            const auto &definition = parsed.document.components.at(*component);
            if (const auto prop = std::find_if(
                    definition.props.begin(), definition.props.end(),
                    [&](const auto &candidate)
                    { return candidate.name == word; });
                prop != definition.props.end())
            {
                location(prop->name_range);
                return;
            }
            if (const auto slot = std::find_if(
                    definition.slots.begin(), definition.slots.end(),
                    [&](const auto &candidate)
                    { return candidate.name == word; });
                slot != definition.slots.end())
            {
                location(analysis::identifier_range(open->text, slot->range, slot->name));
                return;
            }
        }

        if (const auto invocation = analysis::invocation_at(open->text, offset))
        {
            if (const auto component = parsed.document.components.find(invocation->name);
                component != parsed.document.components.end())
            {
                if (const auto prop = std::find_if(
                        component->second.props.begin(), component->second.props.end(),
                        [&](const auto &candidate)
                        { return candidate.name == word; });
                    prop != component->second.props.end())
                {
                    location(prop->name_range);
                    return;
                }
            }
        }

        const auto found = parsed.document.components.find(word);
        if (found == parsed.document.components.end())
        {
            respond(id, nullptr);
            return;
        }
        location(found->second.range);
    }

    void Server::references(const Json &id, const Json &params)
    {
        const auto *open = document(params);
        if (!open)
        {
            respond(id, Json::array());
            return;
        }
        const auto &position = params.at("position");
        const auto offset = text::offset_at(open->text, position.value("line", 0),
                                            position.value("character", 0));
        const auto word = text::word_at(open->text, offset);
        const auto parsed = email_markup::parse(0, open->text);

        const email_markup::ComponentDefinition *component = nullptr;
        const email_markup::PropDeclaration *prop = nullptr;
        const email_markup::SlotDeclaration *slot = nullptr;
        if (const auto name = analysis::containing_component_at(
                open->text, offset, parsed.document.components))
        {
            component = &parsed.document.components.at(*name);
            const auto found_prop = std::find_if(
                component->props.begin(), component->props.end(),
                [&](const auto &candidate)
                { return candidate.name == word; });
            if (found_prop != component->props.end())
                prop = &*found_prop;
            const auto found_slot = std::find_if(
                component->slots.begin(), component->slots.end(),
                [&](const auto &candidate)
                { return candidate.name == word; });
            if (found_slot != component->slots.end())
                slot = &*found_slot;
        }
        else if (const auto invocation = analysis::invocation_at(open->text, offset))
        {
            if (const auto found = parsed.document.components.find(invocation->name);
                found != parsed.document.components.end())
            {
                component = &found->second;
                const auto found_prop = std::find_if(
                    component->props.begin(), component->props.end(),
                    [&](const auto &candidate)
                    { return candidate.name == word; });
                if (found_prop != component->props.end())
                    prop = &*found_prop;
            }
        }
        if (!component || (!prop && !slot))
        {
            respond(id, Json::array());
            return;
        }

        const auto uri = params.at("textDocument").at("uri");
        Json result = Json::array();
        const auto add = [&](const std::size_t start, const std::size_t end)
        {
            result.push_back(
                {{"uri", uri},
                 {"range",
                  {{"start", text::position_at(open->text, start)},
                   {"end", text::position_at(open->text, end)}}}});
        };
        if (params.value("context", Json::object()).value("includeDeclaration", false))
        {
            const auto declaration =
                prop ? prop->name_range
                     : analysis::identifier_range(open->text, slot->range, slot->name);
            add(declaration.start, declaration.end);
        }

        const auto add_matches = [&](const std::regex &pattern, const std::size_t begin,
                                     const std::size_t end)
        {
            const auto source = std::string{open->text.substr(begin, end - begin)};
            for (std::sregex_iterator it{source.begin(), source.end(), pattern}, last;
                 it != last; ++it)
            {
                const auto start = begin + static_cast<std::size_t>(it->position(1));
                add(start, start + static_cast<std::size_t>(it->length(1)));
            }
        };
        const auto [component_start, component_end] =
            analysis::component_span(open->text, component->range.start);
        if (prop)
        {
            const auto name = prop->name;
            add_matches(std::regex{"@\\{[^}\\n]*\\b(" + name + ")\\b[^}\\n]*\\}"},
                        component_start, component_end);
            add_matches(std::regex{"@[A-Z][A-Za-z0-9_]*\\([^\\n)]*\\b(" + name +
                                   ")\\b[^\\n)]*\\)"},
                        component_start, component_end);
            add_matches(std::regex{"@" + component->name +
                                   "\\s*\\([^\\n)]*\\b(" + name + ")\\s*:"},
                        0, open->text.size());
        }
        else
        {
            add_matches(std::regex{"@Slot\\s*\\(\\s*(" + slot->name + ")\\b"},
                        component_start, component_end);
        }
        respond(id, std::move(result));
    }
} // namespace email_markup::lsp
