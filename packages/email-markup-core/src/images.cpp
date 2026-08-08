#include "email-markup/core/images.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>

namespace email_markup
{
    namespace
    {
        struct Replacement
        {
            std::size_t offset{};
            std::size_t size{};
            std::string text;
            SourceRange origin;
            std::vector<ExpansionFrame> stack;
        };

        std::string base64(const std::string_view bytes)
        {
            static constexpr std::string_view alphabet{
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
            std::string output;
            output.reserve(((bytes.size() + 2) / 3) * 4);
            for (std::size_t index = 0; index < bytes.size(); index += 3)
            {
                const auto first = static_cast<unsigned char>(bytes[index]);
                const auto second = index + 1 < bytes.size()
                                        ? static_cast<unsigned char>(bytes[index + 1])
                                        : 0;
                const auto third = index + 2 < bytes.size()
                                       ? static_cast<unsigned char>(bytes[index + 2])
                                       : 0;
                const auto value = (static_cast<unsigned int>(first) << 16) |
                                   (static_cast<unsigned int>(second) << 8) |
                                   static_cast<unsigned int>(third);
                output.push_back(alphabet[(value >> 18) & 0x3f]);
                output.push_back(alphabet[(value >> 12) & 0x3f]);
                output.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6) & 0x3f]
                                                          : '=');
                output.push_back(index + 2 < bytes.size() ? alphabet[value & 0x3f] : '=');
            }
            return output;
        }

        std::string decode_url(std::string value)
        {
            for (const auto &[entity, decoded] : {
                     std::pair{"&amp;", "&"}, std::pair{"&quot;", "\""},
                     std::pair{"&#39;", "'"}, std::pair{"&lt;", "<"},
                     std::pair{"&gt;", ">"}})
            {
                std::size_t position = 0;
                while ((position = value.find(entity, position)) != std::string::npos)
                {
                    value.replace(position, std::string_view{entity}.size(), decoded);
                    position += std::string_view{decoded}.size();
                }
            }
            return value;
        }

        std::string media_type(std::string value)
        {
            if (const auto parameters = value.find(';'); parameters != std::string::npos)
                value.erase(parameters);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                                     { return !std::isspace(ch); }));
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            if (!value.starts_with("image/") ||
                !std::all_of(value.begin(), value.end(), [](unsigned char ch)
                             { return std::isalnum(ch) || ch == '/' || ch == '+' ||
                                      ch == '.' || ch == '-'; }))
            {
                throw std::runtime_error("response Content-Type is not an image");
            }
            return value;
        }

        const OutputSegment *segment_at(const GeneratedHtml &generated,
                                        const std::size_t offset)
        {
            const auto found = std::find_if(
                generated.segments.begin(), generated.segments.end(),
                [&](const auto &segment)
                { return offset >= segment.output_start && offset < segment.output_end; });
            return found == generated.segments.end() ? nullptr : &*found;
        }
    } // namespace

    void embed_remote_images(GeneratedHtml &generated,
                             std::vector<Diagnostic> &diagnostics,
                             const ImageFetcher &fetch,
                             const std::size_t warning_bytes,
                             const std::size_t maximum_image_bytes,
                             const std::size_t maximum_html_bytes)
    {
        static const std::regex image{R"(<img\b[^>]*>)", std::regex::icase};
        static const std::regex marker{
            R"(\sdata-email-markup-embed\s*=\s*(["'])(true|false)\1)",
            std::regex::icase};
        static const std::regex source{
            R"(\ssrc\s*=\s*(["'])([^"']*)\1)", std::regex::icase};
        std::vector<Replacement> replacements;
        auto projected_html_size = generated.html.size();

        for (std::sregex_iterator it{generated.html.begin(), generated.html.end(), image}, end;
             it != end; ++it)
        {
            auto tag = it->str();
            std::smatch marker_match;
            if (!std::regex_search(tag, marker_match, marker))
                continue;
            const auto offset = static_cast<std::size_t>(it->position());
            const auto *segment = segment_at(generated, offset);
            const auto origin = segment ? segment->origin : SourceRange{};
            const auto stack = segment ? segment->expansion_stack
                                       : std::vector<ExpansionFrame>{};
            const auto diagnostic_range =
                segment && !segment->expansion_stack.empty()
                    ? segment->expansion_stack.front().call_site
                    : origin;
            const auto should_embed = marker_match[2].str() == "true";
            const auto marker_offset = static_cast<std::size_t>(marker_match.position());
            tag.erase(marker_offset, static_cast<std::size_t>(marker_match.length()));
            projected_html_size -= static_cast<std::size_t>(marker_match.length());

            if (should_embed)
            {
                std::smatch source_match;
                if (std::regex_search(tag, source_match, source))
                {
                    const auto url = decode_url(source_match[2].str());
                    if (fetch &&
                        (url.starts_with("https://") || url.starts_with("http://")))
                    {
                        try
                        {
                            const auto resource = fetch(url, maximum_image_bytes);
                            if (resource.bytes.size() > warning_bytes)
                            {
                                diagnostics.push_back(
                                    {"EM0816", Severity::warning,
                                     "Base64 image source is " +
                                         std::to_string(resource.bytes.size()) +
                                         " bytes and will grow by about one third; use embed: false for a remote image.",
                                     diagnostic_range});
                            }
                            const auto data = "data:" + media_type(resource.media_type) +
                                              ";base64," + base64(resource.bytes);
                            const auto embedded_size = projected_html_size -
                                                       source_match[2].length() + data.size();
                            if (embedded_size > maximum_html_bytes)
                                throw std::runtime_error(
                                    "embedded data would exceed the generated HTML byte limit");
                            projected_html_size = embedded_size;
                            tag.replace(static_cast<std::size_t>(source_match.position(2)),
                                        static_cast<std::size_t>(source_match.length(2)), data);
                        }
                        catch (const std::exception &error)
                        {
                            diagnostics.push_back(
                                {"EM0815", Severity::warning,
                                 "Image could not be embedded from " + url + ": " +
                                     error.what() + ". The remote URL was retained.",
                                 diagnostic_range});
                        }
                    }
                }
            }
            replacements.push_back(
                {offset, static_cast<std::size_t>(it->length()), std::move(tag), origin, stack});
        }

        for (auto replacement = replacements.rbegin(); replacement != replacements.rend();
             ++replacement)
        {
            generated.replace(replacement->offset, replacement->size, replacement->text,
                              replacement->origin, replacement->stack);
        }
    }
}
