#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "protocol/json.hpp"

namespace email_markup::lsp
{
    struct OpenDocument
    {
        std::filesystem::path path;
        std::string text;
        std::int64_t version{};
    };

    class DocumentStore final
    {
    public:
        OpenDocument &open(std::string uri, std::filesystem::path path, std::string text,
                           std::int64_t version);
        [[nodiscard]] OpenDocument *find(const std::string &uri) noexcept;
        [[nodiscard]] const OpenDocument *find(const std::string &uri) const noexcept;
        [[nodiscard]] OpenDocument *apply_changes(const Json &params);
        void close(const std::string &uri);

        [[nodiscard]] bool has_version(const std::string &uri,
                                       std::int64_t version) const noexcept;
        [[nodiscard]] const std::unordered_map<std::string, OpenDocument> &all() const noexcept;

    private:
        std::unordered_map<std::string, OpenDocument> documents_;
    };
} // namespace email_markup::lsp
