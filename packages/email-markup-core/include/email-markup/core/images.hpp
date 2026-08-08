#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/provenance.hpp"

namespace email_markup
{
    struct ImageResource
    {
        std::string media_type;
        std::string bytes;
    };

    using ImageFetcher =
        std::function<ImageResource(std::string_view url, std::size_t maximum_bytes)>;

    void embed_remote_images(
        GeneratedHtml &generated, std::vector<Diagnostic> &diagnostics,
        const ImageFetcher &fetch,
        std::size_t warning_bytes = 100 * 1024,
        std::size_t maximum_image_bytes = 1024 * 1024,
        std::size_t maximum_html_bytes = 2 * 1024 * 1024);
}
