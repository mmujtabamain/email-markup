#include <memory>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/format.hpp"
#include "email-markup/core/provenance.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/core/source.hpp"

TEST_CASE("source manager owns stable UTF-8 source positions")
{
    email_markup::SourceManager sources;
    const auto id = sources.add("message.em", "one\nβeta\n");
    const auto &source = sources.get(id);

    CHECK(source.position(0).line == 0);
    CHECK(source.position(4).line == 1);
    CHECK(source.position(4).column == 0);
    CHECK(email_markup::is_valid_utf8(source.text));
    CHECK_FALSE(email_markup::is_valid_utf8("\xff"));
}

TEST_CASE("formatter normalizes CRLF and trailing whitespace")
{
    CHECK(email_markup::format_source("<p>trailing   \r\n") == "<p>trailing\n");
}

TEST_CASE("formatter inserts one blank line after includes")
{
    const auto source =
        "<p>Before</p>\n"
        "@Include(\"theme.em\");\n"
        "@Include(\"components.em\");\n"
        "<p>Body</p>\n";
    const auto expected =
        "<p>Before</p>\n\n"
        "@Include(\"theme.em\");\n\n"
        "@Include(\"components.em\");\n\n"
        "<p>Body</p>\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("formatter puts component tags on separate lines")
{
    const auto source =
        "@Template<blockquote>@Slot(default);@If(attribution)<div>"
        "&mdash; @{attribution}</div>@/If</blockquote>@/Template";
    const auto expected =
        "@Template\n"
        "  <blockquote>\n"
        "    @Slot(default);\n"
        "    @If(attribution)\n"
        "      <div>&mdash; @{attribution}</div>\n"
        "    @/If\n"
        "  </blockquote>\n"
        "@/Template\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("formatter separates definitions and protects raw declaration bodies")
{
    const auto source =
        "<p>Before</p>@DefineStyle(name: \"card\")\n"
        "color: red; content: \"<keep>\";\n"
        "@/DefineStyle@DefineToken(name: \"accent\", value: \"#fff\");"
        "@DefineComponent(name: \"Card\")@Props\n"
        "title: string\n"
        "@/Props@Slots\n"
        "default: optional\n"
        "@/Slots@Template<div>@{title}</div>@/Template@/DefineComponent"
        "@Media(\"(max-width: 600px)\")\n"
        ".card::before { content: \"<keep>\"; }\n"
        "@/Media"
        "<p>After</p>";
    const auto expected =
        "<p>Before</p>\n\n"
        "@DefineStyle(name: \"card\")\n"
        "  color: red; content: \"<keep>\";\n"
        "@/DefineStyle\n\n"
        "@DefineToken(name: \"accent\", value: \"#fff\");\n\n"
        "@DefineComponent(name: \"Card\")\n"
        "  @Props\n"
        "    title: string\n"
        "  @/Props\n"
        "  @Slots\n"
        "    default: optional\n"
        "  @/Slots\n"
        "  @Template\n"
        "    <div>@{title}</div>\n"
        "  @/Template\n"
        "@/DefineComponent\n\n"
        "@Media(\"(max-width: 600px)\")\n"
        "  .card::before { content: \"<keep>\"; }\n"
        "@/Media\n\n"
        "<p>After</p>\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("formatter keeps multiline component heads on one directive line")
{
    const auto source =
        "@Image(\n"
        "  src: \"https://cdn.test/image.png\",\n"
        "  alt: \"Preview\"\n"
        ");";
    const auto expected =
        "@Image(src: \"https://cdn.test/image.png\", alt: \"Preview\");\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("formatter compacts simple components and expands long HTML cells")
{
    const auto source =
        "@Heading\n"
        "  Weekly summary\n"
        "@/Heading\n"
        "<table><tr>\n"
        "<td class=\"summary-column\" style=\"width:50%;padding:16px;background:#ecfdf5;vertical-align:top;\"><strong>@{summary.completed}</strong><br>Completed</td>\n"
        "</tr></table>\n";
    const auto expected =
        "@Heading Weekly summary @/Heading\n"
        "<table>\n"
        "  <tr>\n"
        "    <td class=\"summary-column\" style=\"width:50%;padding:16px;background:#ecfdf5;vertical-align:top;\">\n"
        "      <strong>@{summary.completed}</strong>\n"
        "      <br>\n"
        "      Completed\n"
        "    </td>\n"
        "  </tr>\n"
        "</table>\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("formatter applies the same structural layout to every HTML tag")
{
    const auto source =
        "<custom-shell><custom-value>Ready</custom-value><br>Now</custom-shell>";
    const auto expected =
        "<custom-shell>\n"
        "  <custom-value>Ready</custom-value>\n"
        "  <br>\n"
        "  Now\n"
        "</custom-shell>\n";

    CHECK(email_markup::format_source(source) == expected);
    CHECK(email_markup::format_source(expected) == expected);
}

TEST_CASE("generated HTML preserves source segments")
{
    email_markup::GeneratedHtml generated;
    generated.append("hello", {1, 3, 8});
    generated.append(" world", {1, 3, 8});

    CHECK(generated.html == "hello world");
    REQUIRE(generated.segments.size() == 1);
    CHECK(generated.segments.front().output_end == 11);
}

TEST_CASE("generated HTML insertion preserves and shifts source segments")
{
    email_markup::GeneratedHtml generated;
    generated.append("beforeafter", {1, 4, 10});
    generated.insert(6, " middle ", {2, 20, 24});

    CHECK(generated.html == "before middle after");
    REQUIRE(generated.segments.size() == 3);
    CHECK(generated.segments[0].origin.source == 1);
    CHECK(generated.segments[1].origin.source == 2);
    CHECK(generated.segments[2].origin.source == 1);
    CHECK(generated.segments[2].output_start == 14);
}

TEST_CASE("generated HTML replacement preserves and shifts source segments")
{
    email_markup::GeneratedHtml generated;
    generated.append("before URL after", {1, 4, 10});
    generated.replace(7, 3, "data:image/png;base64,AA==", {2, 20, 24});

    CHECK(generated.html == "before data:image/png;base64,AA== after");
    REQUIRE(generated.segments.size() == 3);
    CHECK(generated.segments[0].origin.source == 1);
    CHECK(generated.segments[1].origin.source == 2);
    CHECK(generated.segments[2].origin.source == 1);
    CHECK(generated.segments[2].output_start == 33);
}

TEST_CASE("memory resolver normalizes paths and rejects duplicates")
{
    using email_markup::MemoryFileResolver;
    using email_markup::ResolvedFile;

    MemoryFileResolver resolver{{
        ResolvedFile{"/library/./card.em", "@Define Card() <p>card</p> @/Define"},
    }};
    std::vector<std::filesystem::path> attempted;
    const auto resolved = resolver.resolve(
        "/mail/message.em", "../library/card.em", {}, {"/"}, attempted);
    REQUIRE(resolved.has_value());
    CHECK(resolved->canonical_path == "/library/card.em");

    CHECK_THROWS_AS(MemoryFileResolver({
                        ResolvedFile{"/same.em", "one"},
                        ResolvedFile{"/x/../same.em", "two"},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(MemoryFileResolver({ResolvedFile{"relative.em", "source"}}),
                    std::invalid_argument);

    CHECK(email_markup::portable_path_string("\\project\\shell.em") ==
          "/project/shell.em");
    MemoryFileResolver windows_style{{
        ResolvedFile{"\\library\\portable.em", "portable"},
    }};
    attempted.clear();
    const auto portable = windows_style.resolve(
        "\\mail\\message.em", "\\library\\portable.em", {}, {"\\"}, attempted);
    REQUIRE(portable.has_value());
    CHECK(email_markup::portable_path_string(portable->canonical_path) ==
          "/library/portable.em");
}

TEST_CASE("memory resolver compiles nested includes imports and a shell")
{
    using email_markup::MemoryFileResolver;
    using email_markup::ResolvedFile;

    MemoryFileResolver resolver{{
        ResolvedFile{"/library/theme.em", "@DefineComponent(name: \"Badge\") @Template <strong>ok</strong> @/Template @/DefineComponent"},
        ResolvedFile{"/components/index.em", "@Include(\"nested/item.em\");"},
        ResolvedFile{"/components/nested/item.em", "@DefineComponent(name: \"Item\") @Template <span>@Badge @/Badge</span> @/Template @/DefineComponent"},
        ResolvedFile{"/shell.em", "<!doctype html><html><body>@Slot(default); <a href=\"@{unsubscribe_url}\">Unsubscribe</a></body></html>"},
    }};
    email_markup::CompilationRequest request;
    request.entry_path = "/mail/message.em";
    request.source = "@Include(\"index.em\"); @Item @/Item";
    request.include_directories = {"/components"};
    request.allowed_roots = {"/"};
    request.imports = {"/library/theme.em"};
    request.shell = "/shell.em";
    request.data = {{"unsubscribe_url", "https://example.test/unsubscribe"}};

    const auto result = email_markup::compile(request, resolver);
    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.find("<strong>ok</strong>") != std::string::npos);
    CHECK(result.generated.html.find("<!doctype html>") != std::string::npos);
    CHECK(result.dependencies.size() == 5);
}
