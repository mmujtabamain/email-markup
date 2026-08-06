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
    static const std::regex style_block{R"(<style\b[^>]*>([\s\S]*?)</style\s*>)",
                                        std::regex::icase};
    static const std::regex class_rule{R"(\.([A-Za-z_][A-Za-z0-9_-]*)\s*\{([^{}]*)\})"};
    std::map<std::string, CssDeclarations> classes;
    std::string without_styles;
    std::size_t position = 0;
    for (std::sregex_iterator it{html.begin(), html.end(), style_block}, end; it != end; ++it) {
        without_styles += html.substr(position,
            static_cast<std::size_t>(it->position()) - position);
        const auto body = (*it)[1].str();
        if (body.find("@media") != std::string::npos) {
            without_styles += it->str();
        } else {
            for (std::sregex_iterator rule{body.begin(), body.end(), class_rule}, rules_end;
                 rule != rules_end; ++rule) {
                const auto parsed = parse_declarations((*rule)[2].str());
                auto& target = classes[(*rule)[1].str()];
                for (const auto& [name, value] : parsed) target[name] = value;
            }
        }
        position = static_cast<std::size_t>(it->position() + it->length());
    }
    without_styles += html.substr(position);
    if (classes.empty()) return without_styles;

    static const std::regex tag_pattern{R"(<[A-Za-z][^<>]*>)"};
    static const std::regex class_attribute{R"(\sclass\s*=\s*(["'])(.*?)\1)",
                                            std::regex::icase};
    static const std::regex style_attribute{R"(\sstyle\s*=\s*(["'])(.*?)\1)",
                                            std::regex::icase};
    std::string output;
    position = 0;
    for (std::sregex_iterator it{without_styles.begin(), without_styles.end(), tag_pattern}, end;
         it != end; ++it) {
        output += without_styles.substr(position,
            static_cast<std::size_t>(it->position()) - position);
        auto tag = it->str();
        std::smatch class_match;
        if (std::regex_search(tag, class_match, class_attribute)) {
            CssDeclarations merged;
            std::istringstream names{class_match[2].str()};
            std::string name;
            while (names >> name) {
                if (const auto found = classes.find(name); found != classes.end())
                    for (const auto& [property, value] : found->second)
                        merged[property] = value;
            }
            std::smatch style_match;
            if (std::regex_search(tag, style_match, style_attribute)) {
                const auto inline_declarations = parse_declarations(style_match[2].str());
                for (const auto& [property, value] : inline_declarations)
                    merged[property] = value;
                tag = std::regex_replace(tag, style_attribute, "");
            }
            if (!merged.empty()) {
                tag.pop_back();
                tag += " style=\"" + serialize_declarations(merged) + "\">";
            }
        }
        output += tag;
        position = static_cast<std::size_t>(it->position() + it->length());
    }
    output += without_styles.substr(position);
    return output;
}

}  // namespace ell
