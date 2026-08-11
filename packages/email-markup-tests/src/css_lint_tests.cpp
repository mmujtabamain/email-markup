#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/css.hpp"
#include "email-markup/core/lint.hpp"

TEST_CASE("class CSS is inlined while inline declarations retain precedence")
{
    const auto html = email_markup::inline_css(
        "<style>.card { color: red; padding: 8px; }</style>"
        "<div class=\"card\" style=\"color: blue;\">Hello</div>");

    CHECK(html ==
          "<div class=\"card\" style=\"color: blue; padding: 8px;\">Hello</div>");
    CHECK(html.find("<style>") == std::string::npos);
    CHECK(html.find("color: blue;") != std::string::npos);
    CHECK(html.find("padding: 8px;") != std::string::npos);
}

TEST_CASE("media CSS survives inlining for the shell")
{
    const auto html = email_markup::inline_css(
        "<style>@media (max-width: 600px){.stack{display:block}}</style>"
        "<div class=\"stack\" />");
    CHECK(html.find("@media") != std::string::npos);
}

TEST_CASE("CSS cascade uses specificity before source order and supports tag selectors")
{
    const auto html = email_markup::inline_css(
        "<style>.notice { color: red; } div { color: blue; padding: 2px; }"
        "div { padding: 4px; }</style>"
        "<div class=\"notice\">Hello</div>");

    CHECK(html.find("color: red;") != std::string::npos);
    CHECK(html.find("color: blue;") == std::string::npos);
    CHECK(html.find("padding: 4px;") != std::string::npos);
}

TEST_CASE("CSS important declarations retain explicit inline precedence")
{
    const auto html = email_markup::inline_css(
        "<style>.card { color: red !important; padding: 8px !important; }</style>"
        "<div class=\"card\" style=\"color: blue; padding: 4px !important;\">Hello</div>");

    CHECK(html.find("color: blue;") != std::string::npos);
    CHECK(html.find("padding: 4px !important;") != std::string::npos);
}

TEST_CASE("media CSS is pruned to selectors used by literal output")
{
    const auto html = email_markup::inline_css(
        "<style>@media (max-width: 600px) {"
        ".used, table { display: block; } .unused { color: red; }"
        "}</style><div class=\"used\">Hello</div>");

    CHECK(html.find("@media (max-width: 600px){.used{display:block;}}") !=
          std::string::npos);
    CHECK(html.find(".unused") == std::string::npos);
    CHECK(html.find("table") == std::string::npos);
}

TEST_CASE("unsupported CSS selectors are retained and diagnosed")
{
    email_markup::GeneratedHtml generated;
    generated.append("<style>.card:hover { color: red; }</style>"
                     "<div class=\"card\">Hello</div>",
                     {3, 20, 90});
    std::vector<email_markup::Diagnostic> diagnostics;

    generated = email_markup::inline_css(std::move(generated), diagnostics);

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "EM0520");
    CHECK(diagnostics.front().range.source == 3);
    CHECK(generated.html.find(".card:hover{color:red;}") != std::string::npos);
}

TEST_CASE("CSS parser handles comments and delimiters inside values")
{
    const auto html = email_markup::inline_css(
        "<style>/* shared */ .card { background: url(\"a;b:c\");"
        "/* a brace in a comment: } */ color: red; }</style>"
        "<div class=\"card\">Hello</div>");

    CHECK(html.find("background: url(\"a;b:c\");") != std::string::npos);
    CHECK(html.find("color: red;") != std::string::npos);
}

TEST_CASE("unmatched CSS is removed without rewriting unrelated inline styles")
{
    const auto html = email_markup::inline_css(
        "<style>.card { color: red; }</style>"
        "<p style=\"font-weight:bold\">Hello</p>");

    CHECK(html == "<p style=\"font-weight:bold\">Hello</p>");
}

TEST_CASE("malformed CSS is retained and diagnosed")
{
    email_markup::GeneratedHtml generated;
    generated.append("<style>.card { color: red; /* unfinished</style>"
                     "<div class=\"card\">Hello</div>",
                     {4, 12, 82});
    std::vector<email_markup::Diagnostic> diagnostics;

    generated = email_markup::inline_css(std::move(generated), diagnostics);

    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "EM0521");
    CHECK(generated.html.find("<style>") != std::string::npos);
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
    std::size_t covered = 0;
    for (const auto &segment : generated.segments)
    {
        CHECK(segment.output_start == covered);
        CHECK(segment.output_end > segment.output_start);
        covered = segment.output_end;
    }
    CHECK(covered == generated.html.size());
}
