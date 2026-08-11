#include "email-markup/core/engine.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>

#include "email-markup/core/types.hpp"

namespace email_markup
{
    namespace
    {
        std::string trim(std::string value)
        {
            const auto keep = [](const unsigned char ch)
            { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
            value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
            return value;
        }

        void issue(std::vector<Diagnostic> &diagnostics, const std::size_t limit,
                   std::string code, std::string message, const SourceRange range)
        {
            if (diagnostics.size() < limit)
                diagnostics.push_back({std::move(code), Severity::error,
                                       std::move(message), range});
        }

        std::optional<std::pair<std::string_view, SourceRange>> section(
            const std::string_view body, const std::string_view name,
            const SourceId source, const std::size_t body_offset)
        {
            const auto open = body.find("@" + std::string{name});
            if (open == std::string_view::npos)
                return std::nullopt;
            auto content = open + name.size() + 1;
            while (content < body.size() &&
                   std::isspace(static_cast<unsigned char>(body[content])))
                ++content;
            const auto close = body.find("@/" + std::string{name}, content);
            if (close == std::string_view::npos)
                return std::nullopt;
            return std::pair{body.substr(content, close - content),
                             SourceRange{source, body_offset + content,
                                         body_offset + close}};
        }

        bool capitalized_name(const std::string &name)
        {
            static const std::regex pattern{R"(^[A-Z][A-Za-z0-9_]*$)"};
            return std::regex_match(name, pattern);
        }

        std::optional<EngineMacro> parse_macro(
            const std::string &name, const bool bare, const std::string_view body,
            const SourceRange range, std::vector<Diagnostic> &diagnostics,
            const std::size_t limit)
        {
            EngineMacro macro;
            macro.name = name;
            macro.bare = bare;
            macro.range = range;
            if (const auto params = section(body, "Params", range.source, range.start))
                macro.parameters = parse_declarations(params->first, params->second,
                                                      DeclarationContext::deferred_parameter,
                                                      diagnostics);
            if (const auto slots = section(body, "Slots", range.source, range.start))
                macro.slots = parse_slot_declarations(slots->first, slots->second,
                                                      diagnostics);
            const auto template_body = section(body, "Template", range.source, range.start);
            if (!template_body)
            {
                issue(diagnostics, limit, "EM0810",
                      "An engine macro requires exactly one @Template body.", range);
                return std::nullopt;
            }
            macro.template_source = std::string{template_body->first};
            if (macro.parameters.size() > 16)
                issue(diagnostics, limit, "EM0811",
                      "An engine macro may declare at most 16 parameters.", range);
            if (bare && (macro.parameters.size() > 1 || !macro.slots.empty()))
                issue(diagnostics, limit, "EM0812",
                      "@DefineBareTemplate accepts at most one parameter and no slots.",
                      range);
            for (const auto &parameter : macro.parameters)
            {
                const auto needle = "@{" + parameter.name + "}";
                if (macro.template_source.find(needle) == std::string::npos)
                    issue(diagnostics, limit, "EM0813",
                          "Parameter “" + parameter.name +
                              "” is not referenced by its engine template.",
                          parameter.range);
            }
            for (const auto &slot : macro.slots)
            {
                const auto needle = "@Slot(" + slot.name + ");";
                if (macro.template_source.find(needle) == std::string::npos)
                    issue(diagnostics, limit, "EM0814",
                          "Slot “" + slot.name +
                              "” is not referenced by its engine template.",
                          slot.range);
            }
            return macro;
        }
    } // namespace

    bool EngineParseResult::ok() const noexcept
    {
        return std::none_of(diagnostics.begin(), diagnostics.end(),
                            [](const auto &diagnostic)
                            { return diagnostic.severity == Severity::error; });
    }

