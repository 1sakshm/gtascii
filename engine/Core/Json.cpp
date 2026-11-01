#include "Core/Json.h"

#include "Core/Utf8.h"

#include <cstdlib>

namespace av {

bool JsonValue::asBool() const
{
    if (!isBool()) {
        throw std::runtime_error("JSON value is not a boolean");
    }
    return std::get<bool>(data_);
}

double JsonValue::asNumber() const
{
    if (!isNumber()) {
        throw std::runtime_error("JSON value is not a number");
    }
    return std::get<double>(data_);
}

const std::string& JsonValue::asString() const
{
    if (!isString()) {
        throw std::runtime_error("JSON value is not a string");
    }
    return std::get<std::string>(data_);
}

const JsonValue::Array& JsonValue::asArray() const
{
    if (!isArray()) {
        throw std::runtime_error("JSON value is not an array");
    }
    return *std::get<std::shared_ptr<Array>>(data_);
}

const JsonValue::Object& JsonValue::asObject() const
{
    if (!isObject()) {
        throw std::runtime_error("JSON value is not an object");
    }
    return *std::get<std::shared_ptr<Object>>(data_);
}

const JsonValue* JsonValue::find(std::string_view key) const
{
    if (!isObject()) {
        return nullptr;
    }
    const Object& obj = asObject();
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

double JsonValue::numberOr(std::string_view key, double fallback) const
{
    const JsonValue* v = find(key);
    return v && v->isNumber() ? v->asNumber() : fallback;
}

bool JsonValue::boolOr(std::string_view key, bool fallback) const
{
    const JsonValue* v = find(key);
    return v && v->isBool() ? v->asBool() : fallback;
}

std::string JsonValue::stringOr(std::string_view key, const std::string& fallback) const
{
    const JsonValue* v = find(key);
    return v && v->isString() ? v->asString() : fallback;
}

std::size_t JsonValue::size() const
{
    if (isArray()) {
        return asArray().size();
    }
    if (isObject()) {
        return asObject().size();
    }
    return 0;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonValue parseDocument()
    {
        skipWhitespace();
        JsonValue v = parseValue(0);
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters");
        }
        return v;
    }

private:
    static constexpr int kMaxDepth = 256;

    [[noreturn]] void fail(const std::string& message) const
    {
        int line = 1, column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw JsonError(message, line, column);
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipWhitespace()
    {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (!atEnd() && text_[pos_] != '\n') {
                    ++pos_;
                }
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                const std::size_t end = text_.find("*/", pos_ + 2);
                if (end == std::string_view::npos) {
                    fail("unterminated comment");
                }
                pos_ = end + 2;
            } else {
                break;
            }
        }
    }

    void expect(char c)
    {
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos_;
    }

    bool consumeLiteral(std::string_view lit)
    {
        if (text_.substr(pos_, lit.size()) == lit) {
            pos_ += lit.size();
            return true;
        }
        return false;
    }

    JsonValue parseValue(int depth)
    {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
        }
        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': return JsonValue(parseString());
        case 't':
            if (consumeLiteral("true")) return JsonValue(true);
            break;
        case 'f':
            if (consumeLiteral("false")) return JsonValue(false);
            break;
        case 'n':
            if (consumeLiteral("null")) return JsonValue(nullptr);
            break;
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                return JsonValue(parseNumber());
            }
            break;
        }
        if (atEnd()) {
            fail("unexpected end of input");
        }
        fail("unexpected character");
    }

    JsonValue parseObject(int depth)
    {
        expect('{');
        JsonValue::Object obj;
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            return JsonValue(std::move(obj));
        }
        for (;;) {
            skipWhitespace();
            if (peek() != '"') {
                fail("expected string key");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            obj.insert_or_assign(std::move(key), parseValue(depth + 1));
            skipWhitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            return JsonValue(std::move(obj));
        }
    }

    JsonValue parseArray(int depth)
    {
        expect('[');
        JsonValue::Array arr;
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            return JsonValue(std::move(arr));
        }
        for (;;) {
            skipWhitespace();
            arr.push_back(parseValue(depth + 1));
            skipWhitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            return JsonValue(std::move(arr));
        }
    }

    unsigned parseHex4()
    {
        if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
        }
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid hex digit in \\u escape");
        }
        return v;
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        for (;;) {
            if (atEnd()) {
                fail("unterminated string");
            }
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (atEnd()) {
                fail("unterminated escape");
            }
            const char e = text_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = parseHex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (!consumeLiteral("\\u")) {
                        fail("unpaired surrogate");
                    }
                    const unsigned lo = parseHex4();
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        fail("invalid low surrogate");
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    fail("unpaired surrogate");
                }
                appendUtf8(out, static_cast<char32_t>(cp));
                break;
            }
            default: fail("invalid escape");
            }
        }
    }

    double parseNumber()
    {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            fail("invalid number");
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string token(text_.substr(start, pos_ - start));
        return std::strtod(token.c_str(), nullptr);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

JsonValue parseJson(std::string_view text)
{
    return Parser(text).parseDocument();
}

} // namespace av
