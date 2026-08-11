#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/render.hpp"

namespace
{

    class MemoryResolver final : public email_markup::FileResolver
    {
    public:
        std::unordered_map<std::string, std::string> files;

        std::optional<email_markup::ResolvedFile> resolve(
            const std::filesystem::path &including_file, const std::string_view requested,
            const std::vector<std::filesystem::path> &,
            const std::vector<std::filesystem::path> &,
            std::vector<std::filesystem::path> &attempted) override
        {
            auto path = requested.starts_with('/')
                            ? std::filesystem::path{requested}
                            : including_file.parent_path() / requested;
            path = path.lexically_normal();
            attempted.push_back(path);
            const auto found = files.find(email_markup::portable_path_string(path));
            if (found == files.end())
                return std::nullopt;
            return email_markup::ResolvedFile{path, found->second};
        }
    };

} // namespace

TEST_CASE("compiler validates dynamic defaults and exact rich prop constraints")
{
    MemoryResolver resolver;
    resolver.files["/project/types.em"] = R"EM(
@DefineComponent(name: "Typed")
  @Props
    exact: int(9007199254740993..9007199254740995) >= 9007199254740994
    ratio: decimal(0.0..1.0)
    alias: name
    label: string(1..1)
    fallback: int(1..5) = configured_fallback
  @/Props
  @Template>@{exact}:@{ratio}:@{alias}:@{label}:@{fallback}@/Template
@/DefineComponent
)EM";
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = R"EM(@Include("types.em");
@Typed(exact: 9007199254740994, ratio: 0.5, alias: "valid_name", label: "β");
)EM";
    request.data = {{"configured_fallback", 4}};

    const auto valid = email_markup::compile(request, resolver);
    INFO((valid.diagnostics.empty() ? "" : valid.diagnostics.front().message));
    REQUIRE(valid.ok());
    CHECK(valid.generated.html.find("9007199254740994:0.5:valid_name:β:4") !=
          std::string::npos);

    request.data["configured_fallback"] = 6;
    const auto invalid_default = email_markup::compile(request, resolver);
    CHECK_FALSE(invalid_default.ok());
    CHECK(std::any_of(invalid_default.diagnostics.begin(), invalid_default.diagnostics.end(),
                      [](const auto &diagnostic)
                      { return diagnostic.code == "EM0423"; }));

    request.data["configured_fallback"] = 4;
    request.source = R"EM(@Include("types.em");
@Typed(exact: 9007199254740992, ratio: 1, alias: "not valid", label: "ββ");
)EM";
    const auto invalid_values = email_markup::compile(request, resolver);
    CHECK_FALSE(invalid_values.ok());
    CHECK(std::any_of(invalid_values.diagnostics.begin(), invalid_values.diagnostics.end(),
                      [](const auto &diagnostic)
                      { return diagnostic.code == "EM0420"; }));
    CHECK(std::any_of(invalid_values.diagnostics.begin(), invalid_values.diagnostics.end(),
                      [](const auto &diagnostic)
                      { return diagnostic.code == "EM0423"; }));
}
