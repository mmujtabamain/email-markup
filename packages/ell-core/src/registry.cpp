#include "ell/core/registry.hpp"

namespace ell {

void merge_registry(Registry& target, const Document& source,
                    const bool entry_document,
                    std::vector<Diagnostic>& diagnostics) {
    const auto merge = [&](auto& destination, const auto& additions,
                           const std::string_view kind) {
        for (const auto& [name, definition] : additions) {
            if (const auto found = destination.find(name); found != destination.end() &&
                !entry_document) {
                diagnostics.push_back({"ELL0601", Severity::warning,
                    std::string{kind} + " “" + name +
                    "” replaces an earlier included definition.", definition.range,
                    {{found->second.range, "Earlier definition is here."}}});
            }
            destination[name] = definition;
        }
    };
    merge(target.components, source.components, "Component");
    merge(target.styles, source.styles, "Style");
    merge(target.tokens, source.tokens, "Token");
    target.media.insert(target.media.end(), source.media.begin(), source.media.end());
}

}  // namespace ell
