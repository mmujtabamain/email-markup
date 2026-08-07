#include "email-markup/core/lint.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <stack>
#include <unordered_set>

namespace email_markup {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return std::tolower(ch); });
    return value;
}

template <typename RangeAt>
std::vector<Diagnostic> lint_html_impl(const std::string_view html, const LintRole role,
                                      const SourceRange fallback,
                                      const std::size_t gmail_limit,
                                      RangeAt&& range_at) {
    std::vector<Diagnostic> output;
    const auto lowered = lower(std::string{html});
    const auto finding = [&](std::string code, const Severity severity,
                             std::string message, const std::size_t offset) {
        output.push_back({std::move(code), severity, std::move(message), range_at(offset)});
    };
    for (const auto& tag : {"script", "iframe", "object", "embed", "form"}) {
        const std::regex pattern{"<\\s*" + std::string{tag} + "\\b", std::regex::icase};
        for (std::sregex_iterator it{lowered.begin(), lowered.end(), pattern}, end;
             it != end; ++it) {
            finding("EM0801", Severity::error,
                    "Forbidden email HTML element <" + std::string{tag} + ">.",
                    static_cast<std::size_t>(it->position()));
        }
    }
    static const std::regex stylesheet{
        R"(<link\b[^>]*rel\s*=\s*["']?stylesheet)", std::regex::icase};
    for (std::sregex_iterator it{lowered.begin(), lowered.end(), stylesheet}, end;
         it != end; ++it) {
        finding("EM0802", Severity::error,
                "External stylesheets are not deliverable email HTML.",
                static_cast<std::size_t>(it->position()));
    }
    if (role == LintRole::content) {
        static const std::regex style{R"(<style\b)", std::regex::icase};
        for (std::sregex_iterator it{lowered.begin(), lowered.end(), style}, end;
             it != end; ++it) {
            finding("EM0803", Severity::error,
                    "Content cannot emit a <style> block; media styles belong to the shell.",
                    static_cast<std::size_t>(it->position()));
        }
    }
    static const std::regex image{R"(<img\b([^>]*)>)", std::regex::icase};
    for (std::sregex_iterator it{lowered.begin(), lowered.end(), image}, end; it != end; ++it) {
        const auto attributes = (*it)[1].str();
        const auto offset = static_cast<std::size_t>(it->position());
        if (attributes.find("alt=") == std::string::npos) {
            finding("EM0810", Severity::warning, "Image has no alt attribute.", offset);
        }
        if (attributes.find("src=\"http://") != std::string::npos ||
            attributes.find("src='http://") != std::string::npos) {
            finding("EM0811", Severity::warning, "Image uses insecure HTTP.", offset);
        }
    }
    for (const auto& unsupported : {"display: flex", "display:flex", "display: grid",
                                    "display:grid", "position: fixed", "position:fixed"}) {
        std::size_t offset = 0;
        while ((offset = lowered.find(unsupported, offset)) != std::string::npos) {
            finding("EM0812", Severity::warning,
                    "CSS feature has poor email-client support: " +
                    std::string{unsupported} + ".", offset);
            offset += std::string_view{unsupported}.size();
        }
    }
    if (html.size() > gmail_limit) {
        output.push_back({"EM0813", Severity::warning,
                          "Generated HTML exceeds Gmail's 102,400-byte clipping threshold.",
                          fallback});
    }
    if (role == LintRole::shell && lowered.find("unsubscribe") == std::string::npos) {
        output.push_back({"EM0814", Severity::error,
                          "Final email requires a visible unsubscribe target.", fallback});
    }

    static const std::regex tag_pattern{R"(<\s*(/?)\s*([a-zA-Z][a-zA-Z0-9:-]*)[^>]*>)"};
    const std::unordered_set<std::string> void_elements{
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
        "param", "source", "track", "wbr"};
    std::vector<std::pair<std::string, std::size_t>> stack;
    for (std::sregex_iterator it{lowered.begin(), lowered.end(), tag_pattern}, end;
         it != end; ++it) {
        const bool closing = (*it)[1].matched && !(*it)[1].str().empty();
        const auto name = (*it)[2].str();
        const auto offset = static_cast<std::size_t>(it->position());
        if (void_elements.contains(name) || it->str().ends_with("/>")) continue;
        if (!closing) stack.emplace_back(name, offset);
        else if (stack.empty() || stack.back().first != name) {
            finding("EM0820", Severity::error,
                    "Unbalanced HTML close tag </" + name + ">.", offset);
        } else stack.pop_back();
    }
    if (!stack.empty()) {
        finding("EM0821", Severity::error,
                "Unclosed HTML element <" + stack.back().first + ">.",
                stack.back().second);
    }
    return output;
}

}  // namespace

std::vector<Diagnostic> lint_html(const std::string_view html, const LintRole role,
                                  const SourceRange fallback,
                                  const std::size_t gmail_limit) {
    return lint_html_impl(html, role, fallback, gmail_limit,
                          [&](const std::size_t) { return fallback; });
}

std::vector<Diagnostic> lint_html(const GeneratedHtml& generated, const LintRole role,
                                  const SourceRange fallback,
                                  const std::size_t gmail_limit) {
    return lint_html_impl(generated.html, role, fallback, gmail_limit,
        [&](const std::size_t offset) {
            const auto found = std::find_if(generated.segments.begin(), generated.segments.end(),
                [&](const auto& segment) {
                    return offset >= segment.output_start && offset < segment.output_end;
                });
            if (found == generated.segments.end()) return fallback;
            return found->expansion_stack.empty()
                ? found->origin
                : found->expansion_stack.front().call_site;
        });
}

}  // namespace email_markup
