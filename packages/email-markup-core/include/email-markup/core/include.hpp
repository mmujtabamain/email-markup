#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "email-markup/core/diagnostic.hpp"

namespace email_markup {

struct ResolvedFile {
    std::filesystem::path canonical_path;
    std::string contents;
};

class FileResolver {
public:
    virtual ~FileResolver() = default;
    [[nodiscard]] virtual std::optional<ResolvedFile> resolve(
        const std::filesystem::path& including_file,
        std::string_view requested,
        const std::vector<std::filesystem::path>& search_directories,
        const std::vector<std::filesystem::path>& allowed_roots,
        std::vector<std::filesystem::path>& attempted) = 0;
};

class DiskFileResolver final : public FileResolver {
public:
    explicit DiskFileResolver(std::size_t maximum_bytes = 1024 * 1024);
    [[nodiscard]] std::optional<ResolvedFile> resolve(
        const std::filesystem::path& including_file,
        std::string_view requested,
        const std::vector<std::filesystem::path>& search_directories,
        const std::vector<std::filesystem::path>& allowed_roots,
        std::vector<std::filesystem::path>& attempted) override;

private:
    std::size_t maximum_bytes_;
};

}  // namespace email_markup
