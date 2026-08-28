#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/ast.hpp"
#include "email-markup/core/lexer.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/types.hpp"

TEST_CASE("lexer preserves ordinary at signs and removes comments")
{
  const auto result = email_markup::lex(0,
                                        "hello@example.org @@Team @// hidden\n@* block *@ @{business.name}");

  REQUIRE(result.diagnostics.empty());
  CHECK(result.tokens.front().text.find("hello@example.org @Team") != std::string::npos);
  CHECK(result.tokens[result.tokens.size() - 2].kind == email_markup::TokenKind::expression);
}

TEST_CASE("parser builds compiler control flow")
{
  const auto result = email_markup::parse(0,
                                          "@If(show)yes@Else no@/If @For(item in items)@{item}@/For");

  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.document.nodes.size() == 2);
  CHECK(std::holds_alternative<email_markup::IfNode>(result.document.nodes[0]->value));
  CHECK(std::holds_alternative<email_markup::ForNode>(result.document.nodes[1]->value));
}

TEST_CASE("parser recovers and collects diagnostics")
{
  const auto result = email_markup::parse(0, "@If(true) open @/Wrong @Unknown;");
  CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("component declarations own props slots and templates")
{
  const auto result = email_markup::parse(0, R"EM(
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
)EM");

  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.document.components.contains("Card"));
  CHECK(result.document.components.at("Card").props.size() == 1);
  CHECK(result.document.components.at("Card").slots.size() == 1);
}

TEST_CASE("rich declarations preserve typed constraints and precise ranges")
{
  const std::string source =
      "  size?: int(9007199254740993..9007199254740995) >= 9007199254740994 = "
      "9007199254740994";
  std::vector<email_markup::Diagnostic> diagnostics;

  const auto declarations = email_markup::parse_declarations(
      source, {7, 100, 100 + source.size()},
      email_markup::DeclarationContext::component_prop, diagnostics);

  REQUIRE(diagnostics.empty());
  REQUIRE(declarations.size() == 1);
  const auto &declaration = declarations.front();
  CHECK(declaration.name == "size");
  CHECK(declaration.value_type == email_markup::DeclarationType::integer);
  CHECK(declaration.optional);
  CHECK(declaration.name_range == email_markup::SourceRange{7, 102, 106});
  CHECK(declaration.type_range == email_markup::SourceRange{7, 109, 112});
  REQUIRE(declaration.range_constraint.has_value());
  CHECK(declaration.range_constraint->minimum.spelling == "9007199254740993");
  CHECK(declaration.range_constraint->maximum.spelling == "9007199254740995");
  CHECK(source.substr(declaration.range_constraint->minimum.range.start - 100,
                      declaration.range_constraint->minimum.range.end -
                          declaration.range_constraint->minimum.range.start) ==
        "9007199254740993");
  CHECK(source.substr(declaration.range_constraint->maximum.range.start - 100,
                      declaration.range_constraint->maximum.range.end -
                          declaration.range_constraint->maximum.range.start) ==
        "9007199254740995");
  REQUIRE(declaration.comparison_constraint.has_value());
  CHECK(declaration.comparison_constraint->bound.spelling == "9007199254740994");
  CHECK(source.substr(declaration.comparison_constraint->bound.range.start - 100,
                      declaration.comparison_constraint->bound.range.end -
                          declaration.comparison_constraint->bound.range.start) ==
        "9007199254740994");
  CHECK(source.substr(declaration.default_range.start - 100,
                      declaration.default_range.end - declaration.default_range.start) ==
        "9007199254740994");
}

TEST_CASE("shared declarations enforce type and constraint context rules")
{
  std::vector<email_markup::Diagnostic> diagnostics;
  const auto ordinary = email_markup::parse_declarations(
      "raw_value: raw\nroute: path\nshow: condition\nratio: int(0.0..1.0)\n"
      "label: string >= 2\ncount: int(1..2) > 2\n",
      {0, 0, 0}, email_markup::DeclarationContext::component_prop, diagnostics);

  CHECK(ordinary.empty());
  CHECK(std::count_if(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                      { return diagnostic.code == "EM0406"; }) == 3);
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return diagnostic.code == "EM0408"; }));
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return diagnostic.code == "EM0407"; }));
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return diagnostic.code == "EM0409"; }));

  diagnostics.clear();
  const auto deferred = email_markup::parse_declarations(
      "body\nroute: path\nshow: condition\nalias?: name\n",
      {0, 0, 0}, email_markup::DeclarationContext::deferred_parameter, diagnostics);
  REQUIRE(diagnostics.empty());
  REQUIRE(deferred.size() == 4);
  CHECK(deferred[0].value_type == email_markup::DeclarationType::raw);
  CHECK_FALSE(deferred[0].has_explicit_type);
  CHECK(deferred[1].value_type == email_markup::DeclarationType::path);
  CHECK(deferred[2].value_type == email_markup::DeclarationType::condition);
  CHECK(deferred[3].value_type == email_markup::DeclarationType::name);
}

TEST_CASE("literal defaults are validated against rich declarations")
{
  std::vector<email_markup::Diagnostic> diagnostics;
  const auto declarations = email_markup::parse_prop_declarations(
      "size: int(1..10) = 11\nalias: name = \"not a name\"\n",
      {0, 0, 0}, diagnostics);

  REQUIRE(declarations.size() == 2);
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return diagnostic.code == "EM0423"; }));
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return diagnostic.code == "EM0426"; }));
  CHECK(std::all_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic)
                    { return !diagnostic.range.empty(); }));
}
