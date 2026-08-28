#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/images.hpp"

TEST_CASE("remote image embedding produces a data URI and preserves provenance")
{
    email_markup::GeneratedHtml generated;
    generated.append(
        "<img data-email-markup-embed=\"true\" src=\"https://cdn.test/a.png\" alt=\"A\">",
        {1, 10, 20}, {{"Image", {2, 30, 60}, {1, 10, 20}}});
    std::vector<email_markup::Diagnostic> diagnostics;

    email_markup::embed_remote_images(
        generated, diagnostics,
        [](const std::string_view url, const std::size_t maximum)
        {
            CHECK(url == "https://cdn.test/a.png");
            CHECK(maximum == 1024 * 1024);
            return email_markup::ImageResource{"image/png", "abc"};
        });

    CHECK(generated.html ==
          "<img src=\"data:image/png;base64,YWJj\" alt=\"A\">");
    CHECK(diagnostics.empty());
    REQUIRE(generated.segments.size() == 1);
    CHECK(generated.segments.front().expansion_stack.front().call_site.source == 2);
}

TEST_CASE("image embedding can be disabled explicitly")
{
    email_markup::GeneratedHtml generated;
    generated.append(
        "<img data-email-markup-embed='false' src='https://cdn.test/a.png'>", {1, 0, 20});
    std::vector<email_markup::Diagnostic> diagnostics;
    bool fetched = false;

    email_markup::embed_remote_images(
        generated, diagnostics,
        [&](std::string_view, std::size_t)
        {
            fetched = true;
            return email_markup::ImageResource{};
        });

    CHECK_FALSE(fetched);
    CHECK(generated.html == "<img src='https://cdn.test/a.png'>");
    CHECK(diagnostics.empty());
}

TEST_CASE("network-free compilation strips embedding metadata and keeps the URL")
{
    email_markup::GeneratedHtml generated;
    generated.append(
        "<img data-email-markup-embed=\"true\" src=\"https://cdn.test/a.png\">",
        {1, 0, 20});
    std::vector<email_markup::Diagnostic> diagnostics;

    email_markup::embed_remote_images(generated, diagnostics, {});

    CHECK(generated.html == "<img src=\"https://cdn.test/a.png\">");
    CHECK(diagnostics.empty());
}

TEST_CASE("large and failed image embedding produce warnings without failing output")
{
    email_markup::GeneratedHtml large;
    large.append(
        "<img data-email-markup-embed=\"true\" src=\"https://cdn.test/large.png\">",
        {1, 0, 20});
    std::vector<email_markup::Diagnostic> diagnostics;
    email_markup::embed_remote_images(
        large, diagnostics,
        [](std::string_view, std::size_t)
        { return email_markup::ImageResource{"image/png", "abc"}; },
        2);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "EM0816");

    email_markup::GeneratedHtml failed;
    failed.append(
        "<img data-email-markup-embed=\"true\" src=\"https://cdn.test/missing.png\">",
        {1, 0, 20});
    diagnostics.clear();
    email_markup::embed_remote_images(
        failed, diagnostics,
        [](std::string_view, std::size_t) -> email_markup::ImageResource
        { throw std::runtime_error("download failed"); });
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "EM0815");
    CHECK(failed.html == "<img src=\"https://cdn.test/missing.png\">");
}
