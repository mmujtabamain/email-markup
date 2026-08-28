#pragma once

#include <string>
#include <string_view>

namespace email_markup::browser
{
    inline constexpr std::string_view protocol_name = "email-markup.browser";
    inline constexpr int protocol_version = 1;
    inline constexpr std::size_t maximum_request_bytes = 1024 * 1024;

    [[nodiscard]] std::string handle_request(std::string_view request);
}
