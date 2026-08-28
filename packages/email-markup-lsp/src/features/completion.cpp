#include "server/server.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analysis/context.hpp"
#include "email-markup/core/context_schema.hpp"
#include "email-markup/core/lexer.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"
#include "text/positions.hpp"

namespace email_markup::lsp
{
    namespace
    {
        void add_schema_items(const Json &fields, const std::string &prefix, Json &items)
        {
            for (const auto &[name, field] : fields.items())
            {
                const auto path = prefix.empty() ? name : prefix + "." + name;
                const auto type = field.at("type").get<std::string>();
                auto detail = type;
                if (field.value("required", false)) detail += " · required";
                if (field.value("nullable", false)) detail += " · nullable";
                Json item{{"label", path},
                          {"kind", type == "object" ? 9 : 6},
                          {"insertText", path},
                          {"detail", detail}};
                if (field.contains("description"))
                    item["documentation"] = field.at("description");
                items.push_back(std::move(item));
                if (type == "object")
                    add_schema_items(field.at("fields"), path, items);
            }
        }
    }

    void Server::completion(const Json &id, const Json &params)
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
        Json items = Json::array();
        const auto definitions = workspace_.metadata(*open);
        const auto parsed = email_markup::parse(0, open->text);
        auto style_names = workspace_.styles();
        auto token_names = workspace_.tokens();
        for (const auto &[name, definition] : parsed.document.styles)
            style_names.insert(name);
        for (const auto &[name, definition] : parsed.document.tokens)
            token_names.insert(name);

        const auto add_expressions = [&]()
        {
            for (const auto &name : token_names)
                items.push_back({{"label", "token." + name},
                                 {"kind", 21},
                                 {"detail", "Email Markup design token"}});
            const auto request = workspace_.compilation_request(*open);
            std::function<void(const Json &, const std::string &)> add_data;
            add_data = [&](const Json &value, const std::string &prefix)
            {
                if (!value.is_object())
                    return;
                for (const auto &[key, child] : value.items())
                {
                    const auto path = prefix.empty() ? key : prefix + "." + key;
                    items.push_back({{"label", path},
                                     {"kind", child.is_object() ? 9 : 6},
                                     {"detail", "Compile data"}});
                    add_data(child, path);
                }
            };
            if (!request.context_schema.is_null())
            {
                try
                {
                    add_schema_items(parse_context_schema(request.context_schema).fields,
                                     "", items);
                }
                catch (...)
                {
                }
            }
            else
                add_data(request.data, "");
            const auto prefix = open->text.substr(0, offset);
            static const std::regex local{
                R"(@For\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+in\b)"};
            for (std::sregex_iterator it{prefix.begin(), prefix.end(), local}, end;
                 it != end; ++it)
                items.push_back({{"label", (*it)[1].str()},
                                 {"kind", 6},
                                 {"detail", "Email Markup loop variable"}});
        };

        const auto add_includes = [&]()
        {
            const auto request = workspace_.compilation_request(*open);
            std::set<std::string> include_items;
            auto include_directories = request.include_directories;
            include_directories.insert(include_directories.begin(), open->path.parent_path());
            for (const auto &directory : include_directories)
            {
                std::error_code error;
                for (std::filesystem::directory_iterator it{directory, error}, end;
                     !error && it != end; it.increment(error))
                {
                    if (!it->is_regular_file(error) || it->path().extension() != ".em")
                        continue;
                    include_items.insert(it->path().filename().generic_string());
                }
            }
            for (const auto &path : include_items)
                items.push_back({{"label", path},
                                 {"kind", 17},
                                 {"insertText", path},
                                 {"detail", "Email Markup include"}});
        };

