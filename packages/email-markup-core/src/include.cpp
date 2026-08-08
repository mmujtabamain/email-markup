#include "email-markup/core/include.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "email-markup/core/source.hpp"

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

std::string portable_path(std::filesystem::path path) {
    auto value = path.generic_string();
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

}  // namespace

std::optional<std::filesystem::path> normalize_virtual_path(
    const std::filesystem::path& path, const std::filesystem::path& base) {
    const auto raw = portable_path(path);
    if (raw.empty() || raw.find('\0') != std::string::npos) return std::nullopt;

    auto combined = raw.front() == '/' ? raw : portable_path(base / path);
    if (combined.empty() || combined.front() != '/') return std::nullopt;

    std::vector<std::string> parts;
    std::size_t start = 1;
    while (start <= combined.size()) {
        const auto end = combined.find('/', start);
        const auto part = combined.substr(start, end - start);
        if (part.empty() || part == ".") {
        } else if (part == "..") {
            if (parts.empty()) return std::nullopt;
            parts.pop_back();
        } else {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::string normalized{"/"};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) normalized += '/';
        normalized += parts[index];
    }
    return std::filesystem::path{normalized};
}

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

MemoryFileResolver::MemoryFileResolver(std::vector<ResolvedFile> files,
                                       const std::size_t maximum_bytes)
    : maximum_bytes_(maximum_bytes) {
    for (auto& file : files) {
        if (!portable_path(file.canonical_path).starts_with('/')) {
            throw std::invalid_argument("virtual Email Markup paths must be absolute .em paths");
        }
        const auto normalized = normalize_virtual_path(file.canonical_path);
        if (!normalized || normalized->extension() != ".em") {
            throw std::invalid_argument("virtual Email Markup paths must be absolute .em paths");
        }
        if (file.contents.size() > maximum_bytes_) {
            throw std::invalid_argument("virtual Email Markup source exceeds the byte limit");
        }
        if (!is_valid_utf8(file.contents)) {
            throw std::invalid_argument("virtual Email Markup source is not valid UTF-8");
        }
        const auto [_, inserted] = files_.emplace(normalized->generic_string(),
                                                  std::move(file.contents));
        if (!inserted) {
            throw std::invalid_argument("duplicate virtual Email Markup path: " +
                                        normalized->generic_string());
        }
    }
}

std::optional<ResolvedFile> MemoryFileResolver::resolve(
    const std::filesystem::path& including_file, const std::string_view requested,
    const std::vector<std::filesystem::path>& search_directories,
    const std::vector<std::filesystem::path>& allowed_roots,
    std::vector<std::filesystem::path>& attempted) {
    const std::filesystem::path requested_path{requested};
    std::vector<std::filesystem::path> bases;
    if (portable_path(requested_path).starts_with('/')) {
        bases.emplace_back("/");
    } else {
        bases.push_back(including_file.parent_path());
        bases.insert(bases.end(), search_directories.begin(), search_directories.end());
    }

    for (const auto& base : bases) {
        const auto candidate = normalize_virtual_path(requested_path, base);
        if (!candidate) continue;
        attempted.push_back(*candidate);
        if (candidate->extension() != ".em") continue;

        bool allowed = allowed_roots.empty();
        for (const auto& root : allowed_roots) {
            const auto normalized_root = normalize_virtual_path(root);
            if (normalized_root && beneath(*candidate, *normalized_root)) {
                allowed = true;
                break;
            }
        }
        if (!allowed) continue;

        const auto found = files_.find(candidate->generic_string());
        if (found == files_.end() || found->second.size() > maximum_bytes_) continue;
        return ResolvedFile{*candidate, found->second};
    }
    return std::nullopt;
}

}  // namespace email_markup
