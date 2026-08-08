#include <memory>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/provenance.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/core/source.hpp"

TEST_CASE("source manager owns stable UTF-8 source positions") {
    email_markup::SourceManager sources;
    const auto id = sources.add("message.em", "one\nβeta\n");
    const auto& source = sources.get(id);

    CHECK(source.position(0).line == 0);
    CHECK(source.position(4).line == 1);
    CHECK(source.position(4).column == 0);
    CHECK(email_markup::is_valid_utf8(source.text));
    CHECK_FALSE(email_markup::is_valid_utf8("\xff"));
}

TEST_CASE("generated HTML preserves source segments") {
    email_markup::GeneratedHtml generated;
    generated.append("hello", {1, 3, 8});
    generated.append(" world", {1, 3, 8});

    CHECK(generated.html == "hello world");
    REQUIRE(generated.segments.size() == 1);
    CHECK(generated.segments.front().output_end == 11);
}

TEST_CASE("generated HTML insertion preserves and shifts source segments") {
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

TEST_CASE("memory resolver normalizes paths and rejects duplicates") {
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
        ResolvedFile{"/same.em", "one"}, ResolvedFile{"/x/../same.em", "two"},
    }), std::invalid_argument);
    CHECK_THROWS_AS(MemoryFileResolver({ResolvedFile{"relative.em", "source"}}),
                    std::invalid_argument);
}

TEST_CASE("memory resolver compiles nested includes imports and a shell") {
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
