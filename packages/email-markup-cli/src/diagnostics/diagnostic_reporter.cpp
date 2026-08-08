#include "diagnostic_reporter.hpp"

#include <string>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

namespace email_markup::cli
{
    namespace
    {
        using Json = nlohmann::json;

        Json diagnostic_json(const email_markup::Diagnostic &diagnostic,
                             const email_markup::CompilationResult &result)
        {
            Json output{{"code", diagnostic.code},
                        {"severity", email_markup::severity_name(diagnostic.severity)},
                        {"message", diagnostic.message},
                        {"jsonPath", diagnostic.json_path.empty()
                                         ? Json{}
                                         : Json{diagnostic.json_path}}};
            if (result.snapshot && result.snapshot->sources &&
                diagnostic.range.source < result.snapshot->sources->size())
            {
                const auto &source = result.snapshot->sources->get(diagnostic.range.source);
                const auto start = source.position(diagnostic.range.start);
                const auto end = source.position(diagnostic.range.end);
                output["file"] = email_markup::portable_path_string(source.path);
                output["range"] = {
                    {"start", {{"line", start.line}, {"character", start.column}}},
                    {"end", {{"line", end.line}, {"character", end.column}}}};
            }
            output["related"] = Json::array();
            for (const auto &related : diagnostic.related)
                output["related"].push_back({{"message", related.message},
                                             {"source", related.range.source},
                                             {"start", related.range.start},
                                             {"end", related.range.end}});
            return output;
        }
    } // namespace

    DiagnosticReporter::DiagnosticReporter(const bool machine_readable) noexcept
        : machine_readable_(machine_readable)
    {
    }

    nlohmann::json DiagnosticReporter::serialize(
        const email_markup::CompilationResult &result)
    {
        Json diagnostics = Json::array();
        for (const auto &diagnostic : result.diagnostics)
            diagnostics.push_back(diagnostic_json(diagnostic, result));
        return diagnostics;
    }

    void DiagnosticReporter::print(const email_markup::CompilationResult &result) const
    {
        if (machine_readable_)
        {
            fmt::print("{}\n", Json{{"ok", result.ok()}, {"diagnostics", serialize(result)}}.dump());
            return;
        }

        for (const auto &diagnostic : result.diagnostics)
        {
            std::string file{"<compile>"};
            email_markup::SourcePosition position{};
            const email_markup::SourceFile *source = nullptr;
            if (result.snapshot && result.snapshot->sources &&
                diagnostic.range.source < result.snapshot->sources->size())
            {
                source = &result.snapshot->sources->get(diagnostic.range.source);
                file = email_markup::portable_path_string(source->path);
                position = source->position(diagnostic.range.start);
            }
            fmt::print(stderr, "{}:{}:{}: {} {}: {}\n", file, position.line + 1,
                       position.column + 1, email_markup::severity_name(diagnostic.severity),
                       diagnostic.code, diagnostic.message);
            if (source && position.line < source->line_starts.size())
            {
                const auto start = source->line_starts[position.line];
                const auto end = source->text.find('\n', start);
                const auto excerpt = source->text.substr(
                    start, end == std::string::npos ? std::string::npos : end - start);
                fmt::print(stderr, "  {}\n  {}^\n", excerpt,
                           std::string(position.column, ' '));
            }
        }
    }
} // namespace email_markup::cli
