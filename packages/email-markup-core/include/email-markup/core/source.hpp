#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace email_markup {

using SourceId = std::size_t;

struct SourcePosition {
    std::size_t offset{};
    std::size_t line{};
    std::size_t column{};
};

struct SourceRange {
    SourceId source{};
    std::size_t start{};
    std::size_t end{};

    [[nodiscard]] bool empty() const noexcept { return start == end; }
    bool operator==(const SourceRange&) const = default;
};

struct SourceFile {
    SourceId id{};
    std::filesystem::path path;
    std::string text;
    std::vector<std::size_t> line_starts;

    [[nodiscard]] SourcePosition position(std::size_t offset) const noexcept;
};

class SourceManager {
public:
    SourceId add(std::filesystem::path path, std::string text);
    [[nodiscard]] const SourceFile& get(SourceId id) const;
    [[nodiscard]] const SourceFile* find(const std::filesystem::path& path) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<SourceFile> files_;
};

struct DocumentSnapshot {
    std::shared_ptr<const SourceManager> sources;
    SourceId entry{};
};

[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

}  // namespace email_markup
