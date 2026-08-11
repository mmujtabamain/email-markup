#include "server/server.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

#include "analysis/context.hpp"
#include "email-markup/core/context_schema.hpp"
#include "email-markup/core/engine.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"
#include "text/positions.hpp"

namespace email_markup::lsp
{
    namespace
    {
        std::string context_path_at(const std::string &source, const std::size_t offset)
        {
            const auto interpolation = source.rfind("@{", offset);
            const auto deferred = source.rfind("@[", offset);
            const auto open = interpolation == std::string::npos ? deferred
                              : deferred == std::string::npos ? interpolation
                                                             : std::max(interpolation, deferred);
            if (open == std::string::npos) return {};
            const auto closing = source.find(source[open + 1] == '{' ? '}' : ']', open + 2);
            if (closing != std::string::npos && offset > closing) return {};
            auto end = open + 2;
            while (end < source.size() &&
                   (std::isalnum(static_cast<unsigned char>(source[end])) ||
                    source[end] == '_' || source[end] == '.'))
                ++end;
            if (offset < open + 2 || offset > end) return {};
            return source.substr(open + 2, end - (open + 2));
        }

        const Json *schema_field_at(const Json &fields, const std::string &path)
        {
            const Json *current_fields = &fields;
            const Json *field = nullptr;
            std::size_t cursor = 0;
            while (cursor < path.size())
            {
                const auto dot = path.find('.', cursor);
                const auto name = path.substr(cursor, dot - cursor);
                if (!current_fields->contains(name)) return nullptr;
                field = &current_fields->at(name);
                if (dot == std::string::npos) return field;
                if (field->at("type") != "object") return nullptr;
                current_fields = &field->at("fields");
                cursor = dot + 1;
            }
            return field;
        }
    }

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
        const auto path = context_path_at(open->text, offset);
        if (!path.empty())
        {
            try
            {
                const auto request = workspace_.compilation_request(*open);
                if (!request.context_schema.is_null())
                {
                    const auto schema = parse_context_schema(request.context_schema);
                    if (const auto *field = schema_field_at(schema.fields, path))
                    {
                        std::string markdown = "**" + path + "**\n\n`" +
                                               field->at("type").get<std::string>() + "`";
                        if (field->value("required", false)) markdown += " · required";
                        if (field->value("nullable", false)) markdown += " · nullable";
                        if (field->contains("description"))
                            markdown += "\n\n" +
                                        field->at("description").get<std::string>();
                        if (field->contains("example"))
                            markdown += "\n\nExample: `" + field->at("example").dump() + "`";
                        respond(id, {{"contents", {{"kind", "markdown"},
                                                     {"value", markdown}}}});
                        return;
                    }
                }
            }
            catch (...)
            {
            }
        }
        const auto word = text::word_at(open->text, offset);
        if (!analysis::directive_name_at(open->text, offset, word))
        {
            respond(id, nullptr);
            return;
        }
        if (word == "Engine")
        {
            respond(id, {{"contents", {{"kind", "markdown"},
                                         {"value", "**@Engine**\n\nSelects a canonical `.emt` target and changes the output kind to `engine-template`."}}}});
            return;
        }
        if (word == "If" || word == "For")
        {
            const auto bracket = open->text.find('[', offset);
            const auto paren = open->text.find('(', offset);
            if (bracket != std::string::npos &&
                (paren == std::string::npos || bracket < paren))
            {
                const auto value = word == "If"
                                       ? "**@If[…]**\n\nTyped recipient-time condition emitted through EMIR."
                                       : "**@For[…]**\n\nTyped bounded recipient-time loop emitted through EMIR.";
                respond(id, {{"contents", {{"kind", "markdown"}, {"value", value}}}});
                return;
            }
        }
        if (open->path.extension() == ".emt")
        {
            const auto parsed = email_markup::parse_engine_definition(
                open->path, 0, open->text);
            if (const auto found = parsed.engine.macros.find(word);
                found != parsed.engine.macros.end())
            {
                std::string markdown = "**@" + word + "[…]**";
                for (const auto &parameter : found->second.parameters)
                    markdown += "\n\n`" + format_declaration(parameter) + "`";
                respond(id, {{"contents", {{"kind", "markdown"},
                                             {"value", markdown}}}});
                return;
            }
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
