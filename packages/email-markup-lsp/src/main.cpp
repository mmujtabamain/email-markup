#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "email-markup/core/format.hpp"
#include "email-markup/core/lexer.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/core/version.hpp"

namespace {

using Json = nlohmann::json;

struct OpenDocument {
    std::filesystem::path path;
    std::string text;
    std::int64_t version{};
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>{stream}, {}};
}

std::filesystem::path uri_path(std::string uri) {
    constexpr std::string_view prefix{"file://"};
    if (uri.starts_with(prefix)) uri.erase(0, prefix.size());
#ifdef _WIN32
    if (uri.size() > 2 && uri[0] == '/' && uri[2] == ':') uri.erase(0, 1);
#endif
    std::string decoded;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const auto hex = uri.substr(i + 1, 2);
            char* end = nullptr;
            const auto value = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                decoded.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        decoded.push_back(uri[i]);
    }
    return decoded;
}

std::size_t utf8_width(const unsigned char byte) {
    if ((byte & 0x80U) == 0) return 1;
    if ((byte & 0xe0U) == 0xc0U) return 2;
    if ((byte & 0xf0U) == 0xe0U) return 3;
    if ((byte & 0xf8U) == 0xf0U) return 4;
    return 1;
}

std::uint32_t codepoint_at(const std::string_view text, const std::size_t offset,
                           const std::size_t width) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (width == 1 || offset + width > text.size()) return first;
    std::uint32_t value = first & (0x7fU >> width);
    for (std::size_t i = 1; i < width; ++i)
        value = (value << 6U) | (static_cast<unsigned char>(text[offset + i]) & 0x3fU);
    return value;
}

std::size_t offset_at(const std::string_view text, const std::size_t target_line,
                      const std::size_t target_character) {
    std::size_t offset = 0;
    std::size_t line = 0;
    while (offset < text.size() && line < target_line) {
        if (text[offset++] == '\n') ++line;
    }
    std::size_t character = 0;
    while (offset < text.size() && text[offset] != '\n' && character < target_character) {
        const auto width = utf8_width(static_cast<unsigned char>(text[offset]));
        const auto point = codepoint_at(text, offset, width);
        const auto units = point > 0xffffU ? 2U : 1U;
        if (character + units > target_character) break;
        character += units;
        offset += width;
    }
    return offset;
}

Json position_at(const std::string_view text, const std::size_t target_offset) {
    std::size_t offset = 0;
    std::size_t line = 0;
    std::size_t character = 0;
    while (offset < text.size() && offset < target_offset) {
        if (text[offset] == '\n') {
            ++line;
            character = 0;
            ++offset;
            continue;
        }
        const auto width = utf8_width(static_cast<unsigned char>(text[offset]));
        const auto point = codepoint_at(text, offset, width);
        character += point > 0xffffU ? 2U : 1U;
        offset += width;
    }
    return {{"line", line}, {"character", character}};
}

std::string word_at(const std::string_view text, const std::size_t offset) {
    std::size_t start = std::min(offset, text.size());
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
                         text[start - 1] == '_')) --start;
    std::size_t end = std::min(offset, text.size());
    while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) ||
                                 text[end] == '_')) ++end;
    return std::string{text.substr(start, end - start)};
}

struct InvocationContext {
    std::string name;
    std::string current_argument;
    std::set<std::string> used_arguments;
    bool expects_name{};
};

enum class PropsCompletionContext {
    none,
    type,
    default_value,
};

PropsCompletionContext props_context_at(const std::string_view text,
                                        const std::size_t offset) {
    const auto cursor = std::min(offset, text.size());
    const auto open = text.rfind("@Props", cursor);
    if (open == std::string_view::npos) return PropsCompletionContext::none;
    const auto close = text.rfind("@/Props", cursor);
    if (close != std::string_view::npos && close > open)
        return PropsCompletionContext::none;

    const auto newline = text.rfind('\n', cursor == 0 ? 0 : cursor - 1);
    const auto line_start = newline == std::string_view::npos ? 0 : newline + 1;
    const auto line = text.substr(line_start, cursor - line_start);
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return PropsCompletionContext::none;
    return line.find('=', colon + 1) == std::string_view::npos
        ? PropsCompletionContext::type
        : PropsCompletionContext::default_value;
}

bool slot_requirement_context_at(const std::string_view text,
                                 const std::size_t offset) {
    const auto cursor = std::min(offset, text.size());
    const auto open = text.rfind("@Slots", cursor);
    if (open == std::string_view::npos) return false;
    const auto close = text.rfind("@/Slots", cursor);
    if (close != std::string_view::npos && close > open) return false;

    const auto newline = text.rfind('\n', cursor == 0 ? 0 : cursor - 1);
    const auto line_start = newline == std::string_view::npos ? 0 : newline + 1;
    return text.substr(line_start, cursor - line_start).find(':') !=
           std::string_view::npos;
}

