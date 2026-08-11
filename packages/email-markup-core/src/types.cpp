#include "email-markup/core/types.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

namespace email_markup
{
    namespace
    {

        struct ExactDecimal
        {
            bool negative{};
            std::string digits;
            std::int64_t scale{};
        };

        std::string trim(std::string value)
        {
            const auto keep = [](const unsigned char ch)
            { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), keep));
            value.erase(std::find_if(value.rbegin(), value.rend(), keep).base(), value.end());
            return value;
        }

        void error(std::vector<Diagnostic> &diagnostics, std::string code,
                   std::string message, const SourceRange range)
        {
            diagnostics.push_back({std::move(code), Severity::error, std::move(message), range});
        }

        std::optional<DeclarationType> declaration_type(const std::string_view name)
        {
            if (name == "string")
                return DeclarationType::string;
            if (name == "int")
                return DeclarationType::integer;
            if (name == "decimal")
                return DeclarationType::decimal;
            if (name == "number")
                return DeclarationType::number;
            if (name == "bool")
                return DeclarationType::boolean;
            if (name == "name")
                return DeclarationType::name;
            if (name == "url")
                return DeclarationType::url;
            if (name == "email")
                return DeclarationType::email;
            if (name == "color")
                return DeclarationType::color;
            if (name == "raw")
                return DeclarationType::raw;
            if (name == "path")
                return DeclarationType::path;
            if (name == "condition")
                return DeclarationType::condition;
            return std::nullopt;
        }

        bool numeric_type(const DeclarationType type)
        {
            return type == DeclarationType::integer || type == DeclarationType::decimal ||
                   type == DeclarationType::number;
        }

        std::optional<ExactDecimal> parse_exact_decimal(const std::string_view spelling)
        {
            std::size_t position = 0;
            ExactDecimal value;
            if (position < spelling.size() &&
                (spelling[position] == '+' || spelling[position] == '-'))
            {
                value.negative = spelling[position] == '-';
                ++position;
            }
            const auto whole_start = position;
            while (position < spelling.size() &&
                   std::isdigit(static_cast<unsigned char>(spelling[position])))
                ++position;
            if (position == whole_start)
                return std::nullopt;
            value.digits = std::string{spelling.substr(whole_start, position - whole_start)};
            if (position < spelling.size() && spelling[position] == '.')
            {
                ++position;
                const auto fraction_start = position;
                while (position < spelling.size() &&
                       std::isdigit(static_cast<unsigned char>(spelling[position])))
                    ++position;
                if (position == fraction_start)
                    return std::nullopt;
                value.digits += spelling.substr(fraction_start, position - fraction_start);
                value.scale = static_cast<std::int64_t>(position - fraction_start);
            }
            if (position < spelling.size() &&
                (spelling[position] == 'e' || spelling[position] == 'E'))
            {
                ++position;
                bool exponent_negative = false;
                if (position < spelling.size() &&
                    (spelling[position] == '+' || spelling[position] == '-'))
                {
                    exponent_negative = spelling[position] == '-';
                    ++position;
                }
                const auto exponent_start = position;
                std::int64_t exponent = 0;
                while (position < spelling.size() &&
                       std::isdigit(static_cast<unsigned char>(spelling[position])))
                {
                    if (exponent < 10000)
                        exponent = std::min<std::int64_t>(
                            10000, exponent * 10 + spelling[position] - '0');
                    ++position;
                }
                if (position == exponent_start)
                    return std::nullopt;
                value.scale += exponent_negative ? exponent : -exponent;
            }
            if (position != spelling.size())
                return std::nullopt;
            const auto nonzero = value.digits.find_first_not_of('0');
            if (nonzero == std::string::npos)
                return ExactDecimal{false, "0", 0};
            value.digits.erase(0, nonzero);
            while (value.scale > 0 && value.digits.ends_with('0'))
            {
                value.digits.pop_back();
                --value.scale;
            }
            return value;
        }

        int compare_exact(const std::string_view left, const std::string_view right)
        {
            const auto lhs = parse_exact_decimal(left);
            const auto rhs = parse_exact_decimal(right);
            if (!lhs || !rhs)
                return 0;
            if (lhs->negative != rhs->negative)
                return lhs->negative ? -1 : 1;
            const auto target_scale = std::max(lhs->scale, rhs->scale);
            auto left_digits = lhs->digits;
            auto right_digits = rhs->digits;
            left_digits.append(static_cast<std::size_t>(target_scale - lhs->scale), '0');
            right_digits.append(static_cast<std::size_t>(target_scale - rhs->scale), '0');
            const auto strip_zeroes = [](std::string &digits)
            {
                const auto nonzero = digits.find_first_not_of('0');
                if (nonzero == std::string::npos)
                    digits = "0";
                else
                    digits.erase(0, nonzero);
            };
            strip_zeroes(left_digits);
            strip_zeroes(right_digits);
            int result = 0;
            if (left_digits.size() != right_digits.size())
                result = left_digits.size() < right_digits.size() ? -1 : 1;
            else if (left_digits != right_digits)
                result = left_digits < right_digits ? -1 : 1;
            return lhs->negative ? -result : result;
        }

        std::size_t unicode_scalar_count(const std::string_view text)
        {
            return static_cast<std::size_t>(std::count_if(
                text.begin(), text.end(), [](const unsigned char ch)
                { return (ch & 0xc0U) != 0x80U; }));
        }

        class DeclarationLineParser
        {
        public:
            DeclarationLineParser(const std::string_view line, const SourceRange line_range,
                                  const DeclarationContext context,
                                  std::vector<Diagnostic> &diagnostics)
                : line_(line), line_range_(line_range), context_(context),
                  diagnostics_(diagnostics)
            {
            }

            std::optional<Declaration> parse()
            {
                Declaration declaration;
                declaration.range = line_range_;
                skip_space();
                const auto name_start = position_;
                declaration.name = identifier();
                if (declaration.name.empty())
                    return fail("EM0401", "Invalid declaration; expected a name.");
                declaration.name_range = source_range(name_start, position_);
                skip_space();
                if (take('?'))
                {
                    declaration.optional = true;
                    declaration.optional_range = source_range(position_ - 1, position_);
                    skip_space();
                }

                if (!take(':'))
                {
                    if (context_ == DeclarationContext::component_prop)
                        return fail("EM0405", "Component props require an explicit : type.");
                    declaration.type = "raw";
                    declaration.value_type = DeclarationType::raw;
                }
                else
                {
                    skip_space();
                    const auto type_start = position_;
                    declaration.type = identifier();
                    declaration.type_range = source_range(type_start, position_);
                    declaration.has_explicit_type = true;
                    if (declaration.type.empty())
                        return fail("EM0401", "Invalid declaration; expected a type after ':'.");
                    const auto parsed_type = declaration_type(declaration.type);
                    if (!parsed_type)
                        return fail("EM0404", "Unknown declaration type “" + declaration.type + "”.",
                                    declaration.type_range);
                    declaration.value_type = *parsed_type;
                    if (context_ == DeclarationContext::component_prop &&
                        (*parsed_type == DeclarationType::raw ||
                         *parsed_type == DeclarationType::path ||
                         *parsed_type == DeclarationType::condition))
                    {
                        return fail("EM0406", "Type “" + declaration.type +
                                                   "” is valid only for deferred macro parameters.",
                                    declaration.type_range);
                    }
                    if (context_ == DeclarationContext::deferred_parameter &&
                        (*parsed_type == DeclarationType::string ||
                         *parsed_type == DeclarationType::url ||
                         *parsed_type == DeclarationType::email ||
                         *parsed_type == DeclarationType::color))
                    {
                        return fail("EM0406", "Type “" + declaration.type +
                                                   "” is not a deferred macro parameter type.",
                                    declaration.type_range);
                    }
                }

                skip_space();
                if (take('('))
                {
                    skip_space();
                    auto minimum = bound(declaration.value_type);
                    if (!minimum)
                        return std::nullopt;
                    skip_space();
                    if (!take('.') || !take('.'))
                        return fail("EM0401", "Invalid range; expected '..' between its bounds.");
                    skip_space();
                    auto maximum = bound(declaration.value_type);
                    if (!maximum)
                        return std::nullopt;
                    skip_space();
                    if (!take(')'))
                        return fail("EM0401", "Invalid range; expected ')' after its bounds.");
                    declaration.range_constraint = RangeConstraint{std::move(*minimum),
                                                                   std::move(*maximum)};
                    if (declaration.value_type != DeclarationType::string &&
                        !numeric_type(declaration.value_type))
                    {
                        return fail("EM0407", "Type “" + declaration.type +
                                                   "” does not support range constraints.");
                    }
                    if (compare_exact(declaration.range_constraint->minimum.spelling,
                                      declaration.range_constraint->maximum.spelling) > 0)
                    {
                        return fail("EM0403", "Declaration range minimum exceeds its maximum.",
                                    {line_range_.source,
                                     declaration.range_constraint->minimum.range.start,
                                     declaration.range_constraint->maximum.range.end});
                    }
                    skip_space();
                }

                if (const auto comparison = comparison_operator())
                {
                    if (!numeric_type(declaration.value_type))
                        return fail("EM0407", "Only numeric declarations support comparisons.",
                                    comparison->second);
                    skip_space();
                    auto compared = bound(declaration.value_type);
                    if (!compared)
                        return std::nullopt;
                    declaration.comparison_constraint = ComparisonConstraint{
                        comparison->first, std::move(*compared), comparison->second};
                    skip_space();
                }

                if (take('='))
                {
                    const auto default_start = skip_space();
                    auto default_end = line_.size();
                    while (default_end > default_start &&
                           std::isspace(static_cast<unsigned char>(line_[default_end - 1])))
                        --default_end;
                    declaration.default_expression =
                        std::string{line_.substr(default_start, default_end - default_start)};
                    declaration.default_range = source_range(default_start, default_end);
                    declaration.has_default = true;
                    declaration.optional = true;
                    position_ = line_.size();
                    if (context_ == DeclarationContext::component_prop &&
                        declaration.default_expression.empty())
                        return fail("EM0401", "A component prop default cannot be empty.",
                                    declaration.default_range);
                    if (context_ == DeclarationContext::deferred_parameter &&
                        declaration.default_expression.find("@{") != std::string::npos)
                        return fail("EM0401", "Deferred parameter defaults cannot contain interpolation.",
                                    declaration.default_range);
                }
                skip_space();
                if (position_ != line_.size())
                    return fail("EM0401", "Unexpected text after the declaration.");

                if (declaration.range_constraint && declaration.comparison_constraint &&
                    constraints_contradict(declaration))
                    return fail("EM0409", "Declaration range and comparison contradict each other.");
                return declaration;
            }

        private:
            std::size_t skip_space()
            {
                while (position_ < line_.size() &&
                       std::isspace(static_cast<unsigned char>(line_[position_])))
                    ++position_;
                return position_;
            }

            bool take(const char expected)
            {
                if (position_ >= line_.size() || line_[position_] != expected)
                    return false;
                ++position_;
                return true;
            }

            std::string identifier()
            {
                if (position_ >= line_.size() ||
                    !(std::isalpha(static_cast<unsigned char>(line_[position_])) ||
                      line_[position_] == '_'))
                    return {};
                const auto start = position_++;
                while (position_ < line_.size() &&
                       (std::isalnum(static_cast<unsigned char>(line_[position_])) ||
                        line_[position_] == '_'))
                    ++position_;
                return std::string{line_.substr(start, position_ - start)};
            }

            std::optional<NumericBound> bound(const DeclarationType type)
            {
                const auto start = position_;
                if (position_ < line_.size() &&
                    (line_[position_] == '+' || line_[position_] == '-'))
                    ++position_;
                const auto digits = position_;
                while (position_ < line_.size() &&
                       std::isdigit(static_cast<unsigned char>(line_[position_])))
                    ++position_;
                if (position_ == digits)
                {
                    fail("EM0408", "Expected a numeric constraint bound.");
                    return std::nullopt;
                }
                bool integer = true;
                if (position_ < line_.size() && line_[position_] == '.' &&
                    !(position_ + 1 < line_.size() && line_[position_ + 1] == '.'))
                {
                    integer = false;
                    ++position_;
                    const auto fraction = position_;
                    while (position_ < line_.size() &&
                           std::isdigit(static_cast<unsigned char>(line_[position_])))
                        ++position_;
                    if (position_ == fraction)
                    {
                        fail("EM0408", "A decimal bound requires digits after '.'.");
                        return std::nullopt;
                    }
                }
                const auto parsed_range = source_range(start, position_);
                const auto spelling = std::string{line_.substr(start, position_ - start)};
                if (type == DeclarationType::integer && !integer)
                {
                    fail("EM0408", "Integer constraint bounds must be exact integers.", parsed_range);
                    return std::nullopt;
                }
                if (type == DeclarationType::integer)
                {
                    std::int64_t parsed{};
                    auto checked = std::string_view{spelling};
                    if (checked.starts_with('+'))
                        checked.remove_prefix(1);
                    const auto [end, status] =
                        std::from_chars(checked.data(), checked.data() + checked.size(), parsed);
                    if (status != std::errc{} || end != checked.data() + checked.size())
                    {
                        fail("EM0408", "Integer constraint bound is outside the supported integer range.",
                             parsed_range);
                        return std::nullopt;
                    }
                }
                if (type == DeclarationType::decimal && integer)
                {
                    fail("EM0408", "Decimal constraint bounds require a decimal point.", parsed_range);
                    return std::nullopt;
                }
                if (type == DeclarationType::string &&
                    (!integer || spelling.starts_with('-') || spelling.starts_with('+')))
                {
                    fail("EM0408", "String length bounds must be non-negative integers.", parsed_range);
                    return std::nullopt;
                }
                if (type == DeclarationType::string)
                {
                    std::size_t parsed{};
                    const auto [end, status] = std::from_chars(
                        spelling.data(), spelling.data() + spelling.size(), parsed);
                    if (status != std::errc{} || end != spelling.data() + spelling.size())
                    {
                        fail("EM0408", "String length bound is outside the supported size range.",
                             parsed_range);
                        return std::nullopt;
                    }
                }
                if (type != DeclarationType::string && !numeric_type(type))
                {
                    fail("EM0407", "This declaration type does not support numeric bounds.",
                         parsed_range);
                    return std::nullopt;
                }
                return NumericBound{spelling, integer, parsed_range};
            }

            std::optional<std::pair<ComparisonOperator, SourceRange>> comparison_operator()
            {
                if (position_ >= line_.size() ||
                    (line_[position_] != '<' && line_[position_] != '>'))
                    return std::nullopt;
                const auto start = position_;
                const bool greater = line_[position_++] == '>';
                const bool equal = take('=');
                const auto operation = greater
                                           ? (equal ? ComparisonOperator::greater_equal
                                                    : ComparisonOperator::greater)
                                           : (equal ? ComparisonOperator::less_equal
                                                    : ComparisonOperator::less);
                return std::pair{operation, source_range(start, position_)};
            }

            bool constraints_contradict(const Declaration &declaration) const
            {
                const auto &range = *declaration.range_constraint;
                const auto &comparison = *declaration.comparison_constraint;
                const auto versus_minimum =
                    compare_exact(comparison.bound.spelling, range.minimum.spelling);
                const auto versus_maximum =
                    compare_exact(comparison.bound.spelling, range.maximum.spelling);
                switch (comparison.operation)
                {
                case ComparisonOperator::greater:
                    return versus_maximum >= 0;
                case ComparisonOperator::greater_equal:
                    return versus_maximum > 0;
                case ComparisonOperator::less:
                    return versus_minimum <= 0;
                case ComparisonOperator::less_equal:
                    return versus_minimum < 0;
                }
                return false;
            }

            SourceRange source_range(const std::size_t start, const std::size_t end) const
            {
                return {line_range_.source, line_range_.start + start,
                        line_range_.start + end};
            }

            std::optional<Declaration> fail(std::string code, std::string message)
            {
                return fail(std::move(code), std::move(message),
                            source_range(position_, std::min(position_ + 1, line_.size())));
            }

            std::optional<Declaration> fail(std::string code, std::string message,
                                            const SourceRange range)
            {
                error(diagnostics_, std::move(code), std::move(message), range);
                return std::nullopt;
            }

            std::string_view line_;
            SourceRange line_range_;
            DeclarationContext context_;
            std::vector<Diagnostic> &diagnostics_;
            std::size_t position_{};
        };

        std::string comparison_text(const ComparisonOperator operation)
        {
            switch (operation)
            {
            case ComparisonOperator::greater:
                return ">";
            case ComparisonOperator::greater_equal:
                return ">=";
            case ComparisonOperator::less:
                return "<";
            case ComparisonOperator::less_equal:
                return "<=";
            }
            return {};
        }

    } // namespace

    std::vector<Declaration> parse_declarations(
        const std::string_view text, const SourceRange range,
        const DeclarationContext context, std::vector<Diagnostic> &diagnostics)
    {
        std::vector<Declaration> output;
        std::size_t line_start = 0;
        while (line_start <= text.size())
        {
            auto line_end = text.find('\n', line_start);
            if (line_end == std::string_view::npos)
                line_end = text.size();
            auto line = text.substr(line_start, line_end - line_start);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const auto line_range = SourceRange{range.source, range.start + line_start,
                                                range.start + line_start + line.size()};
            if (!trim(std::string{line}).empty())
            {
                DeclarationLineParser parser{line, line_range, context, diagnostics};
                if (auto declaration = parser.parse())
                {
                    const auto duplicate = std::find_if(
                        output.begin(), output.end(), [&](const auto &candidate)
                        { return candidate.name == declaration->name; });
                    if (duplicate != output.end())
                    {
                        error(diagnostics, "EM0402", "Declaration “" + declaration->name +
                                                         "” is declared more than once.",
                              declaration->name_range);
                    }
                    else
                    {
                        if (context == DeclarationContext::component_prop &&
                            declaration->has_default)
                        {
                            auto literal = Json::parse(declaration->default_expression, nullptr,
                                                       false);
                            if (!literal.is_discarded())
                                (void)validate_prop(*declaration, literal, diagnostics,
                                                    declaration->default_range);
                        }
                        output.push_back(std::move(*declaration));
                    }
                }
            }
            if (line_end == text.size())
                break;
            line_start = line_end + 1;
        }
        return output;
    }

    std::vector<PropDeclaration> parse_prop_declarations(
        const std::string_view text, const SourceRange range,
        std::vector<Diagnostic> &diagnostics)
    {
        return parse_declarations(text, range, DeclarationContext::component_prop,
                                  diagnostics);
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
                error(diagnostics, "EM0411", "Slot “" + match[1].str() +
                                                   "” is declared more than once.",
                      line_range);
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
        else
        {
            switch (declaration.value_type)
            {
            case DeclarationType::string:
            case DeclarationType::url:
            case DeclarationType::email:
            case DeclarationType::color:
            case DeclarationType::name:
                type_ok = value.is_string();
                break;
            case DeclarationType::integer:
                type_ok = value.is_number_integer();
                break;
            case DeclarationType::decimal:
                type_ok = value.is_number_float();
                break;
            case DeclarationType::number:
                type_ok = value.is_number();
                break;
            case DeclarationType::boolean:
                type_ok = value.is_boolean();
                break;
            case DeclarationType::raw:
            case DeclarationType::path:
            case DeclarationType::condition:
                type_ok = false;
                break;
            }
        }
        if (!type_ok)
        {
            error(diagnostics, "EM0420", "Prop “" + declaration.name + "” requires " +
                                               declaration.type + "; values are never coerced.",
                  value_range);
            return false;
        }
        if (value.is_null())
            return true;
        if (numeric_type(declaration.value_type) && value.is_number_float() &&
            !std::isfinite(value.get<double>()))
        {
            error(diagnostics, "EM0420", "Prop “" + declaration.name +
                                               "” requires a finite " + declaration.type + ".",
                  value_range);
            return false;
        }
        if (declaration.value_type == DeclarationType::url)
        {
            const auto &raw = value.get_ref<const std::string &>();
            static const std::regex url{R"(^([A-Za-z][A-Za-z0-9+.-]*:|/).+)"};
            if (!std::regex_match(raw, url))
            {
                error(diagnostics, "EM0421", "Prop “" + declaration.name +
                                                   "” is not a URL with a scheme or root-relative path.",
                      value_range);
                return false;
            }
        }
        if (declaration.value_type == DeclarationType::email)
        {
            static const std::regex email{R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)"};
            if (!std::regex_match(value.get_ref<const std::string &>(), email))
            {
                error(diagnostics, "EM0422", "Prop “" + declaration.name +
                                                   "” is not an email address.",
                      value_range);
                return false;
            }
        }
        if (declaration.value_type == DeclarationType::color)
        {
            static const std::regex color{
                R"(^(#[0-9a-fA-F]{3,8}|rgba?\([^)]*\)|hsla?\([^)]*\)|[A-Za-z]+)$)"};
            if (!std::regex_match(value.get_ref<const std::string &>(), color))
            {
                error(diagnostics, "EM0425", "Prop “" + declaration.name +
                                                   "” is not a CSS color.",
                      value_range);
                return false;
            }
        }
        if (declaration.value_type == DeclarationType::name)
        {
            static const std::regex name{R"(^[A-Za-z_][A-Za-z0-9_]*$)"};
            if (!std::regex_match(value.get_ref<const std::string &>(), name))
            {
                error(diagnostics, "EM0426", "Prop “" + declaration.name +
                                                   "” is not a valid name.",
                      value_range);
                return false;
            }
        }

        std::string measured;
        if (declaration.value_type == DeclarationType::string)
            measured = std::to_string(
                unicode_scalar_count(value.get_ref<const std::string &>()));
        else if (numeric_type(declaration.value_type))
            measured = value.dump();

        if (declaration.range_constraint)
        {
            const auto &constraint = *declaration.range_constraint;
            if (compare_exact(measured, constraint.minimum.spelling) < 0 ||
                compare_exact(measured, constraint.maximum.spelling) > 0)
            {
                error(diagnostics, "EM0423", "Prop “" + declaration.name +
                                                   "” is outside its declared range.",
                      value_range);
                return false;
            }
        }
        if (declaration.comparison_constraint)
        {
            const auto &constraint = *declaration.comparison_constraint;
            const auto compared = compare_exact(measured, constraint.bound.spelling);
            const bool valid = constraint.operation == ComparisonOperator::greater
                                   ? compared > 0
                               : constraint.operation == ComparisonOperator::greater_equal
                                   ? compared >= 0
                               : constraint.operation == ComparisonOperator::less
                                   ? compared < 0
                                   : compared <= 0;
            if (!valid)
            {
                error(diagnostics, "EM0424", "Prop “" + declaration.name +
                                                   "” violates its declared numeric bound.",
                      value_range);
                return false;
            }
        }
        return true;
    }

    std::string format_declaration(const Declaration &declaration)
    {
        std::string output = declaration.name;
        if (declaration.optional && !declaration.has_default)
            output += '?';
        if (declaration.has_explicit_type)
            output += ": " + declaration.type;
        if (declaration.range_constraint)
            output += '(' + declaration.range_constraint->minimum.spelling + ".." +
                      declaration.range_constraint->maximum.spelling + ')';
        if (declaration.comparison_constraint)
            output += ' ' + comparison_text(declaration.comparison_constraint->operation) + ' ' +
                      declaration.comparison_constraint->bound.spelling;
        if (declaration.has_default)
            output += " = " + declaration.default_expression;
        return output;
    }

} // namespace email_markup
