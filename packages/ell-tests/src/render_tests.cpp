#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "ell/core/render.hpp"

namespace {

class MemoryResolver final : public ell::FileResolver {
public:
    std::unordered_map<std::string, std::string> files;

    std::optional<ell::ResolvedFile> resolve(
        const std::filesystem::path& including_file, const std::string_view requested,
        const std::vector<std::filesystem::path>&,
        const std::vector<std::filesystem::path>&,
        std::vector<std::filesystem::path>& attempted) override {
        auto path = requested.starts_with('/')
                        ? std::filesystem::path{requested}
                        : including_file.parent_path() / requested;
        path = path.lexically_normal();
        attempted.push_back(path);
        const auto found = files.find(path.string());
        if (found == files.end()) return std::nullopt;
        return ell::ResolvedFile{path, found->second};
    }
};

constexpr std::string_view library = R"ELL(
@DefineToken(name: "accent", value: "#7c5cff");
@DefineStyle(name: "card")
  color: @{token.accent}; padding: 8px;
@/DefineStyle
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
)ELL";

}  // namespace

TEST_CASE("compiler renders data loops components styles and provenance") {
    MemoryResolver resolver;
    resolver.files["/project/lib.ell"] = std::string{library};
    ell::CompilationRequest request;
    request.entry_path = "/project/message.ell";
    request.source = R"ELL(
@Include("lib.ell");
@Card(title: business.name, style: "card")
  @For(item in business.items)<p>@{item}</p>@/For
@/Card
)ELL";
    request.data = {{"business", {{"name", "North & Star"},
                                   {"items", {"one", "two"}}}}};

    const auto result = ell::compile(request, resolver);

    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.find("North &amp; Star") != std::string::npos);
    CHECK(result.generated.html.find("<p>one</p><p>two</p>") != std::string::npos);
    CHECK(result.generated.html.find("color: #7c5cff;") != std::string::npos);
    CHECK_FALSE(result.generated.segments.empty());
    CHECK(result.dependencies.size() == 2);
}

TEST_CASE("compiler applies shell media and final lint") {
    MemoryResolver resolver;
    resolver.files["/project/shell.ell"] = R"ELL(
@Media("(max-width: 600px)")
  .stack { display: block !important; }
@/Media
<!doctype html><html><head></head><body>@Slot(default);<a href="/unsubscribe">Unsubscribe</a></body></html>
)ELL";
    ell::CompilationRequest request;
    request.entry_path = "/project/message.ell";
    request.source = "<p>Hello @{name}</p>";
    request.shell = "shell.ell";
    request.data = {{"name", "Northstar"}};

    const auto result = ell::compile(request, resolver);

    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.starts_with("<!doctype html>"));
    CHECK(result.generated.html.find("@media (max-width: 600px)") != std::string::npos);
    CHECK(result.generated.html.find("<p>Hello Northstar</p>") != std::string::npos);
}

TEST_CASE("compiler rejects missing data and invalid component contracts") {
    MemoryResolver resolver;
    resolver.files["/project/lib.ell"] = std::string{library};
    ell::CompilationRequest request;
    request.entry_path = "/project/message.ell";
    request.source = "@Include(\"lib.ell\"); @Card(unknown: 3);";
    request.data = ell::Json::object();

    const auto result = ell::compile(request, resolver);

    CHECK_FALSE(result.ok());
}

TEST_CASE("compiler enforces output and cancellation limits") {
    MemoryResolver resolver;
    ell::CompilationRequest request;
    request.entry_path = "/project/message.ell";
    request.source = "@For(item in items)<p>@{item}</p>@/For";
    request.data = {{"items", {1, 2, 3}}};
    request.limits.maximum_loop_iterations = 2;
    CHECK_FALSE(ell::compile(request, resolver).ok());

    auto flag = std::make_shared<std::atomic_bool>(true);
    request.limits.maximum_loop_iterations = 100;
    CHECK_FALSE(ell::compile(request, resolver, ell::CancellationToken{flag}).ok());
}

TEST_CASE("compiler detects include cycles") {
    MemoryResolver resolver;
    resolver.files["/project/a.ell"] = "@Include(\"b.ell\");";
    resolver.files["/project/b.ell"] = "@Include(\"a.ell\");";
    ell::CompilationRequest request;
    request.entry_path = "/project/a.ell";
    request.source = resolver.files.at("/project/a.ell");

    CHECK_FALSE(ell::compile(request, resolver).ok());
}
