#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "email-markup/core/context_schema.hpp"
#include "email-markup/core/diagnostic.hpp"
#include "email-markup/core/source.hpp"

namespace email_markup::detail
{
    /// An unguarded use of a context field that can be missing.
    inline constexpr std::string_view context_guard_code = "EM0910";

    /// Every use of a context field the schema allows to be missing — not
    /// `required`, `nullable`, or under a parent that can be missing — made
    /// outside an `@If[...]` branch that proves it present, as an error.
    ///
    /// `document` is the EMIR document's `children`. A branch proves a path
    /// present when its condition is that path, an `and` including it, or the
    /// else of a `not` of it; a present parent proves a child the schema
    /// requires (and does not let be null). `or`, comparisons and else slots
    /// prove nothing. Values bound by `@For[...]` are not context fields.
    /// `checked` says which sources are the project's own: uses in the
    /// packaged library are not reported. Each diagnostic's `json_path` is
    /// the field's dotted path.
    [[nodiscard]] std::vector<Diagnostic> check_context_guards(
        const Json &document, const ContextSchema &schema,
        const std::function<bool(SourceId)> &checked);
}
