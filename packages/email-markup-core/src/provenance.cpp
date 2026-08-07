#include "email-markup/core/provenance.hpp"

namespace email_markup {

namespace {

void push_segment(std::vector<OutputSegment>& segments, OutputSegment segment) {
    if (segment.output_start == segment.output_end) return;
    if (!segments.empty() && segments.back().output_end == segment.output_start &&
        segments.back().origin.source == segment.origin.source &&
        segments.back().origin.start == segment.origin.start &&
        segments.back().origin.end == segment.origin.end &&
        segments.back().expansion_stack == segment.expansion_stack) {
        segments.back().output_end = segment.output_end;
        return;
    }
    segments.push_back(std::move(segment));
}

}  // namespace

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

void GeneratedHtml::insert(const std::size_t offset, const std::string_view text,
                           const SourceRange origin,
                           const std::vector<ExpansionFrame>& stack) {
    if (text.empty()) return;
    const auto position = std::min(offset, html.size());
    const auto size = text.size();
    std::vector<OutputSegment> updated;
    updated.reserve(segments.size() + 2);
    bool inserted = false;
    const auto add_inserted = [&] {
        if (inserted) return;
        push_segment(updated, {position, position + size, origin, stack});
        inserted = true;
    };
    for (const auto& segment : segments) {
        if (segment.output_end <= position) {
            push_segment(updated, segment);
        } else if (segment.output_start >= position) {
            add_inserted();
            auto shifted = segment;
            shifted.output_start += size;
            shifted.output_end += size;
            push_segment(updated, std::move(shifted));
        } else {
            push_segment(updated, {segment.output_start, position,
                                   segment.origin, segment.expansion_stack});
            add_inserted();
            push_segment(updated, {position + size, segment.output_end + size,
                                   segment.origin, segment.expansion_stack});
        }
    }
    add_inserted();
    html.insert(position, text);
    segments = std::move(updated);
}

}  // namespace email_markup
