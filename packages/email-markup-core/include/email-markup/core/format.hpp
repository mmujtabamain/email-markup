#pragma once

#include <string>
#include <string_view>

namespace email_markup {

[[nodiscard]] std::string format_source(std::string_view source);

}  // namespace email_markup