std::optional<std::string> containing_component_at(
    const std::string_view text, const std::size_t offset,
    const std::unordered_map<std::string, email_markup::ComponentDefinition>& definitions) {
    std::optional<std::size_t> definition_start;
    for (const auto& token : email_markup::lex(0, text).tokens) {
        if (token.range.start > offset) break;
        if (token.kind == email_markup::TokenKind::open && token.name == "DefineComponent")
            definition_start = token.range.start;
        else if (token.kind == email_markup::TokenKind::close &&
                 token.name == "DefineComponent")
            definition_start.reset();
    }
    if (!definition_start) return std::nullopt;
    for (const auto& [name, definition] : definitions)
        if (definition.range.start == *definition_start) return name;
    return std::nullopt;
}

email_markup::SourceRange identifier_range(const std::string_view text,
                                  const email_markup::SourceRange declaration,
                                  const std::string_view name) {
    const auto start = text.find(name, declaration.start);
    if (start == std::string_view::npos || start >= declaration.end) return declaration;
    return {declaration.source, start, start + name.size()};
}

std::pair<std::size_t, std::size_t> component_span(
    const std::string_view text, const std::size_t definition_start) {
    bool inside = false;
    for (const auto& token : email_markup::lex(0, text).tokens) {
        if (!inside && token.kind == email_markup::TokenKind::open &&
            token.name == "DefineComponent" && token.range.start == definition_start) {
            inside = true;
        } else if (inside && token.kind == email_markup::TokenKind::close &&
                   token.name == "DefineComponent") {
            return {definition_start, token.range.end};
        }
    }
    return {definition_start, text.size()};
}

std::optional<InvocationContext> invocation_at(const std::string_view text,
                                               const std::size_t offset) {
    std::optional<InvocationContext> result;
    for (std::size_t at = 0; at < offset; ++at) {
        if (text[at] != '@' || at + 1 >= offset ||
            !std::isupper(static_cast<unsigned char>(text[at + 1]))) continue;
        std::size_t cursor = at + 2;
        while (cursor < offset && (std::isalnum(static_cast<unsigned char>(text[cursor])) ||
                                   text[cursor] == '_')) ++cursor;
        const auto name = std::string{text.substr(at + 1, cursor - at - 1)};
        while (cursor < offset && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor >= offset || text[cursor] != '(') continue;

        const auto open = cursor++;
        std::size_t segment = cursor;
        int depth = 1;
        char quote = 0;
        for (; cursor < offset; ++cursor) {
            const auto character = text[cursor];
            if (quote) {
                if (character == '\\' && cursor + 1 < offset) ++cursor;
                else if (character == quote) quote = 0;
                continue;
            }
            if (character == '"' || character == '\'') quote = character;
            else if (character == '(') ++depth;
            else if (character == ')' && --depth == 0) break;
            else if (character == ',' && depth == 1) segment = cursor + 1;
        }
        if (depth == 0) continue;

        InvocationContext context;
        context.name = name;
        std::size_t invocation_end = offset;
        auto forward_depth = depth;
        auto forward_quote = quote;
        for (; invocation_end < text.size(); ++invocation_end) {
            const auto character = text[invocation_end];
            if (forward_quote) {
                if (character == '\\' && invocation_end + 1 < text.size()) ++invocation_end;
                else if (character == forward_quote) forward_quote = 0;
            } else if (character == '"' || character == '\'') forward_quote = character;
            else if (character == '(') ++forward_depth;
            else if (character == ')' && --forward_depth == 0) break;
        }
        const auto arguments = std::string{text.substr(
            open + 1, invocation_end - open - 1)};
        static const std::regex named{R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*:)"};
        for (std::sregex_iterator it{arguments.begin(), arguments.end(), named}, end;
             it != end; ++it) context.used_arguments.insert((*it)[1].str());

        const auto current = std::string{text.substr(segment, offset - segment)};
        std::size_t colon = std::string::npos;
        depth = 0;
        quote = 0;
        for (std::size_t index = 0; index < current.size(); ++index) {
            const auto character = current[index];
            if (quote) {
                if (character == '\\' && index + 1 < current.size()) ++index;
                else if (character == quote) quote = 0;
            } else if (character == '"' || character == '\'') quote = character;
            else if (character == '(' || character == '{' || character == '[') ++depth;
            else if (character == ')' || character == '}' || character == ']') --depth;
            else if (character == ':' && depth == 0) { colon = index; break; }
        }
        context.expects_name = colon == std::string::npos;
        if (context.expects_name) {
            const auto remaining = std::string{text.substr(segment, invocation_end - segment)};
            std::smatch current_name;
            if (std::regex_search(remaining, current_name, named))
                context.used_arguments.erase(current_name[1].str());
        }
        if (colon != std::string::npos) {
            const auto raw = current.substr(0, colon);
            const auto begin = raw.find_first_not_of(" \t\r\n");
            const auto end = raw.find_last_not_of(" \t\r\n");
            if (begin != std::string::npos) context.current_argument = raw.substr(begin, end - begin + 1);
        }
        result = std::move(context);
    }
    return result;
}

