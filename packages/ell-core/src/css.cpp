#include "ell/core/css.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace ell {
namespace {

std::string trim(std::string value) {
    const auto non_space = [](const unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), non_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(), value.end());
    return value;
}

}  // namespace

CssDeclarations parse_declarations(const std::string_view css) {
    CssDeclarations result;
    std::string current;
    bool quoted = false;
    int parentheses = 0;
    const auto commit = [&] {
        const auto colon = current.find(':');
        if (colon != std::string::npos) {
            auto name = trim(current.substr(0, colon));
            auto value = trim(current.substr(colon + 1));
            std::transform(name.begin(), name.end(), name.begin(),
                           [](const unsigned char ch) { return std::tolower(ch); });
            if (!name.empty() && !value.empty()) result[name] = value;
        }
        current.clear();
    };
    for (const char ch : css) {
        if (ch == '"') quoted = !quoted;
        if (!quoted && ch == '(') ++parentheses;
        if (!quoted && ch == ')' && parentheses > 0) --parentheses;
        if (!quoted && parentheses == 0 && ch == ';') commit();
        else current.push_back(ch);
    }
    commit();
    return result;
}

std::string serialize_declarations(const CssDeclarations& declarations) {
    std::string result;
    for (const auto& [name, value] : declarations) {
        if (!result.empty()) result.push_back(' ');
        result += name + ": " + value + ';';
    }
    return result;
}

std::string apply_root_styles(std::string html, const CssDeclarations& additions) {
    if (additions.empty()) return html;
    const auto open_end = html.find('>');
    if (open_end == std::string::npos) return html;
    auto tag = html.substr(0, open_end);
    static const std::regex style_pattern{R"(\sstyle\s*=\s*(["'])(.*?)\1)",
                                          std::regex::icase};
    std::smatch match;
    CssDeclarations merged;
    if (std::regex_search(tag, match, style_pattern)) {
        merged = parse_declarations(match[2].str());
        tag = std::regex_replace(tag, style_pattern, "");
    }
    merged.insert(additions.begin(), additions.end());
    for (const auto& [name, value] : additions) merged[name] = value;
    tag += " style=\"" + serialize_declarations(merged) + "\"";
    return tag + html.substr(open_end);
}

std::string inline_css(std::string html) {
    return html;
}

}  // namespace ell
