#include "email-markup/core/registry.hpp"

namespace email_markup
{

    void merge_registry(Registry &target, const Document &source,
                        const bool entry_document,
                        std::vector<Diagnostic> &diagnostics)
    {
        const auto merge = [&](auto &destination, const auto &additions,
                               const std::string_view kind)
        {
            for (const auto &[name, definition] : additions)
            {
                if (const auto found = destination.find(name); found != destination.end() &&
                                                               !entry_document)
                {
                    // Two components of one name would make every call ambiguous:
                    // a copy of a library component has to be renamed, so this is
                    // an error. A token or style overridden later stays a warning.
                    if (kind == "Component")
                        diagnostics.push_back({"EM0605", Severity::error, "Component “" + name + "” is defined twice. Rename one of them.", definition.range, {{found->second.range, "The other definition is here."}}});
                    else
                        diagnostics.push_back({"EM0601", Severity::warning, std::string{kind} + " “" + name + "” replaces an earlier included definition.", definition.range, {{found->second.range, "Earlier definition is here."}}});
                }
                destination[name] = definition;
            }
        };
        merge(target.components, source.components, "Component");
        merge(target.styles, source.styles, "Style");
        merge(target.tokens, source.tokens, "Token");
        target.media.insert(target.media.end(), source.media.begin(), source.media.end());
    }

} // namespace email_markup
