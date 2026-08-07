#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "email-markup/core/data.hpp"
#include "email-markup/core/format.hpp"
#include "email-markup/core/lint.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/core/version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using Json = nlohmann::json;

constexpr int success = 0;
constexpr int compilation_failed = 1;
constexpr int usage_failed = 2;

struct ProjectConfig {
    std::filesystem::path path;
    std::vector<std::filesystem::path> includes;
    std::vector<std::filesystem::path> imports;
    std::optional<std::filesystem::path> data;
    std::optional<std::filesystem::path> shell;
    std::filesystem::path output{"build"};
};

struct Options {
    std::string command;
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::vector<std::filesystem::path> includes;
    std::vector<std::filesystem::path> imports;
    std::optional<std::filesystem::path> shell;
    std::optional<std::string> data_json;
    std::optional<std::filesystem::path> data_file;
    bool data_stdin{};
    bool json{};
    bool write{};
    email_markup::LintRole lint_role{email_markup::LintRole::content};
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>{stream}, {}};
}

std::string read_standard_input() {
    return {std::istreambuf_iterator<char>{std::cin}, {}};
}

void atomic_write(const std::filesystem::path& path, const std::string_view contents) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = path;
    temporary += ".tmp." + std::to_string(nonce);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot create " + temporary.string());
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream) {
            std::filesystem::remove(temporary);
            throw std::runtime_error("Cannot write " + temporary.string());
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace " + path.string());
    }
#else
    std::filesystem::rename(temporary, path);
#endif
}

std::filesystem::path executable_path(const char* argv0) {
    std::error_code error;
    auto path = std::filesystem::absolute(argv0, error);
    return error ? std::filesystem::path{argv0} :
                   std::filesystem::weakly_canonical(path, error);
}

std::string replace_all(std::string value, const std::string_view needle,
                        const std::string_view replacement) {
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return value;
}

struct AssetPaths {
    std::filesystem::path library;
    std::filesystem::path brand;
    std::filesystem::path schema;
};

AssetPaths locate_assets(const std::filesystem::path& executable) {
    const auto binary = executable.parent_path();
    AssetPaths paths{binary / "lib", binary / "brand/example", binary / "schema/em.schema.json"};
    const auto installed = binary.parent_path() / "share/email-markup";
    if (!std::filesystem::exists(paths.library) && std::filesystem::exists(installed / "lib")) {
        paths.library = installed / "lib";
        paths.brand = installed / "brand/example";
        paths.schema = installed / "schema/em.schema.json";
    }
    if (const char* value = std::getenv("EMAIL_MARKUP_LIB")) paths.library = value;
    if (const char* value = std::getenv("EMAIL_MARKUP_BRAND")) paths.brand = value;
    return paths;
}

std::optional<std::filesystem::path> find_config(std::filesystem::path from) {
    std::error_code error;
    if (!std::filesystem::is_directory(from, error)) from = from.parent_path();
    from = std::filesystem::absolute(from, error);
    while (!from.empty()) {
        const auto candidate = from / "em.json";
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        const auto parent = from.parent_path();
        if (parent == from) break;
        from = parent;
    }
    return std::nullopt;
}

std::filesystem::path expand_path(const std::string& raw,
                                  const std::filesystem::path& base,
                                  const AssetPaths& assets) {
    auto expanded = replace_all(raw, "${EMAIL_MARKUP_LIB}", assets.library.string());
    expanded = replace_all(expanded, "${EMAIL_MARKUP_BRAND}", assets.brand.string());
    std::filesystem::path path{expanded};
    if (path.is_relative()) path = base / path;
    return path.lexically_normal();
}