bool interpolation_at(const std::string_view text, const std::size_t offset) {
    const auto open = text.rfind("@{", offset);
    if (open == std::string_view::npos) return false;
    const auto close = text.rfind('}', offset);
    return close == std::string_view::npos || close < open;
}

bool sigil_at(const std::string_view text, const std::size_t offset, bool& closing) {
    const auto line = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    const auto start = line == std::string_view::npos ? 0 : line + 1;
    const auto prefix = std::string{text.substr(start, offset - start)};
    static const std::regex pattern{R"((?:^|\s)@(/?)[A-Za-z0-9_]*$)"};
    std::smatch match;
    if (!std::regex_search(prefix, match, pattern)) return false;
    closing = match[1].str() == "/";
    return true;
}

class Server {
public:
    explicit Server(std::filesystem::path executable)
        : executable_(std::move(executable)) {
        const auto binary = executable_.parent_path();
        library_ = binary / "lib";
        const auto installed = binary.parent_path() / "share/email-markup";
        if (!std::filesystem::exists(library_) && std::filesystem::exists(installed / "lib")) {
            library_ = installed / "lib";
        }
        if (const char* value = std::getenv("EMAIL_MARKUP_LIB")) library_ = value;
        load_library_metadata();
    }

    int run() {
        while (auto message = read_message()) {
            handle(*message);
            if (exiting_) break;
        }
        return shutdown_requested_ ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    std::optional<Json> read_message() {
        std::string line;
        std::size_t length = 0;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            constexpr std::string_view header{"Content-Length:"};
            if (line.starts_with(header))
                length = static_cast<std::size_t>(std::stoull(line.substr(header.size())));
        }
        if (!std::cin || length == 0) return std::nullopt;
        std::string body(length, '\0');
        std::cin.read(body.data(), static_cast<std::streamsize>(length));
        if (static_cast<std::size_t>(std::cin.gcount()) != length) return std::nullopt;
        try { return Json::parse(body); }
        catch (...) { return std::nullopt; }
    }

    void send(const Json& message) {
        const auto body = message.dump();
        std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        std::cout.flush();
    }

    void respond(const Json& id, Json result) {
        send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
    }

    void error(const Json& id, const int code, std::string message) {
        send({{"jsonrpc", "2.0"}, {"id", id},
              {"error", {{"code", code}, {"message", std::move(message)}}}});
    }

    void notify(std::string method, Json params) {
        send({{"jsonrpc", "2.0"}, {"method", std::move(method)},
              {"params", std::move(params)}});
    }

    void handle(const Json& message) {
        const auto method = message.value("method", "");
        const auto params = message.value("params", Json::object());
        const auto has_id = message.contains("id");
        const auto id = has_id ? message.at("id") : Json{};
        if (method == "$/cancelRequest") {
            cancelled_.insert(params.value("id", Json{}).dump());
            return;
        }
        if (has_id && cancelled_.erase(id.dump()) > 0) {
            error(id, -32800, "Request cancelled");
            return;
        }
        if (method == "initialize") { initialize(id, params); return; }
        if (method == "initialized") return;
        if (method == "shutdown") { shutdown_requested_ = true; respond(id, nullptr); return; }
        if (method == "exit") { exiting_ = true; return; }
        if (method == "textDocument/didOpen") { did_open(params); return; }
        if (method == "textDocument/didChange") { did_change(params); return; }
        if (method == "textDocument/didClose") { did_close(params); return; }
        if (method == "workspace/didChangeWatchedFiles") {
            for (const auto& [uri, open] : documents_) publish(uri, open);
            return;
        }
        if (method == "textDocument/completion") { completion(id, params); return; }
        if (method == "textDocument/hover") { hover(id, params); return; }
        if (method == "textDocument/definition") { definition(id, params); return; }
        if (method == "textDocument/references") { references(id, params); return; }
        if (method == "textDocument/documentSymbol") { symbols(id, params); return; }
        if (method == "textDocument/foldingRange") { folding(id, params); return; }
        if (method == "textDocument/signatureHelp") { signature(id, params); return; }
        if (method == "textDocument/formatting") { formatting(id, params); return; }
        if (method == "email-markup/protocolVersion") { respond(id, {{"version", 1}}); return; }
        if (method == "email-markup/preview") { preview(id, params); return; }
        if (has_id) error(id, -32601, "Method not found: " + method);
    }

    void initialize(const Json& id, const Json& params) {
        workspace_roots_.clear();
        for (const auto& folder : params.value("workspaceFolders", Json::array()))
            workspace_roots_.push_back(uri_path(folder.value("uri", "")));
        respond(id, {
            {"serverInfo", {{"name", "email-markup-lsp"}, {"version", email_markup::version()}}},
            {"capabilities", {
                {"positionEncoding", "utf-16"},
                {"textDocumentSync", {{"openClose", true}, {"change", 2}}},
                {"completionProvider", {{"triggerCharacters", Json::array(
                    {"@", "/", "(", ",", ":", "=", "{", "\""})}}},
                {"hoverProvider", true}, {"definitionProvider", true},
                {"referencesProvider", true},
                {"documentSymbolProvider", true}, {"foldingRangeProvider", true},
                {"signatureHelpProvider", {{"triggerCharacters", Json::array({"(", ","})}}},
                {"documentFormattingProvider", true}
            }}
        });
    }

