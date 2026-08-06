#include <catch2/catch_test_macros.hpp>

#include "ell/core/ast.hpp"
#include "ell/core/lexer.hpp"
#include "ell/core/parser.hpp"

TEST_CASE("lexer preserves ordinary at signs and removes comments") {
    const auto result = ell::lex(0,
        "hello@example.org @@Team @// hidden\n@* block *@ @{business.name}");

    REQUIRE(result.diagnostics.empty());
    CHECK(result.tokens.front().text.find("hello@example.org @Team") != std::string::npos);
    CHECK(result.tokens[result.tokens.size() - 2].kind == ell::TokenKind::expression);
}

TEST_CASE("parser builds compiler control flow") {
    const auto result = ell::parse(0,
        "@If(show)yes@Else no@/If @For(item in items)@{item}@/For");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.document.nodes.size() == 2);
    CHECK(std::holds_alternative<ell::IfNode>(result.document.nodes[0]->value));
    CHECK(std::holds_alternative<ell::ForNode>(result.document.nodes[1]->value));
}

TEST_CASE("parser recovers and collects diagnostics") {
    const auto result = ell::parse(0, "@If(true) open @/Wrong @Unknown;");
    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("component declarations own props slots and templates") {
    const auto result = ell::parse(0, R"ELL(
@DefineComponent(name: "Card")
  @Props
    title: string
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <section><h1>@{title}</h1>@Slot(default);</section>
  @/Template
@/DefineComponent
)ELL");

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.document.components.contains("Card"));
    CHECK(result.document.components.at("Card").props.size() == 1);
    CHECK(result.document.components.at("Card").slots.size() == 1);
}
