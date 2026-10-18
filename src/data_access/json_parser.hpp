#ifndef F1_SIM_DATA_ACCESS_JSON_PARSER_HPP
#define F1_SIM_DATA_ACCESS_JSON_PARSER_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace f1_sim {
namespace data_access {
namespace detail {

/**
 * @brief Internal representation of a parsed JSON value (strictly hidden from public DAL API).
 */
struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    enum class Type {
        Null,
        Bool,
        Integer,
        Double,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> storage;

    bool isNull() const noexcept { return type == Type::Null; }
    bool isBool() const noexcept { return type == Type::Bool; }
    bool isInteger() const noexcept { return type == Type::Integer; }
    bool isNumber() const noexcept { return type == Type::Integer || type == Type::Double; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }

    bool asBool() const { return std::get<bool>(storage); }
    std::int64_t asInteger() const { return std::get<std::int64_t>(storage); }
    double asNumber() const {
        if (type == Type::Integer) {
            return static_cast<double>(std::get<std::int64_t>(storage));
        }
        return std::get<double>(storage);
    }
    const std::string& asString() const { return std::get<std::string>(storage); }
    const Array& asArray() const { return std::get<Array>(storage); }
    const Object& asObject() const { return std::get<Object>(storage); }

    const JsonValue* findKey(const std::string& key) const noexcept {
        if (type != Type::Object) {
            return nullptr;
        }
        const auto& obj = std::get<Object>(storage);
        auto it = obj.find(key);
        return (it != obj.end()) ? &it->second : nullptr;
    }

    const char* typeName() const noexcept {
        switch (type) {
            case Type::Null:    return "null";
            case Type::Bool:    return "boolean";
            case Type::Integer: return "integer";
            case Type::Double:  return "number";
            case Type::String:  return "string";
            case Type::Array:   return "array";
            case Type::Object:  return "object";
        }
        return "unknown";
    }
};

struct JsonParseError {
    std::string message;
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t byte_offset = 0;
};

struct JsonParseResult {
    std::optional<JsonValue> value;
    std::optional<JsonParseError> error;

    bool ok() const noexcept { return value.has_value() && !error.has_value(); }
};

/**
 * @brief Parse a UTF-8 JSON document according to RFC 8259.
 *        Rejects trailing tokens, duplicate object keys, NaN/Infinity, and invalid escapes.
 */
JsonParseResult parseJsonDocument(const std::string& input);

} // namespace detail
} // namespace data_access
} // namespace f1_sim

#endif // F1_SIM_DATA_ACCESS_JSON_PARSER_HPP