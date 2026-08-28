#include <random>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/lexer.hpp"
#include "email-markup/core/parser.hpp"
#include "email-markup/core/render.hpp"

namespace
{

    class EmptyResolver final : public email_markup::FileResolver
    {
    public:
        std::optional<email_markup::ResolvedFile> resolve(
            const std::filesystem::path &, std::string_view,
            const std::vector<std::filesystem::path> &,
            const std::vector<std::filesystem::path> &,
            std::vector<std::filesystem::path> &) override
        {
            return std::nullopt;
        }
    };

} // namespace

TEST_CASE("deferred syntax is gated by engine selection")
{
    CHECK(email_markup::lex(0, "@[legacy]").diagnostics.empty());
    CHECK(email_markup::parse(0, "@Engine(\"django.emt\");").diagnostics.empty());
    CHECK(email_markup::parse(0, "@Name[value];").diagnostics.empty());

    EmptyResolver resolver;
    email_markup::CompilationRequest request;
    request.entry_path = "message.emt";
    request.source = "plain";
    CHECK_FALSE(email_markup::compile(request, resolver).ok());
}

TEST_CASE("lexer and parser tolerate arbitrary malformed bytes")
{
    std::mt19937 generator{0x454c4c};
    std::uniform_int_distribution<int> length_distribution{0, 256};
    std::uniform_int_distribution<int> byte_distribution{0, 255};
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        std::string input(static_cast<std::size_t>(length_distribution(generator)), '\0');
        for (char &byte : input)
            byte = static_cast<char>(byte_distribution(generator));
        CHECK_NOTHROW(email_markup::lex(0, input, 16));
        CHECK_NOTHROW(email_markup::parse(0, input, 16));
    }
}

TEST_CASE("lexer handles representative Unicode")
{
    const auto parsed = email_markup::parse(0,
                                            "@Paragraph مرحبا 世界 👋 café Привет @{business.name} @/Paragraph");
    CHECK(parsed.diagnostics.empty());
}
