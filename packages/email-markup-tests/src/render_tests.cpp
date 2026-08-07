#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "email-markup/core/render.hpp"

namespace {

class MemoryResolver final : public email_markup::FileResolver {
public:
    std::unordered_map<std::string, std::string> files;

    std::optional<email_markup::ResolvedFile> resolve(
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
        return email_markup::ResolvedFile{path, found->second};
    }
};

constexpr std::string_view library = R"EM(
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
)EM";

}  // namespace

TEST_CASE("compiler renders data loops components styles and provenance") {
    MemoryResolver resolver;
    resolver.files["/project/lib.em"] = std::string{library};
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = R"EM(
@Include("lib.em");
@Card(title: business.name, style: "card")
  @For(item in business.items)<p>@{item}</p>@/For
@/Card
)EM";
    request.data = {{"business", {{"name", "North & Star"},
                                   {"items", {"one", "two"}}}}};

    const auto result = email_markup::compile(request, resolver);

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
    resolver.files["/project/shell.em"] = R"EM(
@Media("(max-width: 600px)")
  .stack { display: block !important; }
@/Media
<!doctype html><html><head></head><body>@Slot(default);<a href="/unsubscribe">Unsubscribe</a></body></html>
)EM";
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = "<p>Hello @{name}</p>";
    request.shell = "shell.em";
    request.data = {{"name", "Northstar"}};

    const auto result = email_markup::compile(request, resolver);

    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.starts_with("<!doctype html>"));
    CHECK(result.generated.html.find("@media (max-width: 600px)") != std::string::npos);
    CHECK(result.generated.html.find("<p>Hello Northstar</p>") != std::string::npos);
}

TEST_CASE("compiled HTML lint findings map back to the component call") {
    MemoryResolver resolver;
    resolver.files["/project/image.em"] = R"EM(
@DefineComponent(name: "HeroImage")
  @Props
    src: url
  @/Props
  @Template
    <img src="@{src}">
  @/Template
@/DefineComponent
)EM";
    resolver.files["/project/shell.em"] =
        "<html><body>@Slot(default);<a href=\"/unsubscribe\">Unsubscribe</a></body></html>";
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = R"EM(@Include("image.em");

@HeroImage(src: "http://example.test/image.png");
)EM";
    request.shell = "shell.em";

    const auto result = email_markup::compile(request, resolver);
    const auto call = request.source.find("@HeroImage");
    const auto missing_alt = std::find_if(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.code == "EM0810"; });
    const auto insecure = std::find_if(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.code == "EM0811"; });

    REQUIRE(missing_alt != result.diagnostics.end());
    REQUIRE(insecure != result.diagnostics.end());
    CHECK(missing_alt->range.source == result.snapshot->entry);
    CHECK(insecure->range.source == result.snapshot->entry);
    CHECK(missing_alt->range.start == call);
    CHECK(insecure->range.start == call);
}

TEST_CASE("compiler rejects missing data and invalid component contracts") {
    MemoryResolver resolver;
    resolver.files["/project/lib.em"] = std::string{library};
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = "@Include(\"lib.em\"); @Card(unknown: 3);";
    request.data = email_markup::Json::object();

    const auto result = email_markup::compile(request, resolver);

    CHECK_FALSE(result.ok());
}

TEST_CASE("compiler enforces output and cancellation limits") {
    MemoryResolver resolver;
    email_markup::CompilationRequest request;
    request.entry_path = "/project/message.em";
    request.source = "@For(item in items)<p>@{item}</p>@/For";
    request.data = {{"items", {1, 2, 3}}};
    request.limits.maximum_loop_iterations = 2;
    CHECK_FALSE(email_markup::compile(request, resolver).ok());

    auto flag = std::make_shared<std::atomic_bool>(true);
    request.limits.maximum_loop_iterations = 100;
    CHECK_FALSE(email_markup::compile(request, resolver, email_markup::CancellationToken{flag}).ok());
}

TEST_CASE("compiler detects include cycles") {
    MemoryResolver resolver;
    resolver.files["/project/a.em"] = "@Include(\"b.em\");";
    resolver.files["/project/b.em"] = "@Include(\"a.em\");";
    email_markup::CompilationRequest request;
    request.entry_path = "/project/a.em";
    request.source = resolver.files.at("/project/a.em");

    CHECK_FALSE(email_markup::compile(request, resolver).ok());
}

TEST_CASE("standard library and neutral shell compile a shipped example") {
    const auto root = std::filesystem::path{EMAIL_MARKUP_SOURCE_DIR};
    const auto example = root / "examples/01-interpolation";
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{stream}, {}};
    };
    email_markup::DiskFileResolver resolver;
    email_markup::CompilationRequest request;
    request.entry_path = example / "message.em";
    request.source = read(request.entry_path);
    request.data = email_markup::Json::parse(read(example / "data.json"));
    request.include_directories = {root / "lib", root / "examples/_shared"};
    request.allowed_roots = {root};
    request.imports = {root / "lib/builtins.em", root / "examples/_shared/theme.em"};
    request.shell = root / "examples/_shared/shell.em";

    const auto result = email_markup::compile(request, resolver);

    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html.find("Hello, Avery") != std::string::npos);
    CHECK(result.generated.html.find("Research &amp; Design &lt;Studio&gt;") != std::string::npos);
    CHECK(result.generated.html.find("Unsubscribe") != std::string::npos);
    CHECK(result.generated.html.find("@{") == std::string::npos);
    CHECK(result.generated.html == read(example / "message.html"));
}

TEST_CASE("CSS inlining example compiles to its golden") {
    const auto root = std::filesystem::path{EMAIL_MARKUP_SOURCE_DIR};
    const auto example = root / "examples/09-css-inlining";
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{stream}, {}};
    };
    email_markup::DiskFileResolver resolver;
    email_markup::CompilationRequest request;
    request.entry_path = example / "message.em";
    request.source = read(request.entry_path);
    request.data = email_markup::Json::parse(read(example / "data.json"));
    request.include_directories = {root / "lib", root / "examples/_shared"};
    request.allowed_roots = {root};
    request.imports = {root / "lib/builtins.em", root / "examples/_shared/theme.em"};
    request.shell = root / "examples/_shared/shell.em";

    const auto result = email_markup::compile(request, resolver);

    INFO((result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    CHECK(result.generated.html == read(example / "message.html"));
    CHECK(result.generated.html.find("<style>") == std::string::npos);
    CHECK(result.generated.html.find("<section style=\"background: #eff6ff;") != std::string::npos);
}
