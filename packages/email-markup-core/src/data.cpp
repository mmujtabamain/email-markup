#include "email-markup/core/data.hpp"

namespace email_markup {

DataResult parse_data(const std::string_view text, const std::size_t maximum_bytes) {
    DataResult result;
    if (text.size() > maximum_bytes) {
        result.diagnostics.push_back({"EM1001", Severity::error,
                                      "JSON input exceeds the configured byte limit.", {}});
        return result;
    }
    try {
        result.data = Json::parse(text);
    } catch (const Json::parse_error& error) {
        result.diagnostics.push_back({"EM1002", Severity::error,
                                      std::string{"Malformed JSON: "} + error.what(), {}});
        return result;
    }
    if (!result.data.is_object()) {
        result.diagnostics.push_back({"EM1003", Severity::error,
                                      "Compile data must have a JSON object at its root.", {}});
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace email_markup