ProjectConfig load_config(const std::filesystem::path& input,
                          const AssetPaths& assets) {
    ProjectConfig config;
    if (const auto found = find_config(input)) {
        config.path = *found;
        const auto root = found->parent_path();
        const auto json = Json::parse(read_file(*found));
        if (!json.is_object()) throw std::runtime_error("em.json must contain an object");
        for (const auto& value : json.value("include", Json::array()))
            config.includes.push_back(expand_path(value.get<std::string>(), root, assets));
        for (const auto& value : json.value("imports", Json::array()))
            config.imports.push_back(expand_path(value.get<std::string>(), root, assets));
        if (json.contains("data"))
            config.data = expand_path(json.at("data").get<std::string>(), root, assets);
        if (json.contains("shell"))
            config.shell = expand_path(json.at("shell").get<std::string>(), root, assets);
        if (json.contains("out"))
            config.output = expand_path(json.at("out").get<std::string>(), root, assets);
        else config.output = root / "build";
        return config;
    }
    config.path = std::filesystem::absolute(input).parent_path() / "em.json";
    config.includes = {assets.library, assets.brand};
    config.imports = {assets.library / "builtins.em", assets.brand / "brand.em",
                      assets.brand / "styles.em"};
    config.shell = assets.brand / "shell.em";
    config.output = std::filesystem::absolute(input).parent_path() / "build";
    return config;
}

void usage() {
    fmt::print(stderr,
        "Email Markup compiler {}\n\n"
        "Usage:\n"
        "  emc build [dir] [options]\n"
        "  emc compile <file> -o <file> [options]\n"
        "  emc check <file> [options]\n"
        "  emc lint <file> [--role content|shell] [options]\n"
        "  emc fmt <file> [--write]\n"
        "  emc schema\n"
        "  emc --version\n\n"
        "Options:\n"
        "  -I <dir>            Add an include and allowed-root directory\n"
        "  --import <file>     Pre-import an Email Markup definition file\n"
        "  --data-json <json>  Compile with a JSON object\n"
        "  --data-stdin        Read the JSON object from standard input\n"
        "  --data-file <file>  Read the JSON object from a file\n"
        "  --shell <file>      Select the final HTML shell\n"
        "  --json              Emit machine-readable diagnostics\n",
        email_markup::version());
}

Options parse_options(const int argc, const char* const argv[]) {
    Options options;
    if (argc < 2) throw std::invalid_argument("missing command");
    options.command = argv[1];
    if (options.command == "--version" || options.command == "schema") return options;
    int index = 2;
    if (index < argc && argv[index][0] != '-') options.input = argv[index++];
    if (options.command != "build" && options.input.empty())
        throw std::invalid_argument("command requires an input file");
    if (options.command == "build" && options.input.empty()) options.input = ".";
    while (index < argc) {
        const std::string_view argument = argv[index++];
        const auto value = [&]() -> std::string {
            if (index >= argc) throw std::invalid_argument(
                std::string{argument} + " requires a value");
            return argv[index++];
        };
        if (argument == "-o") options.output = value();
        else if (argument == "-I") options.includes.emplace_back(value());
        else if (argument == "--import") options.imports.emplace_back(value());
        else if (argument == "--shell") options.shell = value();
        else if (argument == "--data-json") options.data_json = value();
        else if (argument == "--data-file") options.data_file = value();
        else if (argument == "--data-stdin") options.data_stdin = true;
        else if (argument == "--json") options.json = true;
        else if (argument == "--write") options.write = true;
        else if (argument == "--role") {
            const auto role = value();
            if (role == "content") options.lint_role = email_markup::LintRole::content;
            else if (role == "shell") options.lint_role = email_markup::LintRole::shell;
            else throw std::invalid_argument("--role must be content or shell");
        } else throw std::invalid_argument("unknown option " + std::string{argument});
    }
    const int data_sources = static_cast<int>(options.data_json.has_value()) +
                             static_cast<int>(options.data_file.has_value()) +
                             static_cast<int>(options.data_stdin);
    if (data_sources > 1) throw std::invalid_argument(
        "--data-json, --data-file, and --data-stdin are mutually exclusive");
    if (options.command == "compile" && !options.output)
        throw std::invalid_argument("compile requires -o <file>");
    return options;
}

email_markup::Json load_data(const Options& options, const ProjectConfig& config) {
    std::optional<std::string> raw;
    if (options.data_json) raw = *options.data_json;
    else if (options.data_stdin) raw = read_standard_input();
    else if (options.data_file) raw = read_file(*options.data_file);
    else if (config.data && std::filesystem::exists(*config.data)) raw = read_file(*config.data);
    if (!raw) return email_markup::Json::object();
    auto result = email_markup::parse_data(*raw, 1024 * 1024);
    if (!result.ok) throw std::runtime_error(result.diagnostics.front().message);
    return std::move(result.data);
}

