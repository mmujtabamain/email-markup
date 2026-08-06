#include "ell/core/provenance.hpp"

namespace ell {

void GeneratedHtml::append(const std::string_view text, const SourceRange origin,
                           const std::vector<ExpansionFrame>& stack) {
    if (text.empty()) return;
    const auto start = html.size();
    html.append(text);
    if (!segments.empty() && segments.back().output_end == start &&
        segments.back().origin.source == origin.source &&
        segments.back().origin.start == origin.start &&
        segments.back().origin.end == origin.end &&
        segments.back().expansion_stack == stack) {
        segments.back().output_end = html.size();
        return;
    }
    segments.push_back({start, html.size(), origin, stack});
}

}  // namespace ell
