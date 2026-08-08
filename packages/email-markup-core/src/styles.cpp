#include "email-markup/core/styles.hpp"

#include <regex>
#include <sstream>

namespace email_markup
{
    namespace
    {

        std::string interpolate(std::string text, const EvaluationContext &context,
                                const SourceRange range, std::vector<Diagnostic> &diagnostics)
        {
            static const std::regex expression{R"(@\{([^{}]+)\})"};
            std::string output;
            std::sregex_iterator current{text.begin(), text.end(), expression};
            const std::sregex_iterator end;
            std::size_t position = 0;
            for (; current != end; ++current)
            {
                output += text.substr(position, static_cast<std::size_t>(current->position()) - position);
                auto evaluated = evaluate_expression((*current)[1].str(), context, range);
                diagnostics.insert(diagnostics.end(), evaluated.diagnostics.begin(),
                                   evaluated.diagnostics.end());
                if (evaluated.ok)
                {
                    try
                    {
                        output += emit_scalar(evaluated.value);
                    }
                    catch (...)
                    {
                        diagnostics.push_back({"EM0501", Severity::error,
                                               "A style interpolation must produce a scalar value.", range});
                    }
                }
                position = static_cast<std::size_t>(current->position() + current->length());
            }
            output += text.substr(position);
            return output;
        }

    } // namespace

    CssDeclarations resolve_style_layers(
        const std::string_view bundle_names, const std::string_view call_css,
        const std::unordered_map<std::string, StyleDefinition> &styles,
        const EvaluationContext &context, const SourceRange range,
        std::vector<Diagnostic> &diagnostics)
    {
        CssDeclarations result;
        std::istringstream names{std::string{bundle_names}};
        std::string name;
        while (names >> name)
        {
            const auto found = styles.find(name);
            if (found == styles.end())
            {
                diagnostics.push_back({"EM0502", Severity::error,
                                       "Unknown style bundle “" + name + "”.", range});
                continue;
            }
            const auto parsed = parse_declarations(
                interpolate(found->second.declarations, context, found->second.range,
                            diagnostics));
            for (const auto &[property, value] : parsed)
                result[property] = value;
        }
        const auto call = parse_declarations(interpolate(std::string{call_css}, context,
                                                         range, diagnostics));
        for (const auto &[property, value] : call)
            result[property] = value;
        return result;
    }

    std::vector<Diagnostic> validate_media(const MediaDefinition &media)
    {
        std::vector<Diagnostic> diagnostics;
        int braces = 0;
        for (const char ch : media.css)
        {
            if (ch == '{')
                ++braces;
            else if (ch == '}')
                --braces;
            if (braces < 0)
                break;
        }
        if (braces != 0)
        {
            diagnostics.push_back({"EM0510", Severity::error,
                                   "Unbalanced @Media braces; Gmail would discard the block.",
                                   media.range});
        }
        if (media.query.empty())
        {
            diagnostics.push_back({"EM0511", Severity::error,
                                   "A media query cannot be empty.", media.range});
        }
        return diagnostics;
    }

} // namespace email_markup
