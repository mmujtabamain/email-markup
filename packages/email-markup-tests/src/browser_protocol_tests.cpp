#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "email-markup/browser/protocol.hpp"

namespace
{
    using Json = nlohmann::json;

    Json request(const std::string &method, Json params = Json::object())
    {
        const Json envelope{{"protocol", email_markup::browser::protocol_name},
                            {"version", email_markup::browser::protocol_version},
                            {"id", 7},
                            {"method", method},
                            {"params", std::move(params)}};
        return Json::parse(email_markup::browser::handle_request(envelope.dump()));
    }

    Json workspace(const std::string &source)
    {
        return {{"entry_path", "/project/message.em"},
                {"source", source},
                {"files", Json::array({{{"path", "/library/card.em"},
                                         {"source",
                                          "@DefineComponent(name: \"Card\")\n"
                                          "  @Props\n"
                                          "    title: string(1..120)\n"
                                          "  @/Props\n"
                                          "  @Slots\n"
                                          "    default: optional\n"
                                          "  @/Slots\n"
                                          "  @Template\n"
                                          "    <section>@{title}@Slot(default);</section>\n"
                                          "  @/Template\n"
                                          "@/DefineComponent"}}})},
                {"include_directories", Json::array({"/library"})},
                {"imports", Json::array({"/library/card.em"})},
                {"data", {{"business", {{"name", "Acme"}}}}}};
    }
}

TEST_CASE("browser protocol exposes bounded non-authoritative capabilities")
{
    const auto response = request("capabilities");
    REQUIRE(response["ok"] == true);
    CHECK(response["result"]["authoritative"] == false);
    CHECK(response["result"]["executes_target"] == false);
    CHECK(response["result"]["network_access"] == false);
    CHECK(response["result"]["position_encoding"] == "utf-16");
    CHECK(response["result"]["limits"]["request_bytes"] == 1024 * 1024);
}

TEST_CASE("browser analysis produces deterministic final HTML assistance")
{
    const auto first = request("analyze", workspace("<p>Hello @{business.name}</p>"));
    const auto second = request("analyze", workspace("<p>Hello @{business.name}</p>"));
    REQUIRE(first["ok"] == true);
    REQUIRE(first["result"]["success"] == true);
    CHECK(first == second);
    CHECK(first["result"]["authoritative"] == false);
    CHECK(first["result"]["output_kind"] == "final-html");
    CHECK(first["result"]["preview"]["kind"] == "final-html");
    CHECK(first["result"]["preview"]["executes_target"] == false);
    CHECK(first["result"]["preview"]["html"].get<std::string>().find("Hello Acme") !=
          std::string::npos);
}

TEST_CASE("browser deferred preview remains unrendered target source")
{
    auto params = workspace("<p>Hello @[business.name]</p>");
    params["engine"] =
        {{"path", "/library/django.emt"},
         {"source",
          "@DefineBareTemplate\n"
          "  @Params\n"
          "    value: path\n"
          "  @/Params\n"
          "  @Template\n"
          "    {{ @{value} }}\n"
          "  @/Template\n"
          "@/DefineBareTemplate"}};
    const auto response = request("analyze", std::move(params));
    REQUIRE(response["ok"] == true);
    REQUIRE(response["result"]["success"] == true);
    CHECK(response["result"]["output_kind"] == "engine-template");
    CHECK(response["result"]["preview"]["kind"] == "target-source");
    CHECK(response["result"]["preview"]["rendered"] == false);
    CHECK(response["result"]["preview"]["executes_target"] == false);
    CHECK(response["result"]["preview"]["source"].get<std::string>().find(
              "{{ business.name }}") != std::string::npos);
    CHECK(response["result"]["emir"]["version"] == 1);
}

TEST_CASE("browser formatting and Monaco metadata share compiler declarations")
{
    const auto formatted = request(
        "format", {{"path", "/project/message.em"},
                   {"source", "@Card(title: \"Hello\") text @/Card"}});
    REQUIRE(formatted["ok"] == true);
    CHECK(formatted["result"]["changed"] == true);

    auto completion_params = workspace("@Ca");
    completion_params["position"] = {{"line", 0}, {"character", 3}};
    const auto completed = request("complete", std::move(completion_params));
    REQUIRE(completed["ok"] == true);
    const auto items = completed["result"]["items"];
    CHECK(std::any_of(items.begin(), items.end(), [](const auto &item)
                      { return item["label"] == "@Card"; }));

    auto prop_params = workspace("@Card(ti");
    prop_params["position"] = {{"line", 0}, {"character", 8}};
    const auto props = request("complete", std::move(prop_params));
    REQUIRE(props["ok"] == true);
    CHECK(std::any_of(props["result"]["items"].begin(),
                      props["result"]["items"].end(), [](const auto &item)
                      { return item["label"] == "title"; }));

    auto hover_params = workspace("@Card;");
    hover_params["position"] = {{"line", 0}, {"character", 3}};
    const auto hovered = request("hover", std::move(hover_params));
    REQUIRE(hovered["ok"] == true);
    CHECK(hovered["result"]["markdown"].get<std::string>().find("title") !=
          std::string::npos);

    auto prose_params = workspace("ordinary Card prose");
    prose_params["position"] = {{"line", 0}, {"character", 10}};
    const auto prose_hover = request("hover", std::move(prose_params));
    REQUIRE(prose_hover["ok"] == true);
    CHECK(prose_hover["result"].is_null());

    auto signature_params = workspace("@Card(title: ");
    signature_params["position"] = {{"line", 0}, {"character", 13}};
    const auto signed_response = request("signature", std::move(signature_params));
    REQUIRE(signed_response["ok"] == true);
    CHECK(signed_response["result"]["label"].get<std::string>().find(
              "title: string(1..120)") != std::string::npos);
}

TEST_CASE("browser protocol rejects unknown versions and oversized requests")
{
    auto invalid = Json{{"protocol", email_markup::browser::protocol_name},
                        {"version", 99},
                        {"id", "bad"},
                        {"method", "capabilities"},
                        {"params", Json::object()}};
    const auto response = Json::parse(
        email_markup::browser::handle_request(invalid.dump()));
    CHECK(response["ok"] == false);
    CHECK(response["error"]["code"] == "invalid_request");

    const std::string oversized(email_markup::browser::maximum_request_bytes + 1, 'x');
    const auto too_large = Json::parse(
        email_markup::browser::handle_request(oversized));
    CHECK(too_large["ok"] == false);
    CHECK(too_large["error"]["message"].get<std::string>().find("1 MiB") !=
          std::string::npos);
}
