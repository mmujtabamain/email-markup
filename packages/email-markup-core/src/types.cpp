#include "email-markup/core/types.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

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

        void error(std::vector<Diagnostic> &diagnostics, std::string code,
                   std::string message, SourceRange range)
        {
            diagnostics.push_back({std::move(code), Severity::error, std::move(message), range});
        }

    } // namespace

    std::vector<PropDeclaration> parse_prop_declarations(
        const std::string_view text, const SourceRange range,
        std::vector<Diagnostic> &diagnostics)
    {
        static const std::regex pattern{
            R"(^\s*([A-Za-z_][A-Za-z0-9_]*)(\?)?\s*:\s*(string|int|number|bool|url|email|color)(?:\s*\(\s*(-?[0-9]+(?:\.[0-9]+)?)\s*\.\.\s*(-?[0-9]+(?:\.[0-9]+)?)\s*\))?(?:\s*(>=|<=|>|<)\s*(-?[0-9]+(?:\.[0-9]+)?))?(?:\s*=\s*(.+?))?\s*$)"};
        std::vector<PropDeclaration> output;
        std::istringstream stream{std::string{text}};
        std::string line;
        std::size_t offset = range.start;
        while (std::getline(stream, line))
        {
            const auto line_range = SourceRange{range.source, offset, offset + line.size()};
            offset += line.size() + 1;
            if (trim(line).empty())
                continue;
            std::smatch match;
            if (!std::regex_match(line, match, pattern))
            {
                error(diagnostics, "EM0401",
                      "Invalid prop declaration; expected name: type with optional constraints.",
                      line_range);
                continue;
            }
            if (std::any_of(output.begin(), output.end(), [&](const auto &prop)
                            { return prop.name == match[1].str(); }))
            {
                error(diagnostics, "EM0402", "Prop “" + match[1].str() + "” is declared more than once.", line_range);
                continue;
            }
            PropDeclaration declaration;
            declaration.name = match[1].str();
            declaration.optional = match[2].matched;
            declaration.type = match[3].str();
            declaration.range = line_range;
            if (match[4].matched)
            {
                declaration.minimum = std::stod(match[4].str());
                declaration.maximum = std::stod(match[5].str());
                declaration.has_range = true;
                if (declaration.minimum > declaration.maximum)
                {
                    error(diagnostics, "EM0403", "Prop range minimum exceeds its maximum.",
                          line_range);
                }
            }
            if (match[6].matched)
            {
                declaration.comparison = match[6].str();
                declaration.bound = std::stod(match[7].str());
            }
            if (match[8].matched)
            {
                declaration.default_expression = trim(match[8].str());
                declaration.has_default = true;
                declaration.optional = true;
            }
            output.push_back(std::move(declaration));
        }
        return output;
    }

    std::vector<SlotDeclaration> parse_slot_declarations(
        const std::string_view text, const SourceRange range,
        std::vector<Diagnostic> &diagnostics)
    {
        static const std::regex pattern{
            R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(required|optional)\s*$)"};
        std::vector<SlotDeclaration> output;
        std::istringstream stream{std::string{text}};
        std::string line;
        std::size_t offset = range.start;
        while (std::getline(stream, line))
        {
            const auto line_range = SourceRange{range.source, offset, offset + line.size()};
            offset += line.size() + 1;
            if (trim(line).empty())
                continue;
            std::smatch match;
            if (!std::regex_match(line, match, pattern))
            {
                error(diagnostics, "EM0410",
                      "Invalid slot declaration; expected name: required or name: optional.",
                      line_range);
                continue;
            }
            if (std::any_of(output.begin(), output.end(), [&](const auto &slot)
                            { return slot.name == match[1].str(); }))
            {
                error(diagnostics, "EM0411", "Slot “" + match[1].str() + "” is declared more than once.", line_range);
                continue;
            }
            output.push_back({match[1].str(), match[2].str() == "required", line_range});
        }
        return output;
    }

    bool validate_prop(const PropDeclaration &declaration, const Json &value,
                       std::vector<Diagnostic> &diagnostics,
                       const SourceRange value_range)
    {
        bool type_ok = false;
        if (value.is_null())
            type_ok = declaration.optional;
        else if (declaration.type == "string" || declaration.type == "url" ||
                 declaration.type == "email" || declaration.type == "color")
        {
            type_ok = value.is_string();
        }
        else if (declaration.type == "int")
            type_ok = value.is_number_integer();
        else if (declaration.type == "number")
            type_ok = value.is_number();
        else if (declaration.type == "bool")
            type_ok = value.is_boolean();
        if (!type_ok)
        {
            error(diagnostics, "EM0420", "Prop “" + declaration.name + "” requires " + declaration.type + "; values are never coerced.", value_range);
            return false;
        }
        if (value.is_null())
            return true;
        if (declaration.type == "url")
        {
            const auto &raw = value.get_ref<const std::string &>();
            static const std::regex url{R"(^([A-Za-z][A-Za-z0-9+.-]*:|/).+)"};
            if (!std::regex_match(raw, url))
            {
                error(diagnostics, "EM0421", "Prop “" + declaration.name + "” is not a URL with a scheme or root-relative path.", value_range);
                return false;
            }
        }
        if (declaration.type == "email")
        {
            static const std::regex email{R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)"};
            if (!std::regex_match(value.get_ref<const std::string &>(), email))
            {
                error(diagnostics, "EM0422", "Prop “" + declaration.name + "” is not an email address.", value_range);
                return false;
            }
        }
        if (declaration.type == "color")
        {
            static const std::regex color{
                R"(^(#[0-9a-fA-F]{3,8}|rgba?\([^)]*\)|hsla?\([^)]*\)|[A-Za-z]+)$)"};
            if (!std::regex_match(value.get_ref<const std::string &>(), color))
            {
                error(diagnostics, "EM0425", "Prop “" + declaration.name + "” is not a CSS color.", value_range);
                return false;
            }
        }
        if (declaration.has_range)
        {
            const auto measured = value.is_string()
                                      ? static_cast<double>(value.get_ref<const std::string &>().size())
                                      : value.get<double>();
            if (measured < declaration.minimum || measured > declaration.maximum)
            {
                error(diagnostics, "EM0423", "Prop “" + declaration.name + "” is outside its declared range.", value_range);
                return false;
            }
        }
        if (!declaration.comparison.empty())
        {
            const auto measured = value.get<double>();
            const bool valid = declaration.comparison == ">="   ? measured >= declaration.bound
                               : declaration.comparison == "<=" ? measured <= declaration.bound
                               : declaration.comparison == ">"  ? measured > declaration.bound
                                                                : measured < declaration.bound;
            if (!valid)
            {
                error(diagnostics, "EM0424", "Prop “" + declaration.name + "” violates its declared numeric bound.", value_range);
                return false;
            }
        }
        return true;
    }

} // namespace email_markup
