#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/context_schema.hpp"
#include "email-markup/core/include.hpp"
#include "email-markup/core/render.hpp"

namespace
{
    constexpr std::string_view django_engine = R"EMT(
@DefineBareTemplate
  @Params
    value: path
  @/Params
  @Template
    {{ @{value} }}
  @/Template
@/DefineBareTemplate
@DefineTemplate(name: "If")
  @Params
    condition: condition
  @/Params
  @Slots
    default: required
    else: optional
  @/Slots
  @Template
    {% if @{condition} %}@Slot(default);@If(slot.else){% else %}@Slot(else);@/If{% endif %}
  @/Template
@/DefineTemplate
@DefineTemplate(name: "For")
  @Params
    collection: path
    binding: name
    limit: int(1..100) = 20
  @/Params
  @Slots
    default: required
  @/Slots
  @Template
    {% for @{binding} in @{collection}|slice:":@{limit}" %}@Slot(default);{% endfor %}
  @/Template
@/DefineTemplate
)EMT";

    /// The shape of growth-console-emails' schemas/email-context.em.schema.json.
    email_markup::Json contract()
    {
        return email_markup::Json::parse(R"JSON({
          "format": "email-markup-context", "version": 1, "name": "email-context",
          "fields": {
            "business": {"type": "object", "required": true, "fields": {
              "name": {"type": "string", "required": true, "example": "Amina Coffee Roasters"},
              "category": {"type": "string", "example": "Coffee shop"},
              "rating": {"type": "number", "nullable": true, "minimum": 0, "maximum": 5, "example": 4.8},
              "review_count": {"type": "int", "nullable": true, "minimum": 0, "example": 182}
            }},
            "rep": {"type": "object", "nullable": true, "fields": {
              "id": {"type": "int", "required": true, "minimum": 1, "example": 17},
              "first_name": {"type": "string", "example": "Sam"}
            }},
            "items": {"type": "array", "required": true, "items": {"type": "string", "example": "x"}, "example": ["x"]},
            "message": {"type": "object", "required": true, "fields": {
              "unsubscribe_url": {"type": "url", "required": true, "example": "https://example.invalid/u"}
            }}
          }
        })JSON");
    }

    email_markup::CompilationResult compile(const std::string &source, bool subject = false,
                                            bool with_contract = true)
    {
        email_markup::MemoryFileResolver resolver{{
            {"/lib/engines/django.emt", std::string{django_engine}},
        }};
        email_markup::CompilationRequest request;
        request.entry_path = "/project/templates/closing-your-file/body.em";
        request.source = source;
        request.engine = "/lib/engines/django.emt";
        request.allowed_roots = {"/"};
        request.subject = subject;
        if (with_contract)
        {
            // As the CLI and the browser do: the contract's examples are the data.
            request.context_schema = contract();
            request.data = email_markup::context_schema_example(
                email_markup::parse_context_schema(request.context_schema));
        }
        return email_markup::compile(request, resolver);
    }

    std::vector<email_markup::Diagnostic> guard_errors(const email_markup::CompilationResult &result)
    {
        std::vector<email_markup::Diagnostic> out;
        std::copy_if(result.diagnostics.begin(), result.diagnostics.end(),
                     std::back_inserter(out),
                     [](const auto &diagnostic) { return diagnostic.code == "EM0910"; });
        return out;
    }

    std::string text_at(const std::string &source, const email_markup::Diagnostic &diagnostic)
    {
        return source.substr(diagnostic.range.start,
                             diagnostic.range.end - diagnostic.range.start);
    }
}

TEST_CASE("a field that can be missing, used outside an @If, is an error")
{
    const std::string source = "<p>Loved by @[business.category] fans</p>";
    const auto result = compile(source);
    const auto errors = guard_errors(result);
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().severity == email_markup::Severity::error);
    CHECK(errors.front().message == "business.category can be missing");
    CHECK(errors.front().json_path == "business.category");
    CHECK(text_at(source, errors.front()) == "@[business.category]");
    CHECK_FALSE(result.ok());
    // The template still lowered: the preview can show it while it is fixed.
    CHECK(result.emir.has_value());
}

