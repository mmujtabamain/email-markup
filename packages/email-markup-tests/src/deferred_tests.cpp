#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/emir.hpp"
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

    email_markup::CompilationResult compile_deferred(const std::string &source)
    {
        email_markup::MemoryFileResolver resolver{{
            {"/lib/engines/django.emt", std::string{django_engine}},
        }};
        email_markup::CompilationRequest request;
        request.entry_path = "/project/message.em";
        request.source = source;
        request.engine = "/lib/engines/django.emt";
        request.allowed_roots = {"/"};
        return email_markup::compile(request, resolver);
    }
}

TEST_CASE("deferred Django values compile through public EMIR v1")
{
    const auto result = compile_deferred("<p>Hello @[business.name]</p>");
    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    REQUIRE(result.emir.has_value());
    CHECK(result.output_kind == email_markup::OutputKind::engine_template);
    CHECK(result.target->name == "django");
    CHECK(result.generated.html == "<p>Hello {{ business.name }}</p>");
    CHECK_FALSE(result.emir->value["source_map"]["mappings"].empty());
    CHECK(result.emir->value["source_map"]["sources"][0]["path"] ==
          "message.em");
    const auto &mappings = result.emir->value["source_map"]["mappings"];
    CHECK(std::is_sorted(mappings.begin(), mappings.end(), [](const auto &left,
                                                              const auto &right)
                         {
                             return left["output_start"].template get<std::size_t>() <
                                    right["output_start"].template get<std::size_t>();
                         }));

    const auto serialized = email_markup::canonical_emir_json(*result.emir);
    CHECK(serialized.find("/project/") == std::string::npos);
    const auto reparsed = email_markup::parse_emir(serialized);
    REQUIRE(reparsed.ok());
    CHECK(email_markup::canonical_emir_json(*reparsed.artifact) == serialized);
    const auto emitted = email_markup::emit_emir(*reparsed.artifact, "django");
    REQUIRE(emitted.ok());
    CHECK(emitted.output == result.generated.html);
}

TEST_CASE("deferred conditions and bounded loops stay typed in EMIR")
{
    const auto result = compile_deferred(
        "@If[business.active and business.score >= 10]"
        "<p>Active</p>@Slot(else)<p>Quiet</p>@/Slot@/If"
        "@For[collection: business.items, binding: item, limit: 5]"
        "<span>@[item.name]</span>@/For");
    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.find("{% if") != std::string::npos);
    CHECK(result.generated.html.find("{% else %}") != std::string::npos);
    CHECK(result.generated.html.find("|slice:\":5\"") != std::string::npos);
    const auto inspected = email_markup::inspect_emir(*result.emir);
    CHECK(inspected["node_counts"]["runtime_if"] == 1);
    CHECK(inspected["node_counts"]["runtime_for"] == 1);
    CHECK(inspected["node_counts"]["runtime_value"] == 1);
}

TEST_CASE("entry Engine selection resolves the packaged library identity")
{
    email_markup::MemoryFileResolver resolver{{
        {"/lib/engines/django.emt", std::string{django_engine}},
    }};
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source =
        "@Engine(\"${EMAIL_MARKUP_LIB}/engines/django.emt\");"
        "<a href=\"@[business.website]\">@[business.name]</a>";
    request.include_directories = {"/lib"};
    request.allowed_roots = {"/"};
    const auto result = email_markup::compile(request, resolver);
    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    const auto &children = result.emir->value["document"]["children"];
    REQUIRE(children.size() == 5);
    CHECK(children[1]["escape"] == "url");
    CHECK(children[3]["escape"] == "html_text");
    CHECK(result.emir->value["requirements"]["recipient"]["business.website"]
              ["type"] == "url");
    CHECK(result.emir->value["target"]["engine"] ==
          "${EMAIL_MARKUP_LIB}/engines/django.emt");
}

TEST_CASE("deferred syntax remains an error without an engine")
{
    email_markup::MemoryFileResolver resolver{{}};
    email_markup::CompilationRequest request;
    request.entry_path = "/message.em";
    request.source = "@[business.name]";
    const auto result = email_markup::compile(request, resolver);
    CHECK_FALSE(result.ok());
}

TEST_CASE("unsupported EMIR versions fail explicitly")
{
    const auto parsed = email_markup::parse_emir(
        R"({"format":"email-markup-ir","version":99,"output_kind":"engine-template"})");
    REQUIRE_FALSE(parsed.ok());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == "EMIR0003");
}

TEST_CASE("check-ir rejects an EMIR source map without a source table")
{
    const auto result = compile_deferred("@[business.name]");
    REQUIRE(result.emir.has_value());
    auto broken = result.emir->value;
    broken["source_map"].erase("sources");
    const auto parsed = email_markup::parse_emir(broken.dump());
    REQUIRE_FALSE(parsed.ok());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == "EMIR0006");
}

TEST_CASE("deferred parameter annotations enforce exact bounds")
{
    const auto result = compile_deferred(
        "@For[collection: business.items, binding: item, limit: 101]"
        "@[item.name]@/For");
    REQUIRE_FALSE(result.ok());
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const auto &diagnostic)
                      { return diagnostic.code == "EM0431"; }));
}
