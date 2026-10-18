#include "json_parser.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace f1_sim {
namespace data_access {
namespace detail {

namespace {

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src) {}

    JsonParseResult parse() {
        skipWhitespace();
        if (eof()) {
            return makeError("Empty JSON document");
        }
        auto root = parseValue();
        if (!root.has_value()) {
            return JsonParseResult{std::nullopt, error_};
        }
        skipWhitespace();
        if (!eof()) {
            return makeError("Unexpected trailing content after root JSON value");
        }
        return JsonParseResult{std::move(root), std::nullopt};
    }

private:
    bool eof() const noexcept { return pos_ >= src_.size(); }

    char peek() const noexcept { return eof() ? '\0' : src_[pos_]; }

    char advance() noexcept {
        char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        return c;
    }

    void skipWhitespace() noexcept {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else {
                break;
            }
        }
    }

    JsonParseResult makeError(const std::string& msg) {
        setError(msg);
        return JsonParseResult{std::nullopt, error_};
    }

    void setError(const std::string& msg) {
        if (!error_.has_value()) {
            error_ = JsonParseError{msg, line_, col_, pos_};
        }
    }

    std::optional<JsonValue> parseValue() {
        if (depth_ > 64) {
            setError("Maximum JSON nesting depth (64) exceeded");
            return std::nullopt;
        }
        skipWhitespace();
        if (eof()) {
            setError("Unexpected end of input while expecting a JSON value");
            return std::nullopt;
        }
        char c = peek();
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            auto s = parseString();
            if (!s.has_value()) {
                return std::nullopt;
            }
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.storage = std::move(*s);
            return v;
        }
        if (c == 't') {
            return parseLiteral("true", JsonValue::Type::Bool, true);
        }
        if (c == 'f') {
            return parseLiteral("false", JsonValue::Type::Bool, false);
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber();
        }

        std::ostringstream oss;
        oss << "Unexpected character '" << c << "' while parsing JSON value";
        setError(oss.str());
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull() {
        for (char expected : std::string("null")) {
            if (eof() || peek() != expected) {
                setError("Invalid literal (expected 'null')");
                return std::nullopt;
            }
            advance();
        }
        JsonValue v;
        v.type = JsonValue::Type::Null;
        v.storage = std::monostate{};
        return v;
    }

