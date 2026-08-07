#include "email-markup/core/css.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace email_markup {
namespace {

std::string trim(std::string value) {
    const auto non_space = [](const unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), non_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(), value.end());
    return value;
}

void append_slice(GeneratedHtml& output, const GeneratedHtml& input,
                  const std::size_t start, const std::size_t end) {
    if (start >= end) return;
    for (const auto& segment : input.segments) {
        const auto slice_start = std::max(start, segment.output_start);
        const auto slice_end = std::min(end, segment.output_end);
        if (slice_start >= slice_end) continue;
        output.append(std::string_view{input.html}.substr(slice_start, slice_end - slice_start),
                      segment.origin, segment.expansion_stack);
    }
}

const OutputSegment* segment_at(const GeneratedHtml& input, const std::size_t offset) {
    const auto found = std::find_if(input.segments.begin(), input.segments.end(),
        [&](const auto& segment) {
            return offset >= segment.output_start && offset < segment.output_end;
        });
    return found == input.segments.end() ? nullptr : &*found;
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
    static const std::regex css_rule{R"(([^{}]+)\{([^{}]*)\})"};
    static const std::regex class_selector{R"(^\.([A-Za-z_][A-Za-z0-9_-]*)$)"};
    struct ClassRule {
        std::string name;
        CssDeclarations declarations;
    };
    std::vector<ClassRule> classes;
    std::string without_styles;
    std::size_t position = 0;
    for (std::sregex_iterator it{html.begin(), html.end(), style_block}, end; it != end; ++it) {
        without_styles += html.substr(position,
            static_cast<std::size_t>(it->position()) - position);
        const auto body = (*it)[1].str();
        if (body.find("@media") != std::string::npos) {
            without_styles += it->str();
        } else {
            for (std::sregex_iterator rule{body.begin(), body.end(), css_rule}, rules_end;
                 rule != rules_end; ++rule) {
                const auto parsed = parse_declarations((*rule)[2].str());
                std::istringstream selectors{(*rule)[1].str()};
                std::string selector;
                while (std::getline(selectors, selector, ',')) {
                    std::smatch match;
                    selector = trim(std::move(selector));
                    if (std::regex_match(selector, match, class_selector))
                        classes.push_back({match[1].str(), parsed});
                }
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
            std::set<std::string> names;
            std::istringstream class_names{class_match[2].str()};
            std::string name;
            while (class_names >> name) names.insert(name);
            for (const auto& rule : classes) {
                if (names.contains(rule.name))
                    for (const auto& [property, value] : rule.declarations)
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

GeneratedHtml inline_css(GeneratedHtml generated) {
    static const std::regex style_block{R"(<style\b[^>]*>([\s\S]*?)</style\s*>)",
                                        std::regex::icase};
    static const std::regex css_rule{R"(([^{}]+)\{([^{}]*)\})"};
    static const std::regex class_selector{R"(^\.([A-Za-z_][A-Za-z0-9_-]*)$)"};
    struct ClassRule {
        std::string name;
        CssDeclarations declarations;
    };
    std::vector<ClassRule> classes;
    GeneratedHtml without_styles;
    std::size_t position = 0;
    for (std::sregex_iterator it{generated.html.begin(), generated.html.end(), style_block}, end;
         it != end; ++it) {
        const auto block_start = static_cast<std::size_t>(it->position());
        const auto block_end = block_start + static_cast<std::size_t>(it->length());
        append_slice(without_styles, generated, position, block_start);
        const auto body = (*it)[1].str();
        if (body.find("@media") != std::string::npos) {
            append_slice(without_styles, generated, block_start, block_end);
        } else {
            for (std::sregex_iterator rule{body.begin(), body.end(), css_rule}, rules_end;
                 rule != rules_end; ++rule) {
                const auto parsed = parse_declarations((*rule)[2].str());
                std::istringstream selectors{(*rule)[1].str()};
                std::string selector;
                while (std::getline(selectors, selector, ',')) {
                    std::smatch match;
                    selector = trim(std::move(selector));
                    if (std::regex_match(selector, match, class_selector))
                        classes.push_back({match[1].str(), parsed});
                }
            }
        }
        position = block_end;
    }
    append_slice(without_styles, generated, position, generated.html.size());
    if (classes.empty()) return without_styles;

    static const std::regex tag_pattern{R"(<[A-Za-z][^<>]*>)"};
    static const std::regex class_attribute{R"(\sclass\s*=\s*(["'])(.*?)\1)",
                                            std::regex::icase};
    static const std::regex style_attribute{R"(\sstyle\s*=\s*(["'])(.*?)\1)",
                                            std::regex::icase};
    GeneratedHtml output;
    position = 0;
    for (std::sregex_iterator it{without_styles.html.begin(), without_styles.html.end(), tag_pattern}, end;
         it != end; ++it) {
        const auto tag_start = static_cast<std::size_t>(it->position());
        const auto tag_end = tag_start + static_cast<std::size_t>(it->length());
        append_slice(output, without_styles, position, tag_start);
        auto tag = it->str();
        const auto original = tag;
        std::smatch class_match;
        if (std::regex_search(tag, class_match, class_attribute)) {
            CssDeclarations merged;
            std::set<std::string> names;
            std::istringstream class_names{class_match[2].str()};
            std::string name;
            while (class_names >> name) names.insert(name);
            for (const auto& rule : classes) {
                if (names.contains(rule.name))
                    for (const auto& [property, value] : rule.declarations)
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
        if (tag == original) {
            append_slice(output, without_styles, tag_start, tag_end);
        } else if (const auto* segment = segment_at(without_styles, tag_start)) {
            output.append(tag, segment->origin, segment->expansion_stack);
        } else {
            output.append(tag, {});
        }
        position = tag_end;
    }
    append_slice(output, without_styles, position, without_styles.html.size());
    return output;
}

}  // namespace email_markup
