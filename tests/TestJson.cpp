#include "TestHarness.h"

#include "Core/Json.h"

using av::JsonValue;
using av::parseJson;

TEST_CASE("Json: scalars and nesting")
{
    const JsonValue v = parseJson(R"({"a": 1.5, "b": [true, false, null], "c": {"d": "hi"}, "e": -2e3})");
    CHECK(v.isObject());
    CHECK_EQ(v.numberOr("a", 0), 1.5);
    CHECK_EQ(v.numberOr("e", 0), -2000.0);
    CHECK_EQ(v.find("b")->size(), std::size_t{3});
    CHECK((*v.find("b"))[0].asBool());
    CHECK((*v.find("b"))[2].isNull());
    CHECK_EQ(v.find("c")->stringOr("d", ""), std::string("hi"));
    CHECK(v.find("missing") == nullptr);
    CHECK_EQ(v.numberOr("c", 7.0), 7.0); // wrong type -> fallback
}

TEST_CASE("Json: string escapes and unicode")
{
    const JsonValue v = parseJson(R"(["a\"b\\c\n", "\u00e9", "\ud83d\ude00"])");
    CHECK_EQ(v[0].asString(), std::string("a\"b\\c\n"));
    CHECK_EQ(v[1].asString(), std::string("\xC3\xA9"));
    CHECK_EQ(v[2].asString(), std::string("\xF0\x9F\x98\x80"));
}

TEST_CASE("Json: comments are allowed")
{
    const JsonValue v = parseJson("// header\n{ /* inline */ \"x\": 1 // trailing\n}");
    CHECK_EQ(v.numberOr("x", 0), 1.0);
}

TEST_CASE("Json: errors report line numbers")
{
    auto failsAt = [](const char* text, int line) {
        try {
            parseJson(text);
        } catch (const av::JsonError& e) {
            return e.line() == line;
        }
        return false;
    };
    CHECK(failsAt("{\n\"a\": ,\n}", 2));
    CHECK(failsAt("[1, 2", 1));
    CHECK(failsAt("{\"a\": 1} x", 1));
    CHECK(failsAt("\"unterminated", 1));
    CHECK(failsAt("01", 1));
    CHECK(failsAt("{\"a\" 1}", 1));
    CHECK(failsAt("[\"\\ud800\"]", 1));
}

TEST_CASE("Json: typed access throws on mismatch")
{
    const JsonValue v = parseJson("[1]");
    bool threw = false;
    try {
        (void)v.asObject();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(JsonValue("text").isString()); // not converted to bool
}