    OpenDocument* document(const Json& params) {
        const auto uri = params.at("textDocument").value("uri", "");
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    void did_open(const Json& params) {
        const auto& item = params.at("textDocument");
        const auto uri = item.value("uri", "");
        documents_[uri] = {uri_path(uri), item.value("text", ""), item.value("version", 0)};
        publish(uri, documents_.at(uri));
    }

    void did_change(const Json& params) {
        const auto uri = params.at("textDocument").value("uri", "");
        const auto found = documents_.find(uri);
        if (found == documents_.end()) return;
        auto& open = found->second;
        const auto incoming_version = params.at("textDocument").value("version", open.version + 1);
        for (const auto& change : params.value("contentChanges", Json::array())) {
            if (!change.contains("range")) open.text = change.value("text", "");
            else {
                const auto& range = change.at("range");
                const auto start = offset_at(open.text,
                    range.at("start").value("line", 0),
                    range.at("start").value("character", 0));
                const auto end = offset_at(open.text,
                    range.at("end").value("line", 0),
                    range.at("end").value("character", 0));
                open.text.replace(start, end - start, change.value("text", ""));
            }
        }
        open.version = incoming_version;
        publish(uri, open);
    }

    void did_close(const Json& params) {
        const auto uri = params.at("textDocument").value("uri", "");
        documents_.erase(uri);
        notify("textDocument/publishDiagnostics", {{"uri", uri}, {"diagnostics", Json::array()}});
    }

    std::optional<std::filesystem::path> project_config(const std::filesystem::path& file) {
        auto current = file.parent_path();
        std::filesystem::path boundary;
        for (const auto& root : workspace_roots_) {
            const auto relative = current.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..") { boundary = root; break; }
        }
        while (!current.empty()) {
            const auto candidate = current / "em.json";
            if (std::filesystem::exists(candidate)) return candidate;
            if (!boundary.empty() && current == boundary) break;
            const auto parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
        return std::nullopt;
    }

    std::filesystem::path expand(std::string value, const std::filesystem::path& base) const {
        const auto replace = [&](const std::string_view token,
                                 const std::filesystem::path& path) {
            if (const auto position = value.find(token); position != std::string::npos)
                value.replace(position, token.size(), path.string());
        };
        replace("${EMAIL_MARKUP_LIB}", library_);
        std::filesystem::path path{value};
        return path.is_absolute() ? path : base / path;
    }

    email_markup::CompilationRequest compilation_request(const OpenDocument& open,
                                                const Json* preview_data = nullptr) {
        email_markup::CompilationRequest request;
        request.entry_path = open.path;
        request.source = open.text;
        request.data = preview_data ? *preview_data : Json::object();
        request.include_directories = {library_};
        request.allowed_roots = {open.path.parent_path(), library_};
        request.imports = {library_ / "builtins.em"};
        if (const auto config_path = project_config(open.path)) {
            try {
                const auto config = Json::parse(read_file(*config_path));
                const auto root = config_path->parent_path();
                request.include_directories.clear();
                request.imports.clear();
                for (const auto& value : config.value("include", Json::array()))
                    request.include_directories.push_back(expand(value.get<std::string>(), root));
                for (const auto& value : config.value("imports", Json::array()))
                    request.imports.push_back(expand(value.get<std::string>(), root));
                request.allowed_roots = request.include_directories;
                request.allowed_roots.push_back(root);
                if (config.contains("shell")) request.shell =
                    expand(config.at("shell").get<std::string>(), root);
                if (!preview_data && config.contains("data")) {
                    const auto data_path = expand(config.at("data").get<std::string>(), root);
                    const auto raw = read_file(data_path);
                    if (!raw.empty()) request.data = Json::parse(raw);
                }
            } catch (...) {}
        }
        return request;
    }

    Json lsp_diagnostics(const email_markup::CompilationResult& result, const OpenDocument& open) {
        Json output = Json::array();
        for (const auto& diagnostic : result.diagnostics) {
            if (!result.snapshot || !result.snapshot->sources ||
                diagnostic.range.source >= result.snapshot->sources->size()) continue;
            const auto& source = result.snapshot->sources->get(diagnostic.range.source);
            if (source.path.lexically_normal() != open.path.lexically_normal()) continue;
            output.push_back({
                {"range", {{"start", position_at(source.text, diagnostic.range.start)},
                           {"end", position_at(source.text, diagnostic.range.end)}}},
                {"severity", diagnostic.severity == email_markup::Severity::error ? 1 :
                             diagnostic.severity == email_markup::Severity::warning ? 2 : 3},
                {"code", diagnostic.code}, {"source", "email-markup"},
                {"message", diagnostic.message}
            });
        }
        return output;
    }

    email_markup::CompilationResult compile_document(const OpenDocument& open,
                                            const Json* preview_data = nullptr) {
        auto request = compilation_request(open, preview_data);
        email_markup::DiskFileResolver resolver{request.limits.maximum_source_bytes};
        return email_markup::compile(request, resolver);
    }

    void publish(const std::string& uri, const OpenDocument& open) {
        const auto version = open.version;
        const auto result = compile_document(open);
        const auto found = documents_.find(uri);
        if (found == documents_.end() || found->second.version != version) return;
        notify("textDocument/publishDiagnostics",
               {{"uri", uri}, {"version", version},
                {"diagnostics", lsp_diagnostics(result, open)}});
    }

    void load_library_metadata() {
        for (const auto& path : {library_ / "builtins.em"}) {
            const auto source = read_file(path);
            if (source.empty()) continue;
            auto parsed = email_markup::parse(0, source);
            for (const auto& [name, definition] : parsed.document.components)
                components_[name] = definition;
            for (const auto& [name, definition] : parsed.document.styles)
                styles_.insert(name);
            for (const auto& [name, definition] : parsed.document.tokens)
                tokens_.insert(name);
        }
    }

    std::unordered_map<std::string, email_markup::ComponentDefinition> metadata(
        const OpenDocument& open) const {
        auto result = components_;
        const auto parsed = email_markup::parse(0, open.text);
        for (const auto& [name, definition] : parsed.document.components)
            result[name] = definition;
        return result;
    }

    void completion(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto& position = params.at("position");
        const auto offset = offset_at(open->text, position.value("line", 0),
                                      position.value("character", 0));
        Json items = Json::array();
        const auto definitions = metadata(*open);
        auto parsed = email_markup::parse(0, open->text);
        auto style_names = styles_;
        auto token_names = tokens_;
        for (const auto& [name, definition] : parsed.document.styles) style_names.insert(name);
        for (const auto& [name, definition] : parsed.document.tokens) token_names.insert(name);

        const auto add_expressions = [&]() {
            for (const auto& name : token_names)
                items.push_back({{"label", "token." + name}, {"kind", 21},
                                 {"detail", "Email Markup design token"}});
            const auto request = compilation_request(*open);
            std::function<void(const Json&, const std::string&)> add_data;
            add_data = [&](const Json& value, const std::string& prefix) {
                if (!value.is_object()) return;
                for (const auto& [key, child] : value.items()) {
                    const auto path = prefix.empty() ? key : prefix + "." + key;
                    items.push_back({{"label", path}, {"kind", child.is_object() ? 9 : 6},
                                     {"detail", "Compile data"}});
                    add_data(child, path);
                }
            };
            add_data(request.data, "");
            const auto prefix = open->text.substr(0, offset);
            static const std::regex local{R"(@For\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+in\b)"};
            for (std::sregex_iterator it{prefix.begin(), prefix.end(), local}, end;
                 it != end; ++it) {
                items.push_back({{"label", (*it)[1].str()}, {"kind", 6},
                                 {"detail", "Email Markup loop variable"}});
            }
        };

        const auto add_includes = [&]() {
            const auto request = compilation_request(*open);
            std::set<std::string> include_items;
            auto include_directories = request.include_directories;
            include_directories.insert(include_directories.begin(), open->path.parent_path());
            for (const auto& directory : include_directories) {
                std::error_code error;
                for (std::filesystem::directory_iterator it{directory, error}, end;
                     !error && it != end; it.increment(error)) {
                    if (!it->is_regular_file(error) || it->path().extension() != ".em") continue;
                    include_items.insert(it->path().filename().generic_string());
                }
            }
            for (const auto& path : include_items)
                items.push_back({{"label", path}, {"kind", 17}, {"insertText", path},
                                 {"detail", "Email Markup include"}});
        };

        if (const auto context = props_context_at(open->text, offset);
            context != PropsCompletionContext::none) {
            if (context == PropsCompletionContext::type) {
                for (const auto* type : {"string", "int", "number", "bool", "url",
                                         "email", "color"})
                    items.push_back({{"label", type}, {"kind", 25},
                                     {"detail", "Email Markup prop type"}});
            } else {
                add_expressions();
                for (const auto* literal : {"true", "false", "null"})
                    items.push_back({{"label", literal}, {"kind", 12},
                                     {"detail", "Email Markup literal"}});
            }
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (slot_requirement_context_at(open->text, offset)) {
            for (const auto* requirement : {"required", "optional"})
                items.push_back({{"label", requirement}, {"kind", 14},
                                 {"detail", "Email Markup slot requirement"}});
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (const auto invocation = invocation_at(open->text, offset)) {
            if (invocation->name == "Include") add_includes();
            else if (invocation->name == "Slot") {
                std::set<std::string> slots;
                for (const auto& [name, definition] : definitions)
                    for (const auto& slot : definition.slots) slots.insert(slot.name);
                for (const auto& name : slots)
                    items.push_back({{"label", name}, {"kind", 5}, {"detail", "Email Markup slot"}});
            } else if (invocation->name == "If" || invocation->name == "For" ||
                       invocation->name == "Media") {
                add_expressions();
            } else if (const auto found = definitions.find(invocation->name);
                       found != definitions.end()) {
                if (invocation->expects_name) {
                    for (const auto& prop : found->second.props) {
                        if (invocation->used_arguments.contains(prop.name)) continue;
                        items.push_back({{"label", prop.name}, {"kind", 5},
                                         {"insertText", prop.name + ": "},
                                         {"detail", "Email Markup prop: " + prop.type}});
                    }
                    if (!invocation->used_arguments.contains("style"))
                        items.push_back({{"label", "style"}, {"kind", 5},
                                         {"insertText", "style: \""},
                                         {"detail", "Email Markup style bundle"}});
                } else if (invocation->current_argument == "style") {
                    for (const auto& name : style_names)
                        items.push_back({{"label", name}, {"kind", 12},
                                         {"detail", "Email Markup style bundle"}});
                } else add_expressions();
            } else {
                static const std::unordered_map<std::string, std::vector<std::string>> arguments{
                    {"DefineComponent", {"name"}}, {"DefineStyle", {"name"}},
                    {"DefineToken", {"name", "value"}}
                };
                if (invocation->expects_name) {
                    if (const auto found = arguments.find(invocation->name); found != arguments.end())
                        for (const auto& name : found->second)
                            if (!invocation->used_arguments.contains(name))
                                items.push_back({{"label", name}, {"kind", 5},
                                                 {"insertText", name + ": "},
                                                 {"detail", "Email Markup argument"}});
                } else add_expressions();
            }
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        if (interpolation_at(open->text, offset)) {
            add_expressions();
            respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
            return;
        }

        bool closing = false;
        if (sigil_at(open->text, offset, closing)) {
            const auto sigil = open->text.rfind('@', offset == 0 ? 0 : offset - 1);
            const auto edit = [&](const std::string& text) {
                return Json{{"range", {{"start", position_at(open->text, sigil)},
                                        {"end", position_at(open->text, offset)}}},
                            {"newText", text}};
            };
            if (closing) {
                const auto lexed = email_markup::lex(0, open->text.substr(0, sigil));
                std::vector<std::string> stack;
                for (const auto& token : lexed.tokens) {
                    if (token.kind == email_markup::TokenKind::open) stack.push_back(token.name);
                    else if (token.kind == email_markup::TokenKind::close) {
                        const auto found = std::find(stack.rbegin(), stack.rend(), token.name);
                        if (found != stack.rend()) stack.erase(std::next(found).base(), stack.end());
                    }
                }
                std::set<std::string> seen;
                for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    if (seen.insert(*it).second)
                        items.push_back({{"label", "@/" + *it}, {"kind", 14},
                                         {"textEdit", edit("@/" + *it)}});
            } else {
                for (const auto& keyword : {"If", "For", "Include", "DefineComponent",
                                            "DefineStyle", "DefineToken", "Media", "Slot"}) {
                    items.push_back({{"label", "@" + std::string{keyword}}, {"kind", 14},
                                     {"textEdit", edit("@" + std::string{keyword})}});
                }
                for (const auto& [name, definition] : definitions) {
                    std::string snippet = "@" + name;
                    if (!definition.props.empty()) {
                        snippet += "(";
                        bool first = true;
                        int tab = 1;
                        for (const auto& prop : definition.props) {
                            if (prop.optional) continue;
                            if (!first) snippet += ", ";
                            snippet += prop.name + ": ${" + std::to_string(tab++) + ":" +
                                       prop.type + "}";
                            first = false;
                        }
                        snippet += ")";
                    }
                    const bool body = !definition.slots.empty();
                    snippet += body ? "\n  ${0}\n@/" + name : ";";
                    items.push_back({{"label", "@" + name}, {"kind", 7},
                                     {"insertTextFormat", 2}, {"textEdit", edit(snippet)}});
                }
            }
        }
        respond(id, {{"isIncomplete", false}, {"items", std::move(items)}});
    }

    void hover(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, nullptr); return; }
        const auto& position = params.at("position");
        const auto offset = offset_at(open->text, position.value("line", 0),
                                      position.value("character", 0));
        const auto word = word_at(open->text, offset);
        const auto definitions = metadata(*open);
        const auto found = definitions.find(word);
        if (found == definitions.end()) { respond(id, nullptr); return; }
        std::string markdown = "**@" + word + "**";
        for (const auto& prop : found->second.props)
            markdown += "\n\n`" + prop.name + ": " + prop.type +
                        (prop.optional ? "?" : "") + "`";
        respond(id, {{"contents", {{"kind", "markdown"}, {"value", markdown}}}});
    }

    void definition(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, nullptr); return; }
        const auto& position = params.at("position");
        const auto offset = offset_at(open->text, position.value("line", 0),
                                      position.value("character", 0));
        const auto word = word_at(open->text, offset);
        const auto parsed = email_markup::parse(0, open->text);
        const auto location = [&](const email_markup::SourceRange range) {
            respond(id, {{"uri", params.at("textDocument").at("uri")},
                         {"range", {{"start", position_at(open->text, range.start)},
                                    {"end", position_at(open->text, range.end)}}}});
        };

        if (const auto component = containing_component_at(
                open->text, offset, parsed.document.components)) {
            const auto& definition = parsed.document.components.at(*component);
            if (const auto prop = std::find_if(
                    definition.props.begin(), definition.props.end(),
                    [&](const auto& candidate) { return candidate.name == word; });
                prop != definition.props.end()) {
                location(identifier_range(open->text, prop->range, prop->name));
                return;
            }
            if (const auto slot = std::find_if(
                    definition.slots.begin(), definition.slots.end(),
                    [&](const auto& candidate) { return candidate.name == word; });
                slot != definition.slots.end()) {
                location(identifier_range(open->text, slot->range, slot->name));
                return;
            }
        }

        if (const auto invocation = invocation_at(open->text, offset)) {
            if (const auto component = parsed.document.components.find(invocation->name);
                component != parsed.document.components.end()) {
                if (const auto prop = std::find_if(
                        component->second.props.begin(), component->second.props.end(),
                        [&](const auto& candidate) { return candidate.name == word; });
                    prop != component->second.props.end()) {
                    location(identifier_range(open->text, prop->range, prop->name));
                    return;
                }
            }
        }

        const auto found = parsed.document.components.find(word);
        if (found == parsed.document.components.end()) { respond(id, nullptr); return; }
        location(found->second.range);
    }

    void references(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto& position = params.at("position");
        const auto offset = offset_at(open->text, position.value("line", 0),
                                      position.value("character", 0));
        const auto word = word_at(open->text, offset);
        const auto parsed = email_markup::parse(0, open->text);

        const email_markup::ComponentDefinition* component = nullptr;
        const email_markup::PropDeclaration* prop = nullptr;
        const email_markup::SlotDeclaration* slot = nullptr;
        if (const auto name = containing_component_at(
                open->text, offset, parsed.document.components)) {
            component = &parsed.document.components.at(*name);
            const auto found_prop = std::find_if(
                component->props.begin(), component->props.end(),
                [&](const auto& candidate) { return candidate.name == word; });
            if (found_prop != component->props.end()) prop = &*found_prop;
            const auto found_slot = std::find_if(
                component->slots.begin(), component->slots.end(),
                [&](const auto& candidate) { return candidate.name == word; });
            if (found_slot != component->slots.end()) slot = &*found_slot;
        } else if (const auto invocation = invocation_at(open->text, offset)) {
            if (const auto found = parsed.document.components.find(invocation->name);
                found != parsed.document.components.end()) {
                component = &found->second;
                const auto found_prop = std::find_if(
                    component->props.begin(), component->props.end(),
                    [&](const auto& candidate) { return candidate.name == word; });
                if (found_prop != component->props.end()) prop = &*found_prop;
            }
        }
        if (!component || (!prop && !slot)) { respond(id, Json::array()); return; }

        const auto uri = params.at("textDocument").at("uri");
        Json result = Json::array();
        const auto add = [&](const std::size_t start, const std::size_t end) {
            result.push_back({{"uri", uri},
                {"range", {{"start", position_at(open->text, start)},
                           {"end", position_at(open->text, end)}}}});
        };
        if (params.value("context", Json::object()).value("includeDeclaration", false)) {
            const auto declaration = prop
                ? identifier_range(open->text, prop->range, prop->name)
                : identifier_range(open->text, slot->range, slot->name);
            add(declaration.start, declaration.end);
        }

        const auto add_matches = [&](const std::regex& pattern,
                                     const std::size_t begin,
                                     const std::size_t end) {
            const auto text = std::string{open->text.substr(begin, end - begin)};
            for (std::sregex_iterator it{text.begin(), text.end(), pattern}, last;
                 it != last; ++it) {
                const auto start = begin + static_cast<std::size_t>(it->position(1));
                add(start, start + static_cast<std::size_t>(it->length(1)));
            }
        };
        const auto [component_start, component_end] =
            component_span(open->text, component->range.start);
        if (prop) {
            const auto name = prop->name;
            add_matches(std::regex{"@\\{[^}\\n]*\\b(" + name + ")\\b[^}\\n]*\\}"},
                        component_start, component_end);
            add_matches(std::regex{"@[A-Z][A-Za-z0-9_]*\\([^\\n)]*\\b(" + name +
                                   ")\\b[^\\n)]*\\)"},
                        component_start, component_end);
            add_matches(std::regex{"@" + component->name +
                                   "\\s*\\([^\\n)]*\\b(" + name + ")\\s*:"},
                        0, open->text.size());
        } else {
            add_matches(std::regex{"@Slot\\s*\\(\\s*(" + slot->name + ")\\b"},
                        component_start, component_end);
        }
        respond(id, std::move(result));
    }

    void symbols(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto parsed = email_markup::parse(0, open->text);
        Json result = Json::array();
        const auto add = [&](const std::string& name, const email_markup::SourceRange range, const int kind) {
            result.push_back({{"name", name}, {"kind", kind},
                {"range", {{"start", position_at(open->text, range.start)},
                           {"end", position_at(open->text, range.end)}}},
                {"selectionRange", {{"start", position_at(open->text, range.start)},
                                    {"end", position_at(open->text, range.end)}}}});
        };
        for (const auto& [name, definition] : parsed.document.components)
            add(name, definition.range, 5);
        for (const auto& [name, definition] : parsed.document.styles)
            add(name, definition.range, 13);
        for (const auto& [name, definition] : parsed.document.tokens)
            add(name, definition.range, 14);
        respond(id, std::move(result));
    }

    void folding(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto lexed = email_markup::lex(0, open->text);
        std::vector<email_markup::Token> stack;
        Json result = Json::array();
        for (const auto& token : lexed.tokens) {
            if (token.kind == email_markup::TokenKind::open) stack.push_back(token);
            else if (token.kind == email_markup::TokenKind::close) {
                const auto found = std::find_if(stack.rbegin(), stack.rend(), [&](const auto& open) {
                    return open.name == token.name;
                });
                if (found == stack.rend()) continue;
                const auto start = position_at(open->text, found->range.start);
                const auto end = position_at(open->text, token.range.end);
                if (start.at("line") < end.at("line"))
                    result.push_back({{"startLine", start.at("line")},
                                      {"endLine", end.at("line")}, {"kind", "region"}});
                stack.erase(std::next(found).base(), stack.end());
            }
        }
        respond(id, std::move(result));
    }

    void signature(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, nullptr); return; }
        const auto& position = params.at("position");
        const auto offset = offset_at(open->text, position.value("line", 0),
                                      position.value("character", 0));
        const auto prefix = open->text.substr(0, offset);
        const auto at = prefix.find_last_of('@');
        if (at == std::string::npos) { respond(id, nullptr); return; }
        const auto paren = prefix.find('(', at);
        if (paren == std::string::npos) { respond(id, nullptr); return; }
        const auto name = word_at(prefix, at + 1);
        const auto definitions = metadata(*open);
        const auto found = definitions.find(name);
        if (found == definitions.end()) { respond(id, nullptr); return; }
        std::string label = "@" + name + "(";
        Json parameters = Json::array();
        for (std::size_t index = 0; index < found->second.props.size(); ++index) {
            const auto& prop = found->second.props[index];
            if (index) label += ", ";
            const auto part = prop.name + ": " + prop.type;
            label += part;
            parameters.push_back({{"label", part}});
        }
        label += ")";
        const auto commas = static_cast<int>(std::count(prefix.begin() +
            static_cast<std::ptrdiff_t>(paren), prefix.end(), ','));
        respond(id, {{"signatures", Json::array({{{"label", label},
                                                   {"parameters", parameters}}})},
                     {"activeSignature", 0}, {"activeParameter", commas}});
    }

    void formatting(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto formatted = email_markup::format_source(open->text);
        if (formatted == open->text) { respond(id, Json::array()); return; }
        respond(id, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}},
                                              {"end", position_at(open->text, open->text.size())}}},
                                  {"newText", formatted}}}));
    }

    void preview(const Json& id, const Json& params) {
        const auto uri = params.value("uri", "");
        const auto found = documents_.find(uri);
        if (found == documents_.end()) { error(id, -32602, "Document is not open"); return; }
        const Json* data = params.contains("data") ? &params.at("data") : nullptr;
        const auto version = found->second.version;
        auto result = compile_document(found->second, data);
        if (documents_.find(uri) == documents_.end() ||
            documents_.at(uri).version != version) {
            error(id, -32801, "Preview became stale");
            return;
        }
        respond(id, {{"version", version},
                     {"html", result.ok() ? Json(result.generated.html) : Json{}},
                     {"diagnostics", lsp_diagnostics(result, found->second)}});
    }

    std::filesystem::path executable_;
    std::filesystem::path library_;
    std::vector<std::filesystem::path> workspace_roots_;
    std::unordered_map<std::string, OpenDocument> documents_;
    std::unordered_map<std::string, email_markup::ComponentDefinition> components_;
    std::set<std::string> styles_;
    std::set<std::string> tokens_;
    std::unordered_set<std::string> cancelled_;
    bool shutdown_requested_{};
    bool exiting_{};
};

}  // namespace

int main(const int argc, const char* const argv[]) {
    std::error_code error;
    const auto executable = std::filesystem::weakly_canonical(
        std::filesystem::absolute(argc > 0 ? argv[0] : "email-markup-lsp"), error);
    return Server{executable}.run();
}
