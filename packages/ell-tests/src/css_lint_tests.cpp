#include <catch2/catch_test_macros.hpp>

#include "ell/core/css.hpp"
#include "ell/core/lint.hpp"

TEST_CASE("class CSS is inlined while inline declarations retain precedence") {
    const auto html = ell::inline_css(
        "<style>.card { color: red; padding: 8px; }</style>"
        "<div class=\"card\" style=\"color: blue;\">Hello</div>");

    CHECK(html.find("<style>") == std::string::npos);
    CHECK(html.find("color: blue;") != std::string::npos);
    CHECK(html.find("padding: 8px;") != std::string::npos);
}

TEST_CASE("media CSS survives inlining for the shell") {
    const auto html = ell::inline_css(
        "<style>@media (max-width: 600px){.stack{display:block}}</style><div />");
    CHECK(html.find("@media") != std::string::npos);
}

TEST_CASE("deliverability lint distinguishes content and shell") {
    const auto content = ell::lint_html("<p>Hello</p>", ell::LintRole::content, {});
    CHECK(content.empty());
    const auto shell = ell::lint_html("<html><body>Hello</body></html>",
                                      ell::LintRole::shell, {});
    CHECK_FALSE(shell.empty());
}
