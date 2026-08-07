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

void finding(std::vector<Diagnostic>& output, std::string code, Severity severity,
             std::string message, const SourceRange range) {
    output.push_back({std::move(code), severity, std::move(message), range});
}

}  // namespace

std::vector<Diagnostic> lint_html(const std::string_view html, const LintRole role,
                                  const SourceRange fallback,
                                  const std::size_t gmail_limit) {
    std::vector<Diagnostic> output;
    const auto lowered = lower(std::string{html});
    for (const auto& tag : {"script", "iframe", "object", "embed", "form"}) {
        if (lowered.find("<" + std::string{tag}) != std::string::npos) {
            finding(output, "EM0801", Severity::error,
                    "Forbidden email HTML element <" + std::string{tag} + ">.", fallback);
        }
    }
    static const std::regex stylesheet{
        R"(<link\b[^>]*rel\s*=\s*["']?stylesheet)", std::regex::icase};
    if (std::regex_search(lowered, stylesheet)) {
        finding(output, "EM0802", Severity::error,
                "External stylesheets are not deliverable email HTML.", fallback);
    }
    if (role == LintRole::content && lowered.find("<style") != std::string::npos) {
        finding(output, "EM0803", Severity::error,
                "Content cannot emit a <style> block; media styles belong to the shell.",
                fallback);
    }
    static const std::regex image{R"(<img\b([^>]*)>)", std::regex::icase};
    for (std::sregex_iterator it{lowered.begin(), lowered.end(), image}, end; it != end; ++it) {
        const auto attributes = (*it)[1].str();
        if (attributes.find("alt=") == std::string::npos) {
            finding(output, "EM0810", Severity::warning,
                    "Image has no alt attribute.", fallback);
        }
        if (attributes.find("src=\"http://") != std::string::npos ||
            attributes.find("src='http://") != std::string::npos) {
            finding(output, "EM0811", Severity::warning,
                    "Image uses insecure HTTP.", fallback);
        }
    }
    for (const auto& unsupported : {"display: flex", "display:flex", "display: grid",
                                    "display:grid", "position: fixed", "position:fixed"}) {
        if (lowered.find(unsupported) != std::string::npos) {
            finding(output, "EM0812", Severity::warning,
                    "CSS feature has poor email-client support: " +
                    std::string{unsupported} + ".", fallback);
        }
    }
    if (html.size() > gmail_limit) {
        finding(output, "EM0813", Severity::warning,
                "Generated HTML exceeds Gmail's 102,400-byte clipping threshold.", fallback);
    }
    if (role == LintRole::shell && lowered.find("unsubscribe") == std::string::npos) {
        finding(output, "EM0814", Severity::error,
                "Final email requires a visible unsubscribe target.", fallback);
    }

    static const std::regex tag_pattern{R"(<\s*(/?)\s*([a-zA-Z][a-zA-Z0-9:-]*)[^>]*>)"};
    const std::unordered_set<std::string> void_elements{
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
        "param", "source", "track", "wbr"};
    std::vector<std::string> stack;
    for (std::sregex_iterator it{lowered.begin(), lowered.end(), tag_pattern}, end;
         it != end; ++it) {
        const bool closing = (*it)[1].matched && !(*it)[1].str().empty();
        const auto name = (*it)[2].str();
        if (void_elements.contains(name) || it->str().ends_with("/>")) continue;
        if (!closing) stack.push_back(name);
        else if (stack.empty() || stack.back() != name) {
            finding(output, "EM0820", Severity::error,
                    "Unbalanced HTML close tag </" + name + ">.", fallback);
        } else stack.pop_back();
    }
    if (!stack.empty()) {
        finding(output, "EM0821", Severity::error,
                "Unclosed HTML element <" + stack.back() + ">.", fallback);
    }
    return output;
}

}  // namespace email_markup
