#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "ell/core/provenance.hpp"
#include "ell/core/source.hpp"

TEST_CASE("source manager owns stable UTF-8 source positions") {
    ell::SourceManager sources;
    const auto id = sources.add("message.ell", "one\nβeta\n");
    const auto& source = sources.get(id);

    CHECK(source.position(0).line == 0);
    CHECK(source.position(4).line == 1);
    CHECK(source.position(4).column == 0);
    CHECK(ell::is_valid_utf8(source.text));
    CHECK_FALSE(ell::is_valid_utf8("\xff"));
}

TEST_CASE("generated HTML preserves source segments") {
    ell::GeneratedHtml generated;
    generated.append("hello", {1, 3, 8});
    generated.append(" world", {1, 3, 8});

    CHECK(generated.html == "hello world");
    REQUIRE(generated.segments.size() == 1);
    CHECK(generated.segments.front().output_end == 11);
}
