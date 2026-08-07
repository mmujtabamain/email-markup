#include "email-markup/core/expr.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace email_markup {
namespace {

enum class Kind { end, number, string, identifier, lparen, rparen, op };
struct ExprToken { Kind kind; std::string text; };

class Scanner {
public:
    explicit Scanner(const std::string_view source) : source_(source) {}

    ExprToken next() {
        while (position_ < source_.size() && std::isspace(
                   static_cast<unsigned char>(source_[position_]))) ++position_;
        if (position_ == source_.size()) return {Kind::end, {}};
        const auto start = position_;
        const char ch = source_[position_++];
        if (ch == '(') return {Kind::lparen, "("};
        if (ch == ')') return {Kind::rparen, ")"};
        if (ch == '"') {
            bool escaped = false;
            while (position_ < source_.size()) {
                const char current = source_[position_++];
                if (current == '"' && !escaped) break;
                escaped = current == '\\' && !escaped;
                if (current != '\\') escaped = false;
            }
            return {Kind::string, std::string{source_.substr(start, position_ - start)}};
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) ||
            (ch == '.' && position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])))) {
            while (position_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            if (position_ < source_.size() && source_[position_] == '.') {
                ++position_;
                while (position_ < source_.size() &&
                       std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            }
            if (position_ < source_.size() &&
                (source_[position_] == 'e' || source_[position_] == 'E')) {
                ++position_;
                if (position_ < source_.size() &&
                    (source_[position_] == '+' || source_[position_] == '-')) ++position_;
                while (position_ < source_.size() &&
                       std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            }
            return {Kind::number, std::string{source_.substr(start, position_ - start)}};
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            while (position_ < source_.size()) {
                const char current = source_[position_];
                if (!std::isalnum(static_cast<unsigned char>(current)) &&
                    current != '_' && current != '.') break;
                ++position_;
            }
            const auto text = std::string{source_.substr(start, position_ - start)};
            if (text == "and" || text == "or" || text == "not") return {Kind::op, text};
            return {Kind::identifier, text};
        }
        if (position_ < source_.size()) {
            const auto pair = std::string{source_.substr(start, 2)};
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") {
                ++position_;
                return {Kind::op, pair};
            }
        }
        if (std::string_view{"+-*/%<>"}.find(ch) != std::string_view::npos) {
            return {Kind::op, std::string(1, ch)};
        }
        return {Kind::op, std::string(1, ch)};
    }

private:
    std::string_view source_;
    std::size_t position_{};
};

class ExpressionParser {
public:
    ExpressionParser(const std::string_view source, const EvaluationContext& context,
                     const SourceRange range)
        : scanner_(source), context_(context), range_(range) {
        advance();
    }

    EvaluationResult run() {
        EvaluationResult result;
        try {
            result.value = parse_or(true);
            if (current_.kind != Kind::end) fail("Unexpected token “" + current_.text + "”.");
            result.ok = diagnostics_.empty();
        } catch (const std::runtime_error&) {
            result.ok = false;
        }
        result.diagnostics = std::move(diagnostics_);
        return result;
    }

private:
    void advance() { current_ = scanner_.next(); }
    [[noreturn]] void fail(std::string message, std::string json_path = {}) {
        diagnostics_.push_back({"EM1200", Severity::error, std::move(message), range_, {},
                                std::move(json_path)});
        throw std::runtime_error("expression error");
    }
    bool take(const std::string_view op) {
        if (current_.kind == Kind::op && current_.text == op) {
            advance();
            return true;
        }
        return false;
    }

    Json parse_or(const bool enabled) {
        auto lhs = parse_and(enabled);
        while (take("or")) {
            const bool shorted = enabled && truthy(lhs);
            auto rhs = parse_and(enabled && !shorted);
            if (enabled) lhs = shorted ? lhs : rhs;
        }
        return lhs;
    }

    Json parse_and(const bool enabled) {
        auto lhs = parse_comparison(enabled);
        while (take("and")) {
            const bool shorted = enabled && !truthy(lhs);
            auto rhs = parse_comparison(enabled && !shorted);
            if (enabled) lhs = shorted ? lhs : rhs;
        }
        return lhs;
    }

    static bool numeric(const Json& value) {
        return value.is_number_integer() || value.is_number_unsigned() || value.is_number_float();
    }

