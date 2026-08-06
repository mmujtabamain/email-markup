#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ell/core/format.hpp"
#include "ell/core/lexer.hpp"
#include "ell/core/parser.hpp"
#include "ell/core/render.hpp"
#include "ell/core/version.hpp"

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

class Server {
public:
    explicit Server(std::filesystem::path executable)
        : executable_(std::move(executable)) {
        const auto binary = executable_.parent_path();
        library_ = binary / "lib";
        brand_ = binary / "brand/example";
        const auto installed = binary.parent_path() / "share/ell";
        if (!std::filesystem::exists(library_) && std::filesystem::exists(installed / "lib")) {
            library_ = installed / "lib";
            brand_ = installed / "brand/example";
        }
        if (const char* value = std::getenv("ELL_LIB")) library_ = value;
        if (const char* value = std::getenv("EMAIL_MARKUP_BRAND")) brand_ = value;
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
        if (method == "textDocument/documentSymbol") { symbols(id, params); return; }
        if (method == "textDocument/foldingRange") { folding(id, params); return; }
        if (method == "textDocument/signatureHelp") { signature(id, params); return; }
        if (method == "textDocument/formatting") { formatting(id, params); return; }
        if (method == "ell/preview") { preview(id, params); return; }
        if (has_id) error(id, -32601, "Method not found: " + method);
    }

    void initialize(const Json& id, const Json& params) {
        workspace_roots_.clear();
        for (const auto& folder : params.value("workspaceFolders", Json::array()))
            workspace_roots_.push_back(uri_path(folder.value("uri", "")));
        respond(id, {
            {"serverInfo", {{"name", "ell-lsp"}, {"version", ell::version()}}},
            {"capabilities", {
                {"positionEncoding", "utf-16"},
                {"textDocumentSync", {{"openClose", true}, {"change", 2}}},
                {"completionProvider", {{"triggerCharacters", Json::array({"@", "(", ":"})}}},
                {"hoverProvider", true}, {"definitionProvider", true},
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
            const auto candidate = current / "ell.json";
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
        replace("${ELL_LIB}", library_);
        replace("${EMAIL_MARKUP_BRAND}", brand_);
        std::filesystem::path path{value};
        return path.is_absolute() ? path : base / path;
    }

    ell::CompilationRequest compilation_request(const OpenDocument& open,
                                                const Json* preview_data = nullptr) {
        ell::CompilationRequest request;
        request.entry_path = open.path;
        request.source = open.text;
        request.data = preview_data ? *preview_data : Json::object();
        request.include_directories = {library_, brand_};
        request.allowed_roots = {open.path.parent_path(), library_, brand_};
        request.imports = {library_ / "builtins.ell", brand_ / "brand.ell",
                           brand_ / "styles.ell"};
        request.shell = brand_ / "shell.ell";
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

    Json lsp_diagnostics(const ell::CompilationResult& result, const OpenDocument& open) {
        Json output = Json::array();
        for (const auto& diagnostic : result.diagnostics) {
            if (!result.snapshot || !result.snapshot->sources ||
                diagnostic.range.source >= result.snapshot->sources->size()) continue;
            const auto& source = result.snapshot->sources->get(diagnostic.range.source);
            if (source.path.lexically_normal() != open.path.lexically_normal()) continue;
            output.push_back({
                {"range", {{"start", position_at(source.text, diagnostic.range.start)},
                           {"end", position_at(source.text, diagnostic.range.end)}}},
                {"severity", diagnostic.severity == ell::Severity::error ? 1 :
                             diagnostic.severity == ell::Severity::warning ? 2 : 3},
                {"code", diagnostic.code}, {"source", "ell"},
                {"message", diagnostic.message}
            });
        }
        return output;
    }

    ell::CompilationResult compile_document(const OpenDocument& open,
                                            const Json* preview_data = nullptr) {
        auto request = compilation_request(open, preview_data);
        ell::DiskFileResolver resolver{request.limits.maximum_source_bytes};
        return ell::compile(request, resolver);
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
        for (const auto& path : {library_ / "builtins.ell", brand_ / "brand.ell",
                                 brand_ / "styles.ell"}) {
            const auto source = read_file(path);
            if (source.empty()) continue;
            auto parsed = ell::parse(0, source);
            for (const auto& [name, definition] : parsed.document.components)
                components_[name] = definition;
        }
    }

    std::unordered_map<std::string, ell::ComponentDefinition> metadata(
        const OpenDocument& open) const {
        auto result = components_;
        const auto parsed = ell::parse(0, open.text);
        for (const auto& [name, definition] : parsed.document.components)
            result[name] = definition;
        return result;
    }

    void completion(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        Json items = Json::array();
        for (const auto& keyword : {"If", "For", "Include", "DefineComponent",
                                    "DefineStyle", "DefineToken", "Media", "Slot"}) {
            items.push_back({{"label", "@" + std::string{keyword}}, {"kind", 14},
                             {"insertText", "@" + std::string{keyword}}});
        }
        for (const auto& [name, definition] : metadata(*open)) {
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
                             {"insertTextFormat", 2}, {"insertText", snippet}});
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
        const auto parsed = ell::parse(0, open->text);
        const auto found = parsed.document.components.find(word);
        if (found == parsed.document.components.end()) { respond(id, nullptr); return; }
        respond(id, {{"uri", params.at("textDocument").at("uri")},
                     {"range", {{"start", position_at(open->text, found->second.range.start)},
                                {"end", position_at(open->text, found->second.range.end)}}}});
    }

    void symbols(const Json& id, const Json& params) {
        const auto* open = document(params);
        if (!open) { respond(id, Json::array()); return; }
        const auto parsed = ell::parse(0, open->text);
        Json result = Json::array();
        const auto add = [&](const std::string& name, const ell::SourceRange range, const int kind) {
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
        const auto lexed = ell::lex(0, open->text);
        std::vector<ell::Token> stack;
        Json result = Json::array();
        for (const auto& token : lexed.tokens) {
            if (token.kind == ell::TokenKind::open) stack.push_back(token);
            else if (token.kind == ell::TokenKind::close) {
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
        const auto formatted = ell::format_source(open->text);
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
                     {"html", result.ok() ? Json{result.generated.html} : Json{}},
                     {"diagnostics", lsp_diagnostics(result, found->second)}});
    }

    std::filesystem::path executable_;
    std::filesystem::path library_;
    std::filesystem::path brand_;
    std::vector<std::filesystem::path> workspace_roots_;
    std::unordered_map<std::string, OpenDocument> documents_;
    std::unordered_map<std::string, ell::ComponentDefinition> components_;
    std::unordered_set<std::string> cancelled_;
    bool shutdown_requested_{};
    bool exiting_{};
};

}  // namespace

int main(const int argc, const char* const argv[]) {
    std::error_code error;
    const auto executable = std::filesystem::weakly_canonical(
        std::filesystem::absolute(argc > 0 ? argv[0] : "ell-lsp"), error);
    return Server{executable}.run();
}
