#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ell/core/ast.hpp"
#include "ell/core/diagnostic.hpp"
#include "ell/core/expr.hpp"
#include "ell/core/include.hpp"
#include "ell/core/provenance.hpp"
#include "ell/core/registry.hpp"

namespace ell {

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag);
    [[nodiscard]] bool is_cancelled() const noexcept;

private:
    std::shared_ptr<std::atomic_bool> flag_;
};

struct CompilationLimits {
    std::size_t maximum_source_bytes{1024 * 1024};
    std::size_t maximum_json_bytes{1024 * 1024};
    std::size_t maximum_includes{128};
    std::size_t maximum_include_depth{32};
    std::size_t maximum_expansion_depth{64};
    std::size_t maximum_loop_iterations{10000};
    std::size_t maximum_ast_nodes{200000};
    std::size_t maximum_html_bytes{2 * 1024 * 1024};
    std::size_t maximum_diagnostics{100};
};

struct CompilationRequest {
    std::filesystem::path entry_path;
    std::string source;
    Json data{Json::object()};
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> allowed_roots;
    std::vector<std::filesystem::path> imports;
    std::optional<std::filesystem::path> shell;
    CompilationLimits limits;
};

struct CompilationResult {
    std::shared_ptr<const DocumentSnapshot> snapshot;
    GeneratedHtml generated;
    std::vector<std::filesystem::path> dependencies;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] CompilationResult compile(
    const CompilationRequest& request, FileResolver& files,
    CancellationToken cancellation = {});

}  // namespace ell
