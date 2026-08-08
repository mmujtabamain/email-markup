#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "documents/document_store.hpp"
#include "protocol/json_rpc.hpp"
#include "text/positions.hpp"

TEST_CASE("LSP positions convert between UTF-8 bytes and UTF-16 units")
{
    const std::string source = "A 👋 B";

    const auto after_emoji = email_markup::lsp::text::offset_at(source, 0, 4);
    REQUIRE(source.substr(0, after_emoji) == "A 👋");
    REQUIRE(email_markup::lsp::text::position_at(source, after_emoji) ==
            email_markup::lsp::Json{{"line", 0}, {"character", 4}});
}

TEST_CASE("LSP document store owns incremental document state")
{
    email_markup::lsp::DocumentStore documents;
    documents.open("file:///message.em", "/message.em", "Hello 👋", 1);

    const email_markup::lsp::Json params{
        {"textDocument", {{"uri", "file:///message.em"}, {"version", 2}}},
        {"contentChanges",
         email_markup::lsp::Json::array(
             {{{"range",
                {{"start", {{"line", 0}, {"character", 6}}},
                 {"end", {{"line", 0}, {"character", 8}}}}},
               {"text", "World"}}})}};

    const auto *document = documents.apply_changes(params);
    REQUIRE(document != nullptr);
    REQUIRE(document->text == "Hello World");
    REQUIRE(documents.has_version("file:///message.em", 2));

    documents.close("file:///message.em");
    REQUIRE(documents.find("file:///message.em") == nullptr);
}

TEST_CASE("JSON-RPC connection owns message framing")
{
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
    std::istringstream input{"Content-Length: " + std::to_string(body.size()) +
                             "\r\n\r\n" + body};
    std::ostringstream output;
    email_markup::lsp::JsonRpcConnection connection{input, output};

    const auto message = connection.read();
    REQUIRE(message.has_value());
    REQUIRE(message->at("method") == "test");

    connection.respond(message->at("id"), {{"ok", true}});
    REQUIRE(output.str().find("Content-Length:") == 0);
    REQUIRE(output.str().find(R"("ok":true)") != std::string::npos);
}