    std::optional<JsonValue> parseLiteral(const char* literal, JsonValue::Type type, bool bool_val) {
        for (const char* p = literal; *p != '\0'; ++p) {
            if (eof() || peek() != *p) {
                setError(std::string("Invalid literal (expected '") + literal + "')");
                return std::nullopt;
            }
            advance();
        }
        JsonValue v;
        v.type = type;
        v.storage = bool_val;
        return v;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    std::optional<std::uint32_t> parseHex4() {
        std::uint32_t val = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                setError("Incomplete \\uXXXX hex escape in JSON string");
                return std::nullopt;
            }
            char c = advance();
            val <<= 4;
            if (c >= '0' && c <= '9') {
                val |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val |= static_cast<std::uint32_t>(10 + (c - 'a'));
            } else if (c >= 'A' && c <= 'F') {
                val |= static_cast<std::uint32_t>(10 + (c - 'A'));
            } else {
                setError("Invalid hex digit in \\uXXXX escape");
                return std::nullopt;
            }
        }
        return val;
    }

    std::optional<std::string> parseString() {
        if (eof() || peek() != '"') {
            setError("Expected '\"' at start of JSON string");
            return std::nullopt;
        }
        advance(); // consume opening quote

        std::string result;
        while (!eof()) {
            char c = advance();
            if (c == '"') {
                return result;
            }
            if (static_cast<unsigned char>(c) < 0x20u) {
                setError("Unescaped control character inside JSON string");
                return std::nullopt;
            }
            if (c == '\\') {
                if (eof()) {
                    setError("Unterminated escape sequence in JSON string");
                    return std::nullopt;
                }
                char esc = advance();
                switch (esc) {
                    case '"':  result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/':  result.push_back('/'); break;
                    case 'b':  result.push_back('\b'); break;
                    case 'f':  result.push_back('\f'); break;
                    case 'n':  result.push_back('\n'); break;
                    case 'r':  result.push_back('\r'); break;
                    case 't':  result.push_back('\t'); break;
                    case 'u': {
                        auto cp = parseHex4();
                        if (!cp.has_value()) {
                            return std::nullopt;
                        }
                        appendUtf8(result, *cp);
                        break;
                    }
                    default:
                        setError(std::string("Invalid escape sequence '\\") + esc + "' in JSON string");
                        return std::nullopt;
                }
            } else {
                result.push_back(c);
            }
        }

        setError("Unterminated JSON string literal");
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        std::size_t start = pos_;
        bool is_float = false;

        if (peek() == '-') {
            advance();
        }
        if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            setError("Invalid JSON number: expected digit after sign");
            return std::nullopt;
        }
        if (peek() == '0') {
            advance();
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                setError("Leading zeros are not permitted in JSON numbers");
                return std::nullopt;
            }
        } else {
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        if (!eof() && peek() == '.') {
            is_float = true;
            advance();
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                setError("Invalid JSON number: expected digit after decimal point");
                return std::nullopt;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            advance();
            if (!eof() && (peek() == '+' || peek() == '-')) {
                advance();
            }
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                setError("Invalid JSON number: expected digit in exponent");
                return std::nullopt;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        std::string token = src_.substr(start, pos_ - start);
        if (!is_float) {
            try {
                std::size_t idx = 0;
                long long val = std::stoll(token, &idx, 10);
                if (idx == token.size()) {
                    JsonValue v;
                    v.type = JsonValue::Type::Integer;
                    v.storage = static_cast<std::int64_t>(val);
                    return v;
                }
            } catch (...) {
                is_float = true;
            }
        }

        char* end_ptr = nullptr;
        double dval = std::strtod(token.c_str(), &end_ptr);
        if (end_ptr != token.c_str() + token.size() || !std::isfinite(dval)) {
            setError("Invalid or non-finite JSON number '" + token + "'");
            return std::nullopt;
        }
        JsonValue v;
        v.type = JsonValue::Type::Double;
        v.storage = dval;
        return v;
    }

    std::optional<JsonValue> parseArray() {
        ++depth_;
        advance(); // consume '['
        skipWhitespace();

        JsonValue::Array arr;
        if (!eof() && peek() == ']') {
            advance();
            --depth_;
            JsonValue v;
            v.type = JsonValue::Type::Array;
            v.storage = std::move(arr);
            return v;
        }

        while (true) {
            skipWhitespace();
            auto elem = parseValue();
            if (!elem.has_value()) {
                return std::nullopt;
            }
            arr.push_back(std::move(*elem));
            skipWhitespace();
            if (eof()) {
                setError("Unterminated JSON array (expected ',' or ']')");
                return std::nullopt;
            }
            char c = peek();
            if (c == ',') {
                advance();
                skipWhitespace();
                if (!eof() && peek() == ']') {
                    setError("Trailing comma is not allowed in JSON array");
                    return std::nullopt;
                }
            } else if (c == ']') {
                advance();
                break;
            } else {
                setError(std::string("Expected ',' or ']' in JSON array, found '") + c + "'");
                return std::nullopt;
            }
        }

        --depth_;
        JsonValue v;
        v.type = JsonValue::Type::Array;
        v.storage = std::move(arr);
        return v;
    }

    std::optional<JsonValue> parseObject() {
        ++depth_;
        advance(); // consume '{'
        skipWhitespace();

        JsonValue::Object obj;
        if (!eof() && peek() == '}') {
            advance();
            --depth_;
            JsonValue v;
            v.type = JsonValue::Type::Object;
            v.storage = std::move(obj);
            return v;
        }

        while (true) {
            skipWhitespace();
            if (eof() || peek() != '"') {
                setError("Expected string key in JSON object");
                return std::nullopt;
            }
            auto key = parseString();
            if (!key.has_value()) {
                return std::nullopt;
            }
            if (obj.find(*key) != obj.end()) {
                setError("Duplicate key '" + *key + "' in JSON object");
                return std::nullopt;
            }
            skipWhitespace();
            if (eof() || peek() != ':') {
                setError("Expected ':' after object key '" + *key + "'");
                return std::nullopt;
            }
            advance(); // consume ':'
            skipWhitespace();
            auto val = parseValue();
            if (!val.has_value()) {
                return std::nullopt;
            }
            obj.emplace(std::move(*key), std::move(*val));
            skipWhitespace();
            if (eof()) {
                setError("Unterminated JSON object (expected ',' or '}')");
                return std::nullopt;
            }
            char c = peek();
            if (c == ',') {
                advance();
                skipWhitespace();
                if (!eof() && peek() == '}') {
                    setError("Trailing comma is not allowed in JSON object");
                    return std::nullopt;
                }
            } else if (c == '}') {
                advance();
                break;
            } else {
                setError(std::string("Expected ',' or '}' in JSON object, found '") + c + "'");
                return std::nullopt;
            }
        }

        --depth_;
        JsonValue v;
        v.type = JsonValue::Type::Object;
        v.storage = std::move(obj);
        return v;
    }

    const std::string& src_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t col_ = 1;
    int depth_ = 0;
    std::optional<JsonParseError> error_;
};

} // namespace

JsonParseResult parseJsonDocument(const std::string& input) {
    Parser p(input);
    return p.parse();
}

} // namespace detail
} // namespace data_access
} // namespace f1_sim