#include "email-markup/core/include.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace email_markup {
namespace {

bool beneath(const std::filesystem::path& child, const std::filesystem::path& root) {
    auto child_it = child.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++child_it) {
        if (child_it == child.end() || *child_it != *root_it) return false;
    }
    return true;
}

}  // namespace

DiskFileResolver::DiskFileResolver(const std::size_t maximum_bytes)
    : maximum_bytes_(maximum_bytes) {}

std::optional<ResolvedFile> DiskFileResolver::resolve(
    const std::filesystem::path& including_file, const std::string_view requested,
    const std::vector<std::filesystem::path>& search_directories,
    const std::vector<std::filesystem::path>& allowed_roots,
    std::vector<std::filesystem::path>& attempted) {
    std::vector<std::filesystem::path> bases{including_file.parent_path()};
    bases.insert(bases.end(), search_directories.begin(), search_directories.end());
    for (const auto& base : bases) {
        const auto candidate = base / requested;
        attempted.push_back(candidate.lexically_normal());
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(candidate, error);
        if (error || !std::filesystem::is_regular_file(canonical, error)) continue;
        if (canonical.extension() != ".em") continue;
        bool allowed = allowed_roots.empty();
        for (const auto& root : allowed_roots) {
            auto canonical_root = std::filesystem::weakly_canonical(root, error);
            if (!error && beneath(canonical, canonical_root)) {
                allowed = true;
                break;
            }
        }
        if (!allowed) continue;
        const auto size = std::filesystem::file_size(canonical, error);
        if (error || size > maximum_bytes_) continue;
        std::ifstream stream(canonical, std::ios::binary);
        if (!stream) continue;
        return ResolvedFile{canonical, std::string{std::istreambuf_iterator<char>{stream}, {}}};
    }
    return std::nullopt;
}

}  // namespace email_markup
