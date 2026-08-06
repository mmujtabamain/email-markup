#pragma once

#include <string>
#include <string_view>

namespace ell {

[[nodiscard]] std::string format_source(std::string_view source);

}  // namespace ell
