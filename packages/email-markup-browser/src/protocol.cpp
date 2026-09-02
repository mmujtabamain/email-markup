#include "email-markup/browser/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "email-markup/core/engine.hpp"
#include "email-markup/core/context_schema.hpp"
#include "email-markup/core/format.hpp"
#include "email-markup/core/include.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/core/sample_preview.hpp"
#include "email-markup/core/types.hpp"
#include "email-markup/core/version.hpp"

namespace email_markup::browser
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t maximum_virtual_files = 256;
        constexpr std::size_t maximum_total_source_bytes = 2 * 1024 * 1024;

        struct Workspace
        {
            CompilationRequest request;
            std::vector<ResolvedFile> files;
        };

        bool only_keys(const Json &value,
                       const std::initializer_list<std::string_view> allowed)
        {
            if (!value.is_object())
                return false;
            for (auto item = value.begin(); item != value.end(); ++item)
                if (std::find(allowed.begin(), allowed.end(), item.key()) ==
                    allowed.end())
                    return false;
            return true;
        }

        std::filesystem::path virtual_path(const Json &value,
                                           const std::string_view field,
                                           const std::set<std::string> &extensions)
        {
            if (!value.is_string())
                throw std::invalid_argument(std::string{field} + " must be a string");
            const auto raw = value.get<std::string>();
            if (!raw.starts_with('/'))
                throw std::invalid_argument(std::string{field} +
                                            " must be an absolute virtual path");
            const auto normalized = normalize_virtual_path(raw);
            if (!normalized || !extensions.contains(normalized->extension().string()))
                throw std::invalid_argument(std::string{field} +
                                            " has an unsupported source extension");
            return *normalized;
        }

        std::string source_text(const Json &value, const std::string_view field)
        {
            if (!value.is_string())
                throw std::invalid_argument(std::string{field} + " must be a string");
            auto source = value.get<std::string>();
            if (source.size() > CompilationLimits{}.maximum_source_bytes)
                throw std::invalid_argument(std::string{field} +
                                            " exceeds the 1 MiB source limit");
            if (!is_valid_utf8(source))
                throw std::invalid_argument(std::string{field} +
                                            " must contain valid UTF-8");
            return source;
        }

        std::vector<std::filesystem::path> path_array(
            const Json &params, const std::string_view field,
            const std::set<std::string> &extensions)
        {
            if (!params.contains(field))
                return {};
            const auto &values = params.at(field);
            if (!values.is_array())
                throw std::invalid_argument(std::string{field} + " must be an array");
            std::vector<std::filesystem::path> paths;
            for (const auto &value : values)
                paths.push_back(virtual_path(value, field, extensions));
            return paths;
        }

        std::filesystem::path virtual_root(const Json &value,
                                           const std::string_view field)
        {
            if (!value.is_string())
                throw std::invalid_argument(std::string{field} + " must be a string");
            const auto raw = value.get<std::string>();
            if (!raw.starts_with('/'))
                throw std::invalid_argument(std::string{field} +
                                            " contains a relative virtual path");
            const auto normalized = normalize_virtual_path(raw);
            if (!normalized)
                throw std::invalid_argument(std::string{field} +
                                            " contains an invalid virtual path");
            return *normalized;
        }

        Workspace workspace_from(const Json &params)
        {
            if (!params.is_object())
                throw std::invalid_argument("params must be an object");
            if (!only_keys(params, {"entry_path", "source", "files",
                                    "include_directories", "imports", "shell",
                                    "engine", "data", "context_schema",
                                    "output_context", "position"}))
                throw std::invalid_argument("params contains unknown workspace fields");
            Workspace workspace;
            workspace.request.entry_path =
                virtual_path(params.at("entry_path"), "entry_path", {".em", ".emt"});
            workspace.request.source = source_text(params.at("source"), "source");
            if (params.contains("include_directories"))
            {
                if (!params.at("include_directories").is_array())
                    throw std::invalid_argument(
                        "include_directories must be an array");
                for (const auto &value : params.at("include_directories"))
                    workspace.request.include_directories.push_back(
                        virtual_root(value, "include_directories"));
            }
            workspace.request.imports = path_array(params, "imports", {".em"});
            workspace.request.allowed_roots = workspace.request.include_directories;
            workspace.request.allowed_roots.emplace_back("/");
            workspace.request.data = params.value("data", Json::object());
            if (!workspace.request.data.is_object())
                throw std::invalid_argument("data must be an object");
            if (workspace.request.data.dump().size() >
                workspace.request.limits.maximum_json_bytes)
                throw std::invalid_argument("data exceeds the 1 MiB JSON limit");
            if (params.contains("context_schema"))
            {
                workspace.request.context_schema = params.at("context_schema");
                const auto schema = parse_context_schema(workspace.request.context_schema);
                if (workspace.request.data.empty())
                    workspace.request.data = context_schema_example(schema);
            }
            const auto context = params.value("output_context", std::string{"html"});
            if (context != "html" && context != "subject")
                throw std::invalid_argument("output_context must be html or subject");
            workspace.request.subject = context == "subject";

            std::size_t total = workspace.request.source.size();
            const auto add_file = [&](const Json &file, const std::string_view field,
                                      const std::set<std::string> &extensions)
            {
                if (!file.is_object() || !only_keys(file, {"path", "source"}))
                    throw std::invalid_argument(std::string{field} +
                                                " must contain path and source only");
                auto path = virtual_path(file.at("path"),
                                         std::string{field} + ".path", extensions);
                auto source = source_text(file.at("source"),
                                          std::string{field} + ".source");
                if (path == workspace.request.entry_path)
                    throw std::invalid_argument(std::string{field} +
                                                " duplicates entry_path");
                total += source.size();
                workspace.files.push_back({std::move(path), std::move(source)});
            };

            const auto files = params.value("files", Json::array());
            if (!files.is_array() || files.size() > maximum_virtual_files)
                throw std::invalid_argument("files must be an array of at most 256 entries");
            for (const auto &file : files)
                add_file(file, "files", {".em", ".emt"});
            if (params.contains("shell"))
            {
                const auto before = workspace.files.size();
                add_file(params.at("shell"), "shell", {".em"});
                workspace.request.shell = workspace.files.at(before).canonical_path;
            }
            if (params.contains("engine"))
            {
                const auto before = workspace.files.size();
                add_file(params.at("engine"), "engine", {".emt"});
                workspace.request.engine = workspace.files.at(before).canonical_path;
            }
            if (workspace.files.size() > maximum_virtual_files ||
                total > maximum_total_source_bytes)
                throw std::invalid_argument(
                    "virtual workspace exceeds 256 files or 2 MiB of source");
            return workspace;
        }

        std::size_t utf8_width(const unsigned char byte)
        {
            if ((byte & 0x80U) == 0)
                return 1;
            if ((byte & 0xe0U) == 0xc0U)
                return 2;
            if ((byte & 0xf0U) == 0xe0U)
                return 3;
            if ((byte & 0xf8U) == 0xf0U)
                return 4;
            return 1;
        }

        std::uint32_t codepoint_at(const std::string_view text,
                                   const std::size_t offset,
                                   const std::size_t width)
        {
            const auto first = static_cast<unsigned char>(text[offset]);
            if (width == 1 || offset + width > text.size())
                return first;
            std::uint32_t value = first & (0x7fU >> width);
            for (std::size_t index = 1; index < width; ++index)
                value = (value << 6U) |
                        (static_cast<unsigned char>(text[offset + index]) & 0x3fU);
            return value;
        }

        std::size_t offset_at(const std::string_view text, const Json &position)
        {
            if (!position.is_object() || !position.value("line", Json{}).is_number_unsigned() ||
                !position.value("character", Json{}).is_number_unsigned())
                throw std::invalid_argument(
                    "position requires unsigned UTF-16 line and character values");
            const auto target_line = position.at("line").get<std::size_t>();
            const auto target_character = position.at("character").get<std::size_t>();
            std::size_t offset = 0;
            std::size_t line = 0;
            while (offset < text.size() && line < target_line)
                if (text[offset++] == '\n')
                    ++line;
            std::size_t character = 0;
            while (offset < text.size() && text[offset] != '\n' &&
                   character < target_character)
            {
                const auto width = utf8_width(
                    static_cast<unsigned char>(text[offset]));
                const auto units = codepoint_at(text, offset, width) > 0xffffU ? 2U : 1U;
                if (character + units > target_character)
                    break;
                character += units;
                offset += width;
            }
            return offset;
        }

        Json position_at(const std::string_view text, const std::size_t target)
        {
            std::size_t offset = 0;
            std::size_t line = 0;
            std::size_t character = 0;
            while (offset < text.size() && offset < target)
            {
                if (text[offset] == '\n')
                {
                    ++line;
                    character = 0;
                    ++offset;
                    continue;
                }
                const auto width = utf8_width(static_cast<unsigned char>(text[offset]));
                character += codepoint_at(text, offset, width) > 0xffffU ? 2U : 1U;
                offset += width;
            }
            return {{"line", line}, {"character", character}};
        }

        Json range_at(const std::string_view text, const SourceRange range)
        {
            return {{"start", position_at(text, range.start)},
                    {"end", position_at(text, range.end)}};
        }

        std::string word_at(const std::string_view text, const std::size_t offset)
        {
            std::size_t start = std::min(offset, text.size());
            while (start > 0 &&
                   (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
                    text[start - 1] == '_'))
                --start;
            std::size_t end = std::min(offset, text.size());
            while (end < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[end])) ||
                    text[end] == '_'))
                ++end;
            return std::string{text.substr(start, end - start)};
        }

        Json diagnostics_json(const CompilationResult &result)
        {
            Json output = Json::array();
            for (const auto &diagnostic : result.diagnostics)
            {
                Json value{{"code", diagnostic.code},
                           {"severity", diagnostic.severity == Severity::error
                                            ? "error"
                                        : diagnostic.severity == Severity::warning
                                            ? "warning"
                                            : "information"},
                           {"message", diagnostic.message}};
                if (result.snapshot && result.snapshot->sources &&
                    diagnostic.range.source < result.snapshot->sources->size())
                {
                    const auto &source =
                        result.snapshot->sources->get(diagnostic.range.source);
                    value["path"] = portable_path_string(source.path);
                    value["range"] = range_at(source.text, diagnostic.range);
                }
                output.push_back(std::move(value));
            }
            return output;
        }

        Json symbols_for(const std::filesystem::path &path,
                         const std::string_view source)
        {
            Json symbols = Json::array();
            const auto add = [&](const std::string &name, const std::string_view kind,
                                 const SourceRange range)
            {
                symbols.push_back({{"name", name}, {"kind", kind},
                                   {"range", range_at(source, range)}});
            };
            if (path.extension() == ".emt")
            {
                const auto parsed = parse_engine_definition(path, 0, source);
                if (parsed.engine.bare)
                    add("BareTemplate", "template", parsed.engine.bare->range);
                std::vector<std::string> names;
                for (const auto &[name, _] : parsed.engine.macros)
                    names.push_back(name);
                std::sort(names.begin(), names.end());
                for (const auto &name : names)
                    add(name, "template", parsed.engine.macros.at(name).range);
                return symbols;
            }
            const auto parsed = parse(0, source);
            std::vector<std::pair<std::string, SourceRange>> components;
            std::vector<std::pair<std::string, SourceRange>> styles;
            std::vector<std::pair<std::string, SourceRange>> tokens;
            for (const auto &[name, value] : parsed.document.components)
                components.emplace_back(name, value.range);
            for (const auto &[name, value] : parsed.document.styles)
                styles.emplace_back(name, value.range);
            for (const auto &[name, value] : parsed.document.tokens)
                tokens.emplace_back(name, value.range);
            const auto by_name = [](const auto &left, const auto &right)
            { return left.first < right.first; };
            std::sort(components.begin(), components.end(), by_name);
            std::sort(styles.begin(), styles.end(), by_name);
            std::sort(tokens.begin(), tokens.end(), by_name);
            for (const auto &[name, range] : components)
                add(name, "component", range);
            for (const auto &[name, range] : styles)
                add(name, "style", range);
            for (const auto &[name, range] : tokens)
                add(name, "token", range);
            return symbols;
        }

        std::unordered_map<std::string, ComponentDefinition> definitions_for(
            const Workspace &workspace)
        {
            std::unordered_map<std::string, ComponentDefinition> definitions;
            const auto add = [&](const std::string_view source)
            {
                auto parsed = parse(0, source);
                for (auto &[name, definition] : parsed.document.components)
                    definitions[name] = std::move(definition);
            };
            for (const auto &file : workspace.files)
                if (file.canonical_path.extension() == ".em")
                    add(file.contents);
            add(workspace.request.source);
            return definitions;
        }

        void add_data_items(const Json &value, const std::string &prefix, Json &items)
        {
            if (!value.is_object())
                return;
            std::vector<std::string> keys;
            for (const auto &[key, _] : value.items())
                keys.push_back(key);
            std::sort(keys.begin(), keys.end());
            for (const auto &key : keys)
            {
                const auto path = prefix.empty() ? key : prefix + "." + key;
                items.push_back({{"label", path},
                                 {"kind", value.at(key).is_object() ? "module" : "value"},
                                 {"insert_text", path},
                                 {"detail", "Compile data"}});
                add_data_items(value.at(key), path, items);
            }
        }

        void add_schema_items(const Json &fields, const std::string &prefix, Json &items)
        {
            std::vector<std::string> keys;
            for (const auto &[key, _] : fields.items()) keys.push_back(key);
            std::sort(keys.begin(), keys.end());
            for (const auto &key : keys)
            {
                const auto &field = fields.at(key);
                const auto path = prefix.empty() ? key : prefix + "." + key;
                const auto type = field.at("type").get<std::string>();
                auto detail = type;
                if (field.value("required", false)) detail += " · required";
                Json item{{"label", path}, {"kind", type == "object" ? "module" : "value"},
                          {"insert_text", path}, {"detail", detail}};
                if (field.contains("description"))
                    item["documentation"] = field.at("description");
                items.push_back(std::move(item));
                if (field.at("type") == "object")
                    add_schema_items(field.at("fields"), path, items);
            }
        }

        void add_context_items(const Workspace &workspace, Json &items)
        {
            if (!workspace.request.context_schema.is_null())
            {
                add_schema_items(parse_context_schema(workspace.request.context_schema).fields,
                                 "", items);
                return;
            }
            add_data_items(workspace.request.data, "", items);
        }

        Json completion(const Workspace &workspace, const Json &position)
        {
            const auto &source = workspace.request.source;
            const auto offset = offset_at(source, position);
            Json items = Json::array();
            const auto definitions = definitions_for(workspace);
            const auto params = source.rfind("@Params", offset);
            const auto props = source.rfind("@Props", offset);
            const auto block = params == std::string::npos ? props
                               : props == std::string::npos ? params
                                                           : std::max(params, props);
            if (block != std::string::npos)
            {
                const auto close = source.rfind(params == block ? "@/Params" : "@/Props",
                                                offset);
                const auto line = source.rfind('\n', offset == 0 ? 0 : offset - 1);
                const auto prefix = source.substr(line == std::string::npos ? 0 : line + 1,
                                                  offset - (line == std::string::npos
                                                                ? 0
                                                                : line + 1));
                if ((close == std::string::npos || close < block) &&
                    prefix.find(':') != std::string::npos)
                {
                    const auto types = workspace.request.entry_path.extension() == ".emt"
                                           ? std::vector<std::string>{"int", "decimal", "number",
                                                                      "bool", "name", "raw",
                                                                      "path", "condition"}
                                           : std::vector<std::string>{"string", "int", "decimal",
                                                                      "number", "bool", "name",
                                                                      "url", "email", "color"};
                    for (const auto &type : types)
                        items.push_back({{"label", type}, {"kind", "type"},
                                         {"insert_text", type},
                                         {"detail", params == block
                                                        ? "Deferred macro parameter type"
                                                        : "Email Markup prop type"}});
                    return {{"is_incomplete", false}, {"items", std::move(items)}};
                }
            }

            const auto interpolation = source.rfind("@{", offset);
            const auto interpolation_close = source.rfind('}', offset);
            if (interpolation != std::string::npos &&
                (interpolation_close == std::string::npos ||
                 interpolation_close < interpolation))
            {
                add_context_items(workspace, items);
                return {{"is_incomplete", false}, {"items", std::move(items)}};
            }

            const auto deferred = source.rfind("@[", offset);
            const auto deferred_close = source.rfind(']', offset);
            if (deferred != std::string::npos &&
                (deferred_close == std::string::npos || deferred_close < deferred))
            {
                add_context_items(workspace, items);
                return {{"is_incomplete", false}, {"items", std::move(items)}};
            }

            const auto at = source.rfind('@', offset == 0 ? 0 : offset - 1);
            const auto line = source.rfind('\n', offset == 0 ? 0 : offset - 1);
            if (at != std::string::npos)
            {
                const auto paren = source.find('(', at);
                if (paren != std::string::npos && paren < offset &&
                    source.substr(paren, offset - paren).find(')') ==
                        std::string::npos)
                {
                    const auto name = word_at(source, at + 1);
                    if (const auto found = definitions.find(name);
                        found != definitions.end())
                    {
                        const auto arguments = std::string{
                            source.substr(paren + 1, offset - paren - 1)};
                        std::set<std::string> used;
                        static const std::regex named{
                            R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*:)"};
                        for (std::sregex_iterator item{arguments.begin(), arguments.end(),
                                                     named}, end;
                             item != end; ++item)
                            used.insert((*item)[1].str());
                        const auto comma = arguments.rfind(',');
                        const auto current = arguments.substr(
                            comma == std::string::npos ? 0 : comma + 1);
                        if (current.find(':') == std::string::npos)
                        {
                            for (const auto &prop : found->second.props)
                                if (!used.contains(prop.name))
                                    items.push_back(
                                        {{"label", prop.name}, {"kind", "property"},
                                         {"insert_text", prop.name + ": "},
                                         {"detail", "Email Markup prop: " +
                                                        format_declaration(prop)}});
                        }
                        else
                            add_data_items(workspace.request.data, "", items);
                        return {{"is_incomplete", false},
                                {"items", std::move(items)}};
                    }
                    if (name == "If" || name == "For")
                    {
                        add_data_items(workspace.request.data, "", items);
                        return {{"is_incomplete", false},
                                {"items", std::move(items)}};
                    }
                }
            }
            if (at != std::string::npos && (line == std::string::npos || at > line))
            {
                const bool closing = at + 1 < offset && source[at + 1] == '/';
                const auto replace = Json{{"start", position_at(source, at)},
                                          {"end", position_at(source, offset)}};
                if (!closing)
                {
                    const auto keywords = workspace.request.entry_path.extension() == ".emt"
                                              ? std::vector<std::string>{"DefineBareTemplate",
                                                                         "DefineTemplate", "Params",
                                                                         "Slots", "Template", "If", "Slot"}
                                              : std::vector<std::string>{"Engine", "If", "Else", "For",
                                                                         "Include", "DefineComponent",
                                                                         "DefineStyle", "DefineToken", "Media",
                                                                         "Props", "Slots", "Template", "Slot"};
                    for (const auto &keyword : keywords)
                        items.push_back({{"label", "@" + keyword}, {"kind", "keyword"},
                                         {"insert_text", "@" + keyword},
                                         {"replace", replace}});
                    if (workspace.request.entry_path.extension() != ".emt")
                    {
                        items.push_back({{"label", "@[…]"}, {"kind", "snippet"},
                                         {"insert_text", "@[recipient.path]"},
                                         {"detail", "Deferred recipient value"},
                                         {"replace", replace}});
                        items.push_back({{"label", "@If[…]"}, {"kind", "snippet"},
                                         {"insert_text", "@If[condition]\n  \n@/If"},
                                         {"detail", "Deferred recipient condition"},
                                         {"replace", replace}});
                        items.push_back({{"label", "@For[…]"}, {"kind", "snippet"},
                                         {"insert_text", "@For[collection: recipient.items, binding: item, limit: 20]\n  \n@/For"},
                                         {"detail", "Bounded deferred recipient loop"},
                                         {"replace", replace}});
                    }
                    else
                    {
                        const auto engine = parse_engine_definition(
                            workspace.request.entry_path, 0, source);
                        std::vector<std::string> macro_names;
                        for (const auto &[name, _] : engine.engine.macros)
                            macro_names.push_back(name);
                        std::sort(macro_names.begin(), macro_names.end());
                        for (const auto &name : macro_names)
                            items.push_back({{"label", "@" + name + "[…]"},
                                             {"kind", "template"},
                                             {"insert_text", "@" + name + "[]"},
                                             {"replace", replace}});
                    }
                    std::vector<std::string> names;
                    for (const auto &[name, _] : definitions)
                        names.push_back(name);
                    std::sort(names.begin(), names.end());
                    for (const auto &name : names)
                    {
                        const auto &definition = definitions.at(name);
                        std::string snippet = "@" + name;
                        if (!definition.props.empty())
                        {
                            snippet += "(";
                            bool first = true;
                            for (const auto &prop : definition.props)
                            {
                                if (prop.optional)
                                    continue;
                                if (!first)
                                    snippet += ", ";
                                snippet += prop.name + ": " + prop.type;
                                first = false;
                            }
                            snippet += ")";
                        }
                        snippet += definition.slots.empty() ? ";" : "\n  \n@/" + name;
                        items.push_back({{"label", "@" + name}, {"kind", "component"},
                                         {"insert_text", std::move(snippet)},
                                         {"replace", replace}});
                    }
                }
                return {{"is_incomplete", false}, {"items", std::move(items)}};
            }
            return {{"is_incomplete", false}, {"items", std::move(items)}};
        }

        Json hover(const Workspace &workspace, const Json &position)
        {
            const auto &source = workspace.request.source;
            const auto offset = offset_at(source, position);
            if (!workspace.request.context_schema.is_null())
            {
                const auto interpolation = source.rfind("@{", offset);
                const auto deferred = source.rfind("@[", offset);
                const auto open = interpolation == std::string::npos ? deferred
                                  : deferred == std::string::npos ? interpolation
                                                                 : std::max(interpolation, deferred);
                if (open != std::string::npos)
                {
                    const auto closing = source.find(source[open + 1] == '{' ? '}' : ']', open + 2);
                    if (closing == std::string::npos || offset <= closing)
                    {
                        auto end = std::min(offset, source.size());
                        while (end < source.size() &&
                               (std::isalnum(static_cast<unsigned char>(source[end])) ||
                                source[end] == '_' || source[end] == '.'))
                            ++end;
                        const auto path = source.substr(open + 2, end - (open + 2));
                        const auto schema = parse_context_schema(workspace.request.context_schema);
                        const Json *fields = &schema.fields;
                        const Json *field = nullptr;
                        std::size_t cursor = 0;
                        while (cursor < path.size())
                        {
                            const auto dot = path.find('.', cursor);
                            const auto name = path.substr(cursor, dot - cursor);
                            if (!fields->contains(name)) { field = nullptr; break; }
                            field = &fields->at(name);
                            if (dot == std::string::npos) break;
                            if (field->at("type") != "object") { field = nullptr; break; }
                            fields = &field->at("fields");
                            cursor = dot + 1;
                        }
                        if (field)
                        {
                            std::string markdown = "**" + path + "**\n\n`" +
                                                   field->at("type").get<std::string>() + "`";
                            if (field->value("required", false)) markdown += " · required";
                            if (field->value("nullable", false)) markdown += " · nullable";
                            if (field->contains("description"))
                                markdown += "\n\n" + field->at("description").get<std::string>();
                            if (field->contains("example"))
                                markdown += "\n\nExample: `" + field->at("example").dump() + "`";
                            return {{"markdown", std::move(markdown)}};
                        }
                    }
                }
            }
            const auto word = word_at(source, offset);
            if (word.empty())
                return nullptr;
            auto word_start = std::min(offset, source.size());
            while (word_start > 0 &&
                   (std::isalnum(static_cast<unsigned char>(source[word_start - 1])) ||
                    source[word_start - 1] == '_'))
                --word_start;
            if (word_start == 0 || source[word_start - 1] != '@')
                return nullptr;
            if (word == "Engine")
                return {{"markdown", "**@Engine**\n\nSelects a deferred target. Browser output remains untrusted target source."}};
            if (word == "If" || word == "For")
            {
                const auto bracket = source.find('[', offset);
                if (bracket != std::string::npos)
                    return {{"markdown", word == "If"
                                             ? "**@If[…]**\n\nTyped recipient-time condition."
                                             : "**@For[…]**\n\nTyped bounded recipient-time loop."}};
            }
            if (workspace.request.entry_path.extension() == ".emt")
            {
                const auto parsed = parse_engine_definition(
                    workspace.request.entry_path, 0, source);
                if (const auto found = parsed.engine.macros.find(word);
                    found != parsed.engine.macros.end())
                {
                    std::string markdown = "**@" + word + "[…]**";
                    for (const auto &parameter : found->second.parameters)
                        markdown += "\n\n`" + format_declaration(parameter) + "`";
                    return {{"markdown", std::move(markdown)}};
                }
            }
            const auto definitions = definitions_for(workspace);
            if (const auto found = definitions.find(word); found != definitions.end())
            {
                std::string markdown = "**@" + word + "**";
                for (const auto &prop : found->second.props)
                    markdown += "\n\n`" + format_declaration(prop) + "`";
                return {{"markdown", std::move(markdown)}};
            }
            return nullptr;
        }

        Json signature(const Workspace &workspace, const Json &position)
        {
            const auto &source = workspace.request.source;
            const auto offset = offset_at(source, position);
            const auto prefix = source.substr(0, offset);
            const auto at = prefix.rfind('@');
            if (at == std::string::npos)
                return nullptr;
            const auto paren = prefix.find('(', at);
            if (paren == std::string::npos || prefix.find(')', paren) != std::string::npos)
                return nullptr;
            const auto name = word_at(prefix, at + 1);
            const auto definitions = definitions_for(workspace);
            const auto found = definitions.find(name);
            if (found == definitions.end())
                return nullptr;
            std::string label = "@" + name + "(";
            Json parameters = Json::array();
            for (std::size_t index = 0; index < found->second.props.size(); ++index)
            {
                if (index)
                    label += ", ";
                const auto part = format_declaration(found->second.props[index]);
                label += part;
                parameters.push_back({{"label", part}});
            }
            label += ")";
            return {{"label", std::move(label)},
                    {"parameters", std::move(parameters)},
                    {"active_parameter", std::count(prefix.begin() +
                                                          static_cast<std::ptrdiff_t>(paren),
                                                      prefix.end(), ',')}};
        }

        Json analyze(const Workspace &workspace)
        {
            if (workspace.request.entry_path.extension() == ".emt")
            {
                CompilationResult result;
                auto sources = std::make_shared<SourceManager>();
                const auto source = sources->add(workspace.request.entry_path,
                                                 workspace.request.source);
                auto parsed = parse_engine_definition(workspace.request.entry_path,
                                                      source,
                                                      workspace.request.source);
                result.diagnostics = std::move(parsed.diagnostics);
                auto snapshot = std::make_shared<DocumentSnapshot>();
                snapshot->sources = std::move(sources);
                snapshot->entry = source;
                result.snapshot = std::move(snapshot);
                return {{"success", result.ok()},
                        {"authoritative", false},
                        {"output_kind", "engine-definition"},
                        {"preview", {{"kind", "target-source"},
                                     {"source", workspace.request.source},
                                     {"rendered", false},
                                     {"executes_target", false}}},
                        {"diagnostics", diagnostics_json(result)},
                        {"dependencies", Json::array()},
                        {"symbols", symbols_for(workspace.request.entry_path,
                                                workspace.request.source)}};
            }
            MemoryFileResolver resolver{workspace.files,
                                        workspace.request.limits.maximum_source_bytes};
            auto request = workspace.request;
            const auto parsed = parse(0, request.source);
            request.source = definition_preview_source(parsed, request.source);
            const auto result = compile(request, resolver);
            std::vector<std::string> dependencies;
            for (const auto &dependency : result.dependencies)
                dependencies.push_back(portable_path_string(dependency));
            std::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                               dependencies.end());
            const auto kind = result.output_kind == OutputKind::engine_template
                                  ? "engine-template"
                                  : "final-html";
            Json response{{"success", result.ok()},
                          {"authoritative", false},
                          {"output_kind", kind},
                          {"diagnostics", diagnostics_json(result)},
                          {"dependencies", dependencies},
                          {"symbols", symbols_for(workspace.request.entry_path,
                                                  workspace.request.source)}};
            if (result.ok())
            {
                if (result.output_kind == OutputKind::engine_template)
                    response["preview"] = result.emir
                        ? Json{{"kind", "sample-html"},
                               {"html", render_emir_sample(*result.emir, request.data,
                                                          request.limits.maximum_html_bytes)},
                               {"rendered", true}, {"sample", true},
                               {"executes_target", false}}
                        : Json{{"kind", "target-source"},
                               {"source", result.generated.html},
                               {"rendered", false}, {"executes_target", false}};
                else
                    response["preview"] = {{"kind", "final-html"},
                                           {"html", result.generated.html},
                                           {"rendered", true},
                                           {"executes_target", false}};
                if (result.emir)
                    response["emir"] = result.emir->value;
                if (result.target)
                    response["target"] = {{"name", result.target->name},
                                           {"engine", portable_path_string(
                                                          result.target->engine)}};
            }
            else
                response["preview"] = nullptr;
            return response;
        }

        Json capabilities()
        {
            return {{"compiler_version", email_markup::version()},
                    {"position_encoding", "utf-16"},
                    {"methods", Json::array({"capabilities", "analyze", "format",
                                             "complete", "hover", "signature"})},
                    {"authoritative", false},
                    {"executes_target", false},
                    {"network_access", false},
                    {"limits", {{"request_bytes", maximum_request_bytes},
                                {"source_bytes", CompilationLimits{}.maximum_source_bytes},
                                {"workspace_source_bytes", maximum_total_source_bytes},
                                {"files", maximum_virtual_files},
                                {"diagnostics", CompilationLimits{}.maximum_diagnostics},
                                {"output_bytes", CompilationLimits{}.maximum_html_bytes}}}};
        }

        Json response(const Json &id, Json result)
        {
            return {{"protocol", protocol_name}, {"version", protocol_version},
                    {"compiler_version", email_markup::version()},
                    {"id", id}, {"ok", true}, {"result", std::move(result)}};
        }

        Json error_response(const Json &id, const std::string_view code,
                            const std::string &message)
        {
            return {{"protocol", protocol_name}, {"version", protocol_version},
                    {"compiler_version", email_markup::version()},
                    {"id", id}, {"ok", false},
                    {"error", {{"code", code}, {"message", message}}}};
        }
    } // namespace

    std::string handle_request(const std::string_view request)
    {
        Json id = nullptr;
        try
        {
            if (request.size() > maximum_request_bytes)
                throw std::invalid_argument("request exceeds the 1 MiB protocol limit");
            if (!is_valid_utf8(request))
                throw std::invalid_argument("request must contain valid UTF-8");
            const auto envelope = Json::parse(request);
            if (!only_keys(envelope, {"protocol", "version", "id", "method", "params"}))
                throw std::invalid_argument("request envelope contains unknown fields");
            if (envelope.value("protocol", std::string{}) != protocol_name)
                throw std::invalid_argument("unsupported request protocol");
            if (envelope.value("version", 0) != protocol_version)
                throw std::invalid_argument("unsupported request protocol version");
            if (!envelope.contains("id") ||
                !(envelope.at("id").is_string() || envelope.at("id").is_number_integer() ||
                  envelope.at("id").is_number_unsigned()))
                throw std::invalid_argument("id must be a string or integer");
            id = envelope.at("id");
            if (!envelope.contains("method") || !envelope.at("method").is_string())
                throw std::invalid_argument("method must be a string");
            const auto method = envelope.at("method").get<std::string>();
            // The method is checked here rather than after the workspace is
            // parsed. Reaching an unknown method only at the end meant a host
            // asking for something this build does not serve was told its
            // `entry_path` was missing, or that its method required a position —
            // never that the method itself was the problem, which is the one
            // thing a host on a newer protocol needs to hear.
            static const std::set<std::string> supported{
                "capabilities", "analyze", "format", "complete", "hover", "signature"};
            if (!supported.contains(method))
                throw std::invalid_argument("unsupported method " + method);
            const auto params = envelope.value("params", Json::object());
            if (method == "capabilities")
            {
                if (!params.is_object() || !params.empty())
                    throw std::invalid_argument("capabilities params must be empty");
                return response(id, capabilities()).dump() + "\n";
            }
            if (method == "format")
            {
                if (!params.is_object() || !only_keys(params, {"path", "source"}) ||
                    !params.contains("path") ||
                    !params.contains("source"))
                    throw std::invalid_argument("format requires path and source");
                (void)virtual_path(params.at("path"), "path", {".em", ".emt"});
                const auto source = source_text(params.at("source"), "source");
                const auto formatted = format_source(source);
                return response(id, {{"text", formatted},
                                     {"changed", formatted != source}}).dump() + "\n";
            }
            auto workspace = workspace_from(params);
            if (method == "analyze")
                return response(id, analyze(workspace)).dump() + "\n";
            if (!params.contains("position"))
                throw std::invalid_argument(method + " requires position");
            if (method == "complete")
                return response(id, completion(workspace, params.at("position"))).dump() +
                       "\n";
            if (method == "hover")
                return response(id, hover(workspace, params.at("position"))).dump() + "\n";
            return response(id, signature(workspace, params.at("position"))).dump() +
                   "\n";
        }
        catch (const Json::exception &error)
        {
            return error_response(id, "invalid_json", error.what()).dump() + "\n";
        }
        catch (const std::exception &error)
        {
            return error_response(id, "invalid_request", error.what()).dump() + "\n";
        }
        catch (...)
        {
            return error_response(
                       id, "internal_error",
                       "browser compiler hit an unexpected internal exception")
                .dump() +
                "\n";
        }
    }
} // namespace email_markup::browser
