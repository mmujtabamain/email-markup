#pragma once

#include <map>
#include <string>
#include <string_view>

namespace ell {

using CssDeclarations = std::map<std::string, std::string>;

[[nodiscard]] CssDeclarations parse_declarations(std::string_view css);
[[nodiscard]] std::string serialize_declarations(const CssDeclarations& declarations);
[[nodiscard]] std::string apply_root_styles(std::string html,
                                            const CssDeclarations& additions);
[[nodiscard]] std::string inline_css(std::string html);

}  // namespace ell
