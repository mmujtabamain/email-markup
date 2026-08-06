#include "ell/core/source.hpp"

#include <algorithm>
#include <stdexcept>

namespace ell {

SourcePosition SourceFile::position(const std::size_t requested) const noexcept {
    const auto offset = std::min(requested, text.size());
    const auto next = std::upper_bound(line_starts.begin(), line_starts.end(), offset);
    const auto index = next == line_starts.begin()
                           ? std::size_t{0}
                           : static_cast<std::size_t>(next - line_starts.begin() - 1);
    return {offset, index, offset - line_starts[index]};
}

SourceId SourceManager::add(std::filesystem::path path, std::string text) {
    SourceFile file;
    file.id = files_.size();
    file.path = std::move(path);
    file.text = std::move(text);
    file.line_starts.push_back(0);
    for (std::size_t i = 0; i < file.text.size(); ++i) {
        if (file.text[i] == '\n') {
            file.line_starts.push_back(i + 1);
        }
    }
    files_.push_back(std::move(file));
    return files_.back().id;
}

const SourceFile& SourceManager::get(const SourceId id) const {
    if (id >= files_.size()) {
        throw std::out_of_range("invalid ELL source id");
    }
    return files_[id];
}

const SourceFile* SourceManager::find(const std::filesystem::path& path) const {
    const auto found = std::find_if(files_.begin(), files_.end(), [&](const auto& file) {
        return file.path == path;
    });
    return found == files_.end() ? nullptr : &*found;
}

std::size_t SourceManager::size() const noexcept { return files_.size(); }

bool is_valid_utf8(const std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t continuation = 0;
        if (byte <= 0x7f) {
            ++i;
            continue;
        }
        if ((byte & 0xe0U) == 0xc0U) continuation = 1;
        else if ((byte & 0xf0U) == 0xe0U) continuation = 2;
        else if ((byte & 0xf8U) == 0xf0U) continuation = 3;
        else return false;
        if (i + continuation >= text.size()) return false;
        for (std::size_t j = 1; j <= continuation; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0U) != 0x80U) return false;
        }
        if (continuation == 1 && byte < 0xc2U) return false;
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if (continuation == 2 && byte == 0xe0U && second < 0xa0U) return false;
        if (continuation == 2 && byte == 0xedU && second >= 0xa0U) return false;
        if (continuation == 3 && byte == 0xf0U && second < 0x90U) return false;
        if (continuation == 3 && byte > 0xf4U) return false;
        if (continuation == 3 && byte == 0xf4U && second >= 0x90U) return false;
        i += continuation + 1;
    }
    return true;
}

}  // namespace ell