    EngineParseResult parse_engine_definition(
        const std::filesystem::path &canonical_path, const SourceId source,
        const std::string_view contents, const std::size_t diagnostic_limit)
    {
        EngineParseResult result;
        result.engine.name = canonical_path.stem().string();
        result.engine.canonical_path = canonical_path;
        if (canonical_path.extension() != ".emt")
        {
            issue(result.diagnostics, diagnostic_limit, "EM0802",
                  "An engine definition must use the .emt extension.",
                  {source, 0, contents.size()});
            return result;
        }
        if (contents.size() > 256 * 1024)
        {
            issue(result.diagnostics, diagnostic_limit, "EM0815",
                  "Engine definition exceeds the 256 KiB limit.",
                  {source, 0, contents.size()});
            return result;
        }

        std::size_t position = 0;
        std::size_t macro_count = 0;
        while (position < contents.size())
        {
            const auto bare_at = contents.find("@DefineBareTemplate", position);
            const auto named_at = contents.find("@DefineTemplate", position);
            const bool bare = bare_at != std::string_view::npos &&
                              (named_at == std::string_view::npos || bare_at < named_at);
            const auto start = bare ? bare_at : named_at;
            if (start == std::string_view::npos)
                break;
            const auto directive = bare ? std::string_view{"DefineBareTemplate"}
                                        : std::string_view{"DefineTemplate"};
            const auto close_text = "@/" + std::string{directive};
            const auto close = contents.find(close_text, start + directive.size() + 1);
            if (close == std::string_view::npos)
            {
                issue(result.diagnostics, diagnostic_limit, "EM0803",
                      "@" + std::string{directive} + " is not closed.",
                      {source, start, contents.size()});
                break;
            }
            const auto end = close + close_text.size();
            std::string name;
            std::size_t body_start = start + directive.size() + 1;
            if (!bare)
            {
                const auto head_end = contents.find(')', body_start);
                if (head_end == std::string_view::npos || head_end > close)
                {
                    issue(result.diagnostics, diagnostic_limit, "EM0804",
                          "@DefineTemplate requires name: \"CapitalizedName\".",
                          {source, start, close});
                    position = end;
                    continue;
                }
                const auto head = std::string{contents.substr(body_start,
                                                               head_end - body_start + 1)};
                static const std::regex name_pattern{
                    R"(^\s*\(\s*name\s*:\s*\"([A-Z][A-Za-z0-9_]*)\"\s*\)\s*$)"};
                std::smatch match;
                if (!std::regex_match(head, match, name_pattern))
                {
                    issue(result.diagnostics, diagnostic_limit, "EM0804",
                          "@DefineTemplate requires name: \"CapitalizedName\".",
                          {source, start, head_end + 1});
                    position = end;
                    continue;
                }
                name = match[1].str();
                body_start = head_end + 1;
            }
            const auto range = SourceRange{source, start, end};
            const auto body = contents.substr(body_start, close - body_start);
            auto macro = parse_macro(name, bare, body, range, result.diagnostics,
                                     diagnostic_limit);
            if (macro)
            {
                if (bare)
                {
                    if (result.engine.bare)
                        issue(result.diagnostics, diagnostic_limit, "EM0805",
                              "An engine may define only one bare template.", range);
                    else
                        result.engine.bare = std::move(*macro);
                }
                else if (!capitalized_name(name))
                    issue(result.diagnostics, diagnostic_limit, "EM0806",
                          "Invalid deferred macro name “" + name + "”.", range);
                else if (!result.engine.macros.emplace(name, std::move(*macro)).second)
                    issue(result.diagnostics, diagnostic_limit, "EM0807",
                          "Deferred macro “" + name + "” is defined more than once.",
                          range);
                ++macro_count;
            }
            position = end;
        }
        if (macro_count == 0)
            issue(result.diagnostics, diagnostic_limit, "EM0808",
                  "An engine definition must declare at least one template.",
                  {source, 0, contents.size()});
        if (macro_count > 32)
            issue(result.diagnostics, diagnostic_limit, "EM0809",
                  "An engine may define at most 32 macros.",
                  {source, 0, contents.size()});
        return result;
    }

} // namespace email_markup
