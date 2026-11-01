#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace av {

class JsonError : public std::runtime_error {
public:
    JsonError(const std::string& message, int line, int column)
        : std::runtime_error(message + " (line " + std::to_string(line) + ", column " + std::to_string(column) + ")")
        , line_(line)
        , column_(column)
    {
    }
    int line() const noexcept { return line_; }
    int column() const noexcept { return column_; }

private:
    int line_;
    int column_;
};

// Immutable JSON document value. Objects keep keys sorted (std::map).
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;

    JsonValue() = default; // null
    JsonValue(std::nullptr_t) {}
    JsonValue(bool b) : data_(b) {}
    JsonValue(double n) : data_(n) {}
    JsonValue(std::string s) : data_(std::move(s)) {}
    JsonValue(const char* s) : data_(std::string(s)) {} // otherwise binds to bool
    JsonValue(Array a) : data_(std::make_shared<Array>(std::move(a))) {}
    JsonValue(Object o) : data_(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(data_); }
    bool isBool() const { return std::holds_alternative<bool>(data_); }
    bool isNumber() const { return std::holds_alternative<double>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(data_); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(data_); }

    // Typed access; throws std::runtime_error on type mismatch.
    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Object member lookup; nullptr if not an object or key missing.
    const JsonValue* find(std::string_view key) const;

    // Convenience with defaults (wrong type or missing -> fallback).
    double numberOr(std::string_view key, double fallback) const;
    bool boolOr(std::string_view key, bool fallback) const;
    std::string stringOr(std::string_view key, const std::string& fallback) const;

    std::size_t size() const; // array/object element count, else 0
    const JsonValue& operator[](std::size_t i) const { return asArray().at(i); }

private:
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data_;
};

// Parses a complete JSON document (RFC 8259; also accepts // and /* */ comments,
// which are convenient in hand-edited config files). Throws JsonError.
JsonValue parseJson(std::string_view text);

} // namespace av