    Json parse_comparison(const bool enabled) {
        auto lhs = parse_add(enabled);
        while (current_.kind == Kind::op &&
               (current_.text == "==" || current_.text == "!=" ||
                current_.text == "<" || current_.text == "<=" ||
                current_.text == ">" || current_.text == ">=")) {
            const auto op = current_.text;
            advance();
            auto rhs = parse_add(enabled);
            if (!enabled) continue;
            if (!(numeric(lhs) && numeric(rhs)) && lhs.type() != rhs.type()) {
                fail("Comparison operands must have compatible scalar types.");
            }
            if (lhs.is_array() || lhs.is_object() || lhs.is_null()) {
                fail("Arrays, objects, and null cannot be ordered or compared here.");
            }
            bool value = false;
            if (numeric(lhs)) {
                const auto left = lhs.get<long double>();
                const auto right = rhs.get<long double>();
                if (op == "==") value = left == right;
                else if (op == "!=") value = left != right;
                else if (op == "<") value = left < right;
                else if (op == "<=") value = left <= right;
                else if (op == ">") value = left > right;
                else value = left >= right;
            } else if (lhs.is_string()) {
                const auto left = lhs.get<std::string>();
                const auto right = rhs.get<std::string>();
                if (op == "==") value = left == right;
                else if (op == "!=") value = left != right;
                else if (op == "<") value = left < right;
                else if (op == "<=") value = left <= right;
                else if (op == ">") value = left > right;
                else value = left >= right;
            } else if (lhs.is_boolean() && (op == "==" || op == "!=")) {
                value = lhs.get<bool>() == rhs.get<bool>();
                if (op == "!=") value = !value;
            } else {
                fail("These values cannot be compared with “" + op + "”.");
            }
            lhs = value;
        }
        return lhs;
    }

    Json parse_add(const bool enabled) {
        auto lhs = parse_multiply(enabled);
        while (current_.kind == Kind::op && (current_.text == "+" || current_.text == "-")) {
            const auto op = current_.text;
            advance();
            auto rhs = parse_multiply(enabled);
            if (!enabled) continue;
            if (op == "+" && lhs.is_string() && rhs.is_string()) {
                lhs = lhs.get<std::string>() + rhs.get<std::string>();
                continue;
            }
            lhs = arithmetic(lhs, rhs, op);
        }
        return lhs;
    }

    Json parse_multiply(const bool enabled) {
        auto lhs = parse_unary(enabled);
        while (current_.kind == Kind::op &&
               (current_.text == "*" || current_.text == "/" || current_.text == "%")) {
            const auto op = current_.text;
            advance();
            auto rhs = parse_unary(enabled);
            if (enabled) lhs = arithmetic(lhs, rhs, op);
        }
        return lhs;
    }

