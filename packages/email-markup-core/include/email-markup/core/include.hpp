#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "email-markup/core/diagnostic.hpp"

namespace email_markup
{

    struct ResolvedFile
    {
        std::filesystem::path canonical_path;
        std::string contents;
    };

    class FileResolver
    {
    public:
        virtual ~FileResolver() = default;
        [[nodiscard]] virtual std::optional<ResolvedFile> resolve(
            const std::filesystem::path &including_file,
            std::string_view requested,
            const std::vector<std::filesystem::path> &search_directories,
            const std::vector<std::filesystem::path> &allowed_roots,
            std::vector<std::filesystem::path> &attempted) = 0;
    };

    class DiskFileResolver final : public FileResolver
    {
    public:
        explicit DiskFileResolver(std::size_t maximum_bytes = 1024 * 1024);
        [[nodiscard]] std::optional<ResolvedFile> resolve(
            const std::filesystem::path &including_file,
            std::string_view requested,
            const std::vector<std::filesystem::path> &search_directories,
            const std::vector<std::filesystem::path> &allowed_roots,
            std::vector<std::filesystem::path> &attempted) override;

    private:
        std::size_t maximum_bytes_;
    };

    class MemoryFileResolver final : public FileResolver
    {
    public:
        explicit MemoryFileResolver(std::vector<ResolvedFile> files,
                                    std::size_t maximum_bytes = 1024 * 1024);
        [[nodiscard]] std::optional<ResolvedFile> resolve(
            const std::filesystem::path &including_file,
            std::string_view requested,
            const std::vector<std::filesystem::path> &search_directories,
            const std::vector<std::filesystem::path> &allowed_roots,
            std::vector<std::filesystem::path> &attempted) override;

    private:
        std::unordered_map<std::string, std::string> files_;
        std::size_t maximum_bytes_;
    };

    [[nodiscard]] std::string portable_path_string(std::filesystem::path path);

    [[nodiscard]] std::optional<std::filesystem::path> normalize_virtual_path(
        const std::filesystem::path &path,
        const std::filesystem::path &base = "/");

} // namespace email_markup