email_markup::CompilationRequest request_for(const std::filesystem::path& input,
                                    const Options& options,
                                    const ProjectConfig& config,
                                    const email_markup::Json& data) {
    email_markup::CompilationRequest request;
    request.entry_path = std::filesystem::absolute(input).lexically_normal();
    request.source = read_file(request.entry_path);
    request.data = data;
    request.include_directories = config.includes;
    request.include_directories.insert(request.include_directories.end(),
                                       options.includes.begin(), options.includes.end());
    request.allowed_roots = request.include_directories;
    request.allowed_roots.push_back(config.path.parent_path());
    request.imports = config.imports;
    request.imports.insert(request.imports.end(), options.imports.begin(),
                           options.imports.end());
    request.shell = options.shell ? options.shell : config.shell;
    return request;
}

Json diagnostic_json(const email_markup::Diagnostic& diagnostic,
                     const email_markup::CompilationResult& result) {
    Json output{{"code", diagnostic.code},
                {"severity", email_markup::severity_name(diagnostic.severity)},
                {"message", diagnostic.message},
                {"jsonPath", diagnostic.json_path.empty() ? Json{} : Json{diagnostic.json_path}}};
    if (result.snapshot && result.snapshot->sources &&
        diagnostic.range.source < result.snapshot->sources->size()) {
        const auto& source = result.snapshot->sources->get(diagnostic.range.source);
        const auto start = source.position(diagnostic.range.start);
        const auto end = source.position(diagnostic.range.end);
        output["file"] = source.path.string();
        output["range"] = {{"start", {{"line", start.line}, {"character", start.column}}},
                           {"end", {{"line", end.line}, {"character", end.column}}}};
    }
    output["related"] = Json::array();
    for (const auto& related : diagnostic.related)
        output["related"].push_back({{"message", related.message},
                                      {"source", related.range.source},
                                      {"start", related.range.start},
                                      {"end", related.range.end}});
    return output;
}

void print_diagnostics(const email_markup::CompilationResult& result, const bool json) {
    if (json) {
        Json diagnostics = Json::array();
        for (const auto& diagnostic : result.diagnostics)
            diagnostics.push_back(diagnostic_json(diagnostic, result));
        fmt::print("{}\n", Json{{"ok", result.ok()},
                                {"diagnostics", std::move(diagnostics)}}.dump());
        return;
    }
    for (const auto& diagnostic : result.diagnostics) {
        std::string file{"<compile>"};
        email_markup::SourcePosition position{};
        const email_markup::SourceFile* source = nullptr;
        if (result.snapshot && result.snapshot->sources &&
            diagnostic.range.source < result.snapshot->sources->size()) {
            source = &result.snapshot->sources->get(diagnostic.range.source);
            file = source->path.string();
            position = source->position(diagnostic.range.start);
        }
        fmt::print(stderr, "{}:{}:{}: {} {}: {}\n", file, position.line + 1,
                   position.column + 1, email_markup::severity_name(diagnostic.severity),
                   diagnostic.code, diagnostic.message);
        if (source && position.line < source->line_starts.size()) {
            const auto start = source->line_starts[position.line];
            const auto end = source->text.find('\n', start);
            const auto excerpt = source->text.substr(start,
                end == std::string::npos ? std::string::npos : end - start);
            fmt::print(stderr, "  {}\n  {}^\n", excerpt,
                       std::string(position.column, ' '));
        }
    }
}

email_markup::CompilationResult compile_one(const std::filesystem::path& input,
                                   const Options& options,
                                   const ProjectConfig& config,
                                   const email_markup::Json& data) {
    auto request = request_for(input, options, config, data);
    email_markup::DiskFileResolver resolver{request.limits.maximum_source_bytes};
    return email_markup::compile(request, resolver);
}

