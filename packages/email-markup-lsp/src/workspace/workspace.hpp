#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "documents/document_store.hpp"
#include "email-markup/core/ast.hpp"
#include "email-markup/core/render.hpp"
#include "email-markup/runtime/assets.hpp"
#include "protocol/json.hpp"

namespace email_markup::platform
{
    class System;
}

namespace email_markup::lsp
{
    class Workspace final
    {
    public:
        Workspace(const platform::System &system, const runtime::Assets &assets);

        Workspace(const Workspace &) = delete;
        Workspace &operator=(const Workspace &) = delete;

        void set_roots(std::vector<std::filesystem::path> roots);
        [[nodiscard]] email_markup::CompilationRequest compilation_request(
            const OpenDocument &document, const Json *preview_data = nullptr) const;
        [[nodiscard]] email_markup::CompilationResult compile(
            const OpenDocument &document, const Json *preview_data = nullptr) const;
        [[nodiscard]] Json diagnostics(const email_markup::CompilationResult &result,
                                       const OpenDocument &document) const;
        [[nodiscard]] std::unordered_map<std::string, email_markup::ComponentDefinition>
        metadata(const OpenDocument &document) const;

        [[nodiscard]] const std::set<std::string> &styles() const noexcept;
        [[nodiscard]] const std::set<std::string> &tokens() const noexcept;

    private:
        [[nodiscard]] std::string read_optional(const std::filesystem::path &path) const;
        [[nodiscard]] std::optional<std::filesystem::path> project_config(
            const std::filesystem::path &file) const;
        [[nodiscard]] std::filesystem::path expand(
            std::string value, const std::filesystem::path &base) const;
        void load_library_metadata();

        const platform::System &system_;
        const runtime::Assets &assets_;
        std::vector<std::filesystem::path> roots_;
        std::unordered_map<std::string, email_markup::ComponentDefinition> components_;
        std::set<std::string> styles_;
        std::set<std::string> tokens_;
    };
} // namespace email_markup::lsp
