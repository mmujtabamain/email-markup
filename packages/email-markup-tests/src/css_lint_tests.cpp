#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/css.hpp"
#include "email-markup/core/lint.hpp"

TEST_CASE("class CSS is inlined while inline declarations retain precedence")
{
    const auto html = email_markup::inline_css(
        "<style>.card { color: red; padding: 8px; }</style>"
        "<div class=\"card\" style=\"color: blue;\">Hello</div>");

    CHECK(html.find("<style>") == std::string::npos);
    CHECK(html.find("color: blue;") != std::string::npos);
    CHECK(html.find("padding: 8px;") != std::string::npos);
}

TEST_CASE("media CSS survives inlining for the shell")
{
    const auto html = email_markup::inline_css(
        "<style>@media (max-width: 600px){.stack{display:block}}</style><div />");
    CHECK(html.find("@media") != std::string::npos);
}

TEST_CASE("class CSS follows stylesheet order and supports selector lists")
{
    const auto html = email_markup::inline_css(
        "<style>.second { color: blue; }.first, .shared { color: red; padding: 4px; }"
        ".second { padding: 8px; }</style>"
        "<div class=\"first second shared\">Hello</div>");

    CHECK(html.find("color: red;") != std::string::npos);
    CHECK(html.find("padding: 8px;") != std::string::npos);
}

TEST_CASE("deliverability lint distinguishes content and shell")
{
    const auto content = email_markup::lint_html("<p>Hello</p>", email_markup::LintRole::content, {});
    CHECK(content.empty());
    const auto shell = email_markup::lint_html("<html><body>Hello</body></html>",
                                               email_markup::LintRole::shell, {});
    CHECK_FALSE(shell.empty());
}

TEST_CASE("CSS inlining and deliverability lint preserve source provenance")
{
    email_markup::GeneratedHtml generated;
    generated.append("<style>.card { display: flex; }</style>", {1, 10, 30});
    generated.append("<img class=\"card\" src=\"http://example.test/a.png\">", {1, 80, 120});

    generated = email_markup::inline_css(std::move(generated));
    const auto findings = email_markup::lint_html(
        generated, email_markup::LintRole::content, {1, 0, 0});

    REQUIRE_FALSE(findings.empty());
    for (const auto &finding : findings)
    {
        CHECK(finding.range.source == 1);
        CHECK(finding.range.start != 0);
    }
}