TEST_CASE("the live Closing your file body fails on its category only")
{
    const std::string source =
        "<p>Hi @[business.name] team,</p>\n"
        "@If[business.rating]\n"
        "  @If[business.review_count]\n"
        "    <p>You have <b>@[business.rating]★ from @[business.review_count] reviews</b> — "
        "@[business.category] customers clearly rate you.</p>\n"
        "  @Slot(else)\n"
        "    <p>Your customers clearly rate you.</p>\n"
        "  @/Slot\n"
        "  @/If\n"
        "@Slot(else)\n"
        "  <p>Your customers clearly rate you.</p>\n"
        "@/Slot\n"
        "@/If\n"
        "<p><a href=\"@[message.unsubscribe_url]\">Unsubscribe</a></p>\n";
    const auto errors = guard_errors(compile(source));
    REQUIRE(errors.size() == 1);
    CHECK(text_at(source, errors.front()) == "@[business.category]");
}

TEST_CASE("guarding the field with @If and an else fixes it")
{
    const auto result = compile(
        "@If[business.category]\n"
        "  <p>Loved by @[business.category] fans</p>\n"
        "@Slot(else)\n"
        "  <p>Loved by fans</p>\n"
        "@/Slot\n"
        "@/If\n");
    CHECK(guard_errors(result).empty());
    CHECK(result.ok());
}

TEST_CASE("the else slot of a guard is not guarded")
{
    const auto errors = guard_errors(compile(
        "@If[business.category]\n  <p>yes</p>\n@Slot(else)\n  <p>@[business.category]</p>\n@/Slot\n@/If\n"));
    CHECK(errors.size() == 1);
}

TEST_CASE("and proves both sides; or and comparisons prove nothing")
{
    CHECK(guard_errors(compile(
              "@If[business.rating and business.category]<p>@[business.rating] @[business.category]</p>@/If"))
              .empty());
    CHECK(guard_errors(compile(
              "@If[business.rating or business.category]<p>@[business.category]</p>@/If"))
              .size() == 1);
    CHECK(guard_errors(compile("@If[business.rating > 4]<p>@[business.rating]</p>@/If")).size() == 1);
}

TEST_CASE("the else of a not proves the field present")
{
    CHECK(guard_errors(compile(
              "@If[not business.category]<p>none</p>@Slot(else)<p>@[business.category]</p>@/Slot@/If"))
              .empty());
}

TEST_CASE("a present parent proves the children it requires, not the others")
{
    const std::string source = "@If[rep]<p>@[rep.id] @[rep.first_name]</p>@/If";
    const auto errors = guard_errors(compile(source));
    REQUIRE(errors.size() == 1);
    CHECK(text_at(source, errors.front()) == "@[rep.first_name]");
    CHECK(guard_errors(compile("@If[rep.first_name]<p>@[rep.first_name]</p>@/If")).empty());
}

TEST_CASE("fields that are always sent need no guard")
{
    CHECK(guard_errors(compile("<p>@[business.name] @[message.unsubscribe_url]</p>")).empty());
}

TEST_CASE("loop values are not context fields")
{
    CHECK(guard_errors(compile("@For[collection: items, binding: rep]<p>@[rep.first_name]</p>@/For"))
              .empty());
}

TEST_CASE("subjects follow the same rule")
{
    CHECK(guard_errors(compile("Hello @[business.category]", true)).size() == 1);
    CHECK(guard_errors(
              compile("@If[business.category]Hi @[business.category]@Slot(else)Hi@/Slot@/If", true))
              .empty());
}

TEST_CASE("without a context contract nothing is checked")
{
    CHECK(guard_errors(compile("<p>@[business.category]</p>", false, false)).empty());
}