        if (const auto context = analysis::props_context_at(open->text, offset);
            context != analysis::PropsCompletionContext::none)
        {
            if (context == analysis::PropsCompletionContext::type)
            {
                if (open->path.extension() == ".emt")
                {
                    for (const auto *type : {"int", "decimal", "number", "bool", "name",
                                             "raw", "path", "condition"})
                        items.push_back({{"label", type},
                                         {"kind", 25},
                                         {"detail", "Deferred macro parameter type"}});
                }
                else
                {
                    for (const auto *type : {"string", "int", "decimal", "number", "bool",
                                             "name", "url", "email", "color"})
                        items.push_back({{"label", type},
                                         {"kind", 25},
                                         {"detail", "Email Markup prop type"}});
                }
            }
            else
            {
                add_expressions();
                for (const auto *literal : {"true", "false", "null"})
                    items.push_back({{"label", literal},
                                     {"kind", 12},
                                     {"detail", "Email Markup literal"}});
            }
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (analysis::slot_requirement_context_at(open->text, offset))
        {
            for (const auto *requirement : {"required", "optional"})
                items.push_back({{"label", requirement},
                                 {"kind", 14},
                                 {"detail", "Email Markup slot requirement"}});
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (const auto invocation = analysis::invocation_at(open->text, offset))
        {
            if (invocation->name == "Include")
            {
                add_includes();
            }
            else if (invocation->name == "Slot")
            {
                std::set<std::string> slots;
                for (const auto &[name, definition] : definitions)
                    for (const auto &slot : definition.slots)
                        slots.insert(slot.name);
                for (const auto &name : slots)
                    items.push_back({{"label", name},
                                     {"kind", 5},
                                     {"detail", "Email Markup slot"}});
            }
            else if (invocation->name == "If" || invocation->name == "For" ||
                     invocation->name == "Media")
            {
                add_expressions();
            }
            else if (const auto found = definitions.find(invocation->name);
                     found != definitions.end())
            {
                if (invocation->expects_name)
                {
                    for (const auto &prop : found->second.props)
                    {
                        if (invocation->used_arguments.contains(prop.name))
                            continue;
                        items.push_back({{"label", prop.name},
                                         {"kind", 5},
                                         {"insertText", prop.name + ": "},
                                         {"detail", "Email Markup prop: " + prop.type +
                                                        " — " + format_declaration(prop)}});
                    }
                    if (!invocation->used_arguments.contains("style"))
                        items.push_back({{"label", "style"},
                                         {"kind", 5},
                                         {"insertText", "style: \""},
                                         {"detail", "Email Markup style bundle"}});
                }
                else if (invocation->current_argument == "style")
                {
                    for (const auto &name : style_names)
                        items.push_back({{"label", name},
                                         {"kind", 12},
                                         {"detail", "Email Markup style bundle"}});
                }
                else
                {
                    add_expressions();
                }
            }
            else
            {
                static const std::unordered_map<std::string, std::vector<std::string>> arguments{
                    {"DefineComponent", {"name"}},
                    {"DefineStyle", {"name"}},
                    {"DefineToken", {"name", "value"}}};
                if (invocation->expects_name)
                {
                    if (const auto found = arguments.find(invocation->name);
                        found != arguments.end())
                        for (const auto &name : found->second)
                            if (!invocation->used_arguments.contains(name))
                                items.push_back({{"label", name},
                                                 {"kind", 5},
                                                 {"insertText", name + ": "},
                                                 {"detail", "Email Markup argument"}});
                }
                else
                {
                    add_expressions();
                }
            }
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (analysis::interpolation_at(open->text, offset))
        {
            add_expressions();
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        bool closing = false;
        if (analysis::sigil_at(open->text, offset, closing))
        {
            const auto sigil = open->text.rfind('@', offset == 0 ? 0 : offset - 1);
            const auto edit = [&](const std::string &new_text)
            {
                return Json{{"range",
                             {{"start", text::position_at(open->text, sigil)},
                              {"end", text::position_at(open->text, offset)}}},
                            {"newText", new_text}};
            };
            if (closing)
            {
                const auto lexed = email_markup::lex(0, open->text.substr(0, sigil));
                std::vector<std::string> stack;
                for (const auto &token : lexed.tokens)
                {
                    if (token.kind == email_markup::TokenKind::open)
                    {
                        stack.push_back(token.name);
                    }
                    else if (token.kind == email_markup::TokenKind::close)
                    {
                        const auto found = std::find(stack.rbegin(), stack.rend(), token.name);
                        if (found != stack.rend())
                            stack.erase(std::next(found).base(), stack.end());
                    }
                }
                std::set<std::string> seen;
                for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    if (seen.insert(*it).second)
                        items.push_back({{"label", "@/" + *it},
                                         {"kind", 14},
                                         {"textEdit", edit("@/" + *it)}});
            }
            else
            {
                const std::vector<std::string> keywords =
                    open->path.extension() == ".emt"
                        ? std::vector<std::string>{"DefineBareTemplate", "DefineTemplate",
                                                   "Params", "Slots", "Template", "If",
                                                   "Slot"}
                        : std::vector<std::string>{"Engine", "If", "Else", "For",
                                                   "Include", "DefineComponent",
                                                   "DefineStyle", "DefineToken", "Media",
                                                   "Props", "Slots", "Template", "Slot"};
                for (const auto &keyword : keywords)
                    items.push_back({{"label", "@" + std::string{keyword}},
                                     {"kind", 14},
                                     {"textEdit", edit("@" + std::string{keyword})}});
                if (open->path.extension() != ".emt")
                {
                    items.push_back({{"label", "@[…]"},
                                     {"kind", 15},
                                     {"insertTextFormat", 2},
                                     {"textEdit", edit("@[${1:recipient.path}]")},
                                     {"detail", "Deferred Django value"}});
                    items.push_back({{"label", "@If[…]"},
                                     {"kind", 15},
                                     {"insertTextFormat", 2},
                                     {"textEdit", edit("@If[${1:condition}]\n  ${0}\n@/If")},
                                     {"detail", "Deferred Django condition"}});
                    items.push_back({{"label", "@For[…]"},
                                     {"kind", 15},
                                     {"insertTextFormat", 2},
                                     {"textEdit", edit("@For[collection: ${1:recipient.items}, binding: ${2:item}, limit: ${3:20}]\n  ${0}\n@/For")},
                                     {"detail", "Bounded deferred Django loop"}});
                }
                for (const auto &[name, definition] : definitions)
                {
                    std::string snippet = "@" + name;
                    if (!definition.props.empty())
                    {
                        snippet += "(";
                        bool first = true;
                        int tab = 1;
                        for (const auto &prop : definition.props)
                        {
                            if (prop.optional)
                                continue;
                            if (!first)
                                snippet += ", ";
                            snippet += prop.name + ": ${" + std::to_string(tab++) + ":" +
                                       prop.type + "}";
                            first = false;
                        }
                        snippet += ")";
                    }
                    const bool body = !definition.slots.empty();
                    snippet += body ? "\n  ${0}\n@/" + name : ";";
                    items.push_back({{"label", "@" + name},
                                     {"kind", 7},
                                     {"insertTextFormat", 2},
                                     {"textEdit", edit(snippet)}});
                }
            }
        }
        respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
    }
} // namespace email_markup::lsp