    Json arithmetic(const Json& lhs, const Json& rhs, const std::string& op) {
        if (!numeric(lhs) || !numeric(rhs)) fail("Arithmetic operands must be numbers.");
        if ((op == "/" || op == "%") && rhs.get<long double>() == 0) {
            fail(op == "/" ? "Division by zero." : "Modulo by zero.");
        }
        const bool integers = lhs.is_number_integer() && rhs.is_number_integer();
        if (integers && op != "/") {
            const auto left = lhs.get<std::int64_t>();
            const auto right = rhs.get<std::int64_t>();
            std::int64_t output{};
            bool overflow = false;
            if (op == "+") {
                overflow = (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
                           (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right);
                if (!overflow) output = left + right;
            } else if (op == "-") {
                overflow = (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
                           (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right);
                if (!overflow) output = left - right;
            } else if (op == "*") {
                if (left != 0 && right != 0) {
                    if (left == -1) overflow = right == std::numeric_limits<std::int64_t>::min();
                    else if (right == -1) overflow = left == std::numeric_limits<std::int64_t>::min();
                    else if (left > 0) overflow = right > 0
                        ? left > std::numeric_limits<std::int64_t>::max() / right
                        : right < std::numeric_limits<std::int64_t>::min() / left;
                    else overflow = right > 0
                        ? left < std::numeric_limits<std::int64_t>::min() / right
                        : left < std::numeric_limits<std::int64_t>::max() / right;
                }
                if (!overflow) output = left * right;
            } else output = left % right;
            if (overflow) fail("Integer overflow.");
            return output;
        }
        const auto left = lhs.get<long double>();
        const auto right = rhs.get<long double>();
        long double output{};
        if (op == "+") output = left + right;
        else if (op == "-") output = left - right;
        else if (op == "*") output = left * right;
        else if (op == "/") output = left / right;
        else output = std::fmod(left, right);
        if (!std::isfinite(output) || output > std::numeric_limits<double>::max()) {
            fail("Numeric overflow.");
        }
        return static_cast<double>(output);
    }

    Json parse_unary(const bool enabled) {
        if (take("not")) {
            auto value = parse_unary(enabled);
            return enabled ? Json{!truthy(value)} : Json{};
        }
        if (take("-")) {
            auto value = parse_unary(enabled);
            if (!enabled) return {};
            if (!numeric(value)) fail("Unary minus requires a number.");
            if (value.is_number_integer()) {
                const auto integer = value.get<std::int64_t>();
                if (integer == std::numeric_limits<std::int64_t>::min()) fail("Integer overflow.");
                return -integer;
            }
            return -value.get<double>();
        }
        return parse_primary(enabled);
    }

    Json parse_primary(const bool enabled) {
        if (current_.kind == Kind::lparen) {
            advance();
            auto value = parse_or(enabled);
            if (current_.kind != Kind::rparen) fail("Expected a closing parenthesis.");
            advance();
            return value;
        }
        if (current_.kind == Kind::string) {
            const auto raw = current_.text;
            advance();
            if (!enabled) return {};
            try { return Json::parse(raw); }
            catch (...) { fail("Invalid string literal."); }
        }
        if (current_.kind == Kind::number) {
            const auto raw = current_.text;
            advance();
            if (!enabled) return {};
            try {
                if (raw.find_first_of(".eE") != std::string::npos) return std::stod(raw);
                return std::stoll(raw);
            } catch (...) { fail("Invalid or overflowing number literal."); }
        }
        if (current_.kind != Kind::identifier) fail("Expected an expression value.");
        const auto name = current_.text;
        advance();
        if (!enabled) return {};
        if (name == "true") return true;
        if (name == "false") return false;
        if (name == "null") return nullptr;
        return resolve_path(name);
    }

    Json resolve_path(const std::string& path) {
        const auto dot = path.find('.');
        const auto root = path.substr(0, dot);
        const Json* value = nullptr;
        if (const auto found = context_.locals.find(root); found != context_.locals.end()) {
            value = &found->second;
        } else if (const auto found = context_.props.find(root); found != context_.props.end()) {
            value = &found->second;
        } else if (root == "token") {
            static thread_local Json token_object;
            token_object = context_.tokens;
            value = &token_object;
        } else if (context_.data && context_.data->contains(root)) {
            value = &context_.data->at(root);
        }
        if (!value) fail("Missing compile-data path “" + path + "”.", path);
        std::size_t start = dot == std::string::npos ? path.size() : dot + 1;
        while (start < path.size()) {
            const auto next = path.find('.', start);
            const auto key = path.substr(start, next - start);
            if (!value->is_object() || !value->contains(key)) {
                fail("Missing compile-data path “" + path + "”.", path);
            }
            value = &value->at(key);
            if (next == std::string::npos) break;
            start = next + 1;
        }
        return *value;
    }

    Scanner scanner_;
    const EvaluationContext& context_;
    SourceRange range_;
    ExprToken current_{Kind::end, {}};
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace

EvaluationResult evaluate_expression(const std::string_view expression,
                                     const EvaluationContext& context,
                                     const SourceRange range) {
    return ExpressionParser{expression, context, range}.run();
}

std::string emit_scalar(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number_integer()) return std::to_string(value.get<std::int64_t>());
    if (value.is_number_unsigned()) return std::to_string(value.get<std::uint64_t>());
    if (value.is_number_float()) {
        auto text = value.dump();
        if (text == "-0.0") return "0";
        return text;
    }
    throw std::invalid_argument("Email Markup cannot interpolate null, arrays, or objects");
}

bool truthy(const Json& value) noexcept {
    if (value.is_null()) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value != 0;
    if (value.is_string()) return !value.get_ref<const std::string&>().empty();
    if (value.is_array() || value.is_object()) return !value.empty();
    return false;
}

}  // namespace email_markup
