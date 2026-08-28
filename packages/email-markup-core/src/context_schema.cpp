#include "email-markup/core/context_schema.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace email_markup
{
    namespace
    {
        bool known_type(const std::string_view type)
        {
            return type == "object" || type == "array" || type == "string" ||
                   type == "int" || type == "decimal" || type == "number" ||
                   type == "bool" || type == "name" || type == "url" || type == "email";
        }

        bool scalar_ok(const std::string &type, const Json &value)
        {
            if (type == "string" || type == "name" || type == "url" || type == "email")
                return value.is_string();
            if (type == "int") return value.is_number_integer();
            if (type == "decimal" || type == "number") return value.is_number();
            if (type == "bool") return value.is_boolean();
            return false;
        }

        void validate_value(const Json &field, const Json *value, const std::string &path,
                            std::vector<std::string> &errors)
        {
            if (!value)
            {
                if (field.value("required", false)) errors.push_back(path + " is required");
                return;
            }
            if (value->is_null())
            {
                if (!field.value("nullable", false)) errors.push_back(path + " cannot be null");
                return;
            }
            const auto type = field.at("type").get<std::string>();
            if (type == "object")
            {
                if (!value->is_object()) { errors.push_back(path + " must be an object"); return; }
                for (const auto &[name, child] : field.value("fields", Json::object()).items())
                    validate_value(child, value->contains(name) ? &value->at(name) : nullptr,
                                   path.empty() ? name : path + "." + name, errors);
                return;
            }
            if (type == "array")
            {
                if (!value->is_array()) { errors.push_back(path + " must be an array"); return; }
                for (std::size_t index = 0; index < value->size(); ++index)
                    validate_value(field.at("items"), &value->at(index),
                                   path + "[" + std::to_string(index) + "]", errors);
                return;
            }
            if (!scalar_ok(type, *value))
            {
                errors.push_back(path + " has the wrong type");
                return;
            }
            if (value->is_number())
            {
                const auto number = value->get<double>();
                if (field.contains("minimum") && number < field.at("minimum").get<double>())
                    errors.push_back(path + " is below its minimum");
                if (field.contains("maximum") && number > field.at("maximum").get<double>())
                    errors.push_back(path + " is above its maximum");
            }
            if (value->is_string())
            {
                const auto length = value->get_ref<const std::string &>().size();
                if (field.contains("minLength") &&
                    length < field.at("minLength").get<std::size_t>())
                    errors.push_back(path + " is shorter than its minimum length");
                if (field.contains("maxLength") &&
                    length > field.at("maxLength").get<std::size_t>())
                    errors.push_back(path + " is longer than its maximum length");
            }
        }

        void validate_field(const Json &field, const std::string &path)
        {
            if (!field.is_object() || !field.value("type", Json{}).is_string())
                throw std::invalid_argument("Context schema field " + path + " requires a type.");
            const auto type = field.at("type").get<std::string>();
            if (!known_type(type))
                throw std::invalid_argument("Context schema field " + path + " has an unsupported type.");
            if (type == "object")
            {
                if (!field.value("fields", Json{}).is_object())
                    throw std::invalid_argument("Object field " + path + " requires fields.");
                for (const auto &[name, child] : field.at("fields").items())
                    validate_field(child, path.empty() ? name : path + "." + name);
            }
            if (type == "array")
            {
                if (!field.contains("items"))
                    throw std::invalid_argument("Array field " + path + " requires items.");
                validate_field(field.at("items"), path + "[]");
            }
            if (field.contains("minimum") && field.contains("maximum") &&
                field.at("minimum").get<double>() > field.at("maximum").get<double>())
                throw std::invalid_argument("Context schema field " + path +
                                            " has an invalid numeric range.");
            if (field.contains("minLength") && field.contains("maxLength") &&
                field.at("minLength").get<std::size_t>() >
                    field.at("maxLength").get<std::size_t>())
                throw std::invalid_argument("Context schema field " + path +
                                            " has an invalid length range.");
            if (field.contains("example"))
            {
                std::vector<std::string> errors;
                validate_value(field, &field.at("example"), path, errors);
                if (!errors.empty())
                    throw std::invalid_argument("Context schema example at " + path +
                                                " does not match its type.");
            }
        }

        Json example_for(const Json &field, const std::string &path)
        {
            if (field.contains("example")) return field.at("example");
            const auto type = field.at("type").get<std::string>();
            if (type == "object")
            {
                Json value = Json::object();
                for (const auto &[name, child] : field.at("fields").items())
                    value[name] = example_for(child, path.empty() ? name : path + "." + name);
                return value;
            }
            if (type == "array") return Json::array({example_for(field.at("items"), path + "[]")});
            if (type == "int") return 1;
            if (type == "decimal" || type == "number") return 1.0;
            if (type == "bool") return true;
            if (type == "url") return "https://example.invalid/" + path;
            if (type == "email") return "example@example.invalid";
            if (type == "name") return "Name";
            return path;
        }

        void add_paths(const Json &fields, const std::string &prefix,
                       std::vector<std::string> &paths)
        {
            std::vector<std::string> names;
            for (const auto &[name, _] : fields.items()) names.push_back(name);
            std::sort(names.begin(), names.end());
            for (const auto &name : names)
            {
                const auto path = prefix.empty() ? name : prefix + "." + name;
                paths.push_back(path);
                if (fields.at(name).at("type") == "object")
                    add_paths(fields.at(name).at("fields"), path, paths);
            }
        }

        Json project_field(const Json &field)
        {
            const auto type = field.at("type").get<std::string>();
            Json result = Json::object();
            if (type == "object")
            {
                result = {{"type", "object"}, {"additionalProperties", false},
                          {"properties", Json::object()}};
                Json required = Json::array();
                for (const auto &[name, child] : field.at("fields").items())
                {
                    result["properties"][name] = project_field(child);
                    if (child.value("required", false)) required.push_back(name);
                }
                if (!required.empty()) result["required"] = std::move(required);
            }
            else if (type == "array")
                result = {{"type", "array"}, {"items", project_field(field.at("items"))}};
            else
            {
                result["type"] = type == "int" ? "integer" :
                                 type == "decimal" || type == "number" ? "number" :
                                 type == "bool" ? "boolean" : "string";
                if (type == "url") result["format"] = "uri";
                if (type == "email") result["format"] = "email";
            }
            for (const auto *key : {"description", "minimum", "maximum",
                                    "minLength", "maxLength", "example"})
                if (field.contains(key)) result[key] = field.at(key);
            if (field.value("nullable", false))
                result["type"] = Json::array({result.at("type"), "null"});
            return result;
        }
    }

    ContextSchema parse_context_schema(const Json &document)
    {
        if (!document.is_object() || document.value("format", "") != "email-markup-context" ||
            document.value("version", 0) != 1 || !document.value("name", Json{}).is_string() ||
            !document.value("fields", Json{}).is_object())
            throw std::invalid_argument("Invalid Email Markup context schema envelope.");
        ContextSchema schema{document.at("name").get<std::string>(), document.at("fields")};
        for (const auto &[name, field] : schema.fields.items()) validate_field(field, name);
        return schema;
    }

    Json context_schema_example(const ContextSchema &schema)
    {
        Json result = Json::object();
        for (const auto &[name, field] : schema.fields.items()) result[name] = example_for(field, name);
        return result;
    }

    Json context_schema_json_schema(const ContextSchema &schema)
    {
        Json root{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                  {"title", schema.name}, {"type", "object"},
                  {"additionalProperties", false}, {"properties", Json::object()}};
        Json required = Json::array();
        for (const auto &[name, field] : schema.fields.items())
        {
            root["properties"][name] = project_field(field);
            if (field.value("required", false)) required.push_back(name);
        }
        if (!required.empty()) root["required"] = std::move(required);
        return root;
    }

    std::vector<std::string> context_schema_paths(const ContextSchema &schema)
    {
        std::vector<std::string> result;
        add_paths(schema.fields, "", result);
        return result;
    }

    std::vector<std::string> validate_context_data(const ContextSchema &schema, const Json &data)
    {
        if (!data.is_object()) return {"Context data must be an object"};
        std::vector<std::string> errors;
        for (const auto &[name, field] : schema.fields.items())
            validate_value(field, data.contains(name) ? &data.at(name) : nullptr, name, errors);
        return errors;
    }
}