int compile_command(const Options& options, const AssetPaths& assets) {
    const auto config = load_config(options.input, assets);
    const auto data = load_data(options, config);
    auto result = compile_one(options.input, options, config, data);
    print_diagnostics(result, options.json);
    if (!result.ok()) return compilation_failed;
    atomic_write(*options.output, result.generated.html);
    return success;
}

int check_or_lint(const Options& options, const AssetPaths& assets) {
    auto config = load_config(options.input, assets);
    if (options.command == "lint" && options.lint_role == email_markup::LintRole::shell)
        config.shell.reset();
    const auto data = load_data(options, config);
    auto result = compile_one(options.input, options, config, data);
    if (options.command == "lint" && options.lint_role == email_markup::LintRole::shell && result.ok()) {
        auto findings = email_markup::lint_html(result.generated.html, email_markup::LintRole::shell,
                                       {result.snapshot->entry, 0, 0});
        result.diagnostics.insert(result.diagnostics.end(), findings.begin(), findings.end());
    }
    print_diagnostics(result, options.json);
    return result.ok() ? success : compilation_failed;
}

int format_command(const Options& options) {
    const auto source = read_file(options.input);
    const auto formatted = email_markup::format_source(source);
    if (options.write) atomic_write(options.input, formatted);
    else fmt::print("{}", formatted);
    return success;
}

int build_command(const Options& options, const AssetPaths& assets) {
    const auto root = std::filesystem::absolute(options.input).lexically_normal();
    const auto config = load_config(root, assets);
    const auto data = load_data(options, config);
    std::vector<std::filesystem::path> sources;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it{root, error}, end;
         !error && it != end; it.increment(error)) {
        if (it->is_directory() && (it->path() == config.output ||
            it->path().filename() == ".git" || it->path().filename() == "external")) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || it->path().extension() != ".em") continue;
        const auto relative = std::filesystem::relative(it->path(), root, error);
        if (!relative.empty() && (relative.begin()->string() == "lib" ||
            relative.begin()->string() == "brand" || relative.begin()->string() == "components"))
            continue;
        sources.push_back(it->path());
    }
    std::sort(sources.begin(), sources.end());
    std::set<std::filesystem::path> outputs;
    bool failed = false;
    Json machine = Json::array();
    for (const auto& source : sources) {
        auto relative = std::filesystem::relative(source, root);
        relative.replace_extension(".html");
        const auto output = config.output / relative;
        if (!outputs.insert(output.lexically_normal()).second) {
            fmt::print(stderr, "duplicate output path: {}\n", output.string());
            failed = true;
            continue;
        }
        auto result = compile_one(source, options, config, data);
        if (options.json) {
            Json diagnostics = Json::array();
            for (const auto& diagnostic : result.diagnostics)
                diagnostics.push_back(diagnostic_json(diagnostic, result));
            machine.push_back({{"input", source.string()}, {"output", output.string()},
                               {"ok", result.ok()}, {"diagnostics", diagnostics}});
        } else print_diagnostics(result, false);
        if (!result.ok()) { failed = true; continue; }
        atomic_write(output, result.generated.html);
    }
    if (options.json) fmt::print("{}\n", Json{{"ok", !failed}, {"files", machine}}.dump());
    return failed ? compilation_failed : success;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        const auto options = parse_options(argc, argv);
        const auto assets = locate_assets(executable_path(argv[0]));
        if (options.command == "--version") {
            fmt::print("emc {}\n", email_markup::version());
            return success;
        }
        if (options.command == "schema") {
            fmt::print("{}", read_file(assets.schema));
            return success;
        }
        if (options.command == "compile") return compile_command(options, assets);
        if (options.command == "check" || options.command == "lint")
            return check_or_lint(options, assets);
        if (options.command == "fmt") return format_command(options);
        if (options.command == "build") return build_command(options, assets);
        throw std::invalid_argument("unknown command " + options.command);
    } catch (const std::invalid_argument& error) {
        fmt::print(stderr, "emc: {}\n\n", error.what());
        usage();
        return usage_failed;
    } catch (const std::exception& error) {
        fmt::print(stderr, "emc: {}\n", error.what());
        return usage_failed;
    }
}
