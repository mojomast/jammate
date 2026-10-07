// Minimal, dependency-free JSON reader/writer for the rhythm evaluation tool.
//
// Why hand-written instead of nlohmann/json: nlohmann exists inside the NAM
// submodule, but pulling it into a stand-alone evaluation tool would drag a
// third-party licence surface into a tool that is otherwise pure standard C++
// and would make the tool non-buildable without the submodule. SPEC.md 7.1 only
// forbids JSON parsing on the audio thread; this file is test/tool code and
// never runs on any audio path. The reader is deliberately strict: malformed
// input raises std::runtime_error rather than silently producing a default,
// because a manifest that half-parses would make EVAL-002 score a corpus that
// is not the one on disk.
//
// Header-only on purpose. Both the CLI and the jam-core test binary consume it,
// and jam-core/CMakeLists.txt is frozen for this task, so there is no place to
// add a compiled library for the tests. Every function is `inline`.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rhythmjson
{

class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    static Value makeNull() { return Value(); }

    static Value makeBool (bool b)
    {
        Value v;
        v.type_ = Type::Bool;
        v.boolean_ = b;
        return v;
    }

    static Value makeNumber (double d)
    {
        Value v;
        v.type_ = Type::Number;
        v.number_ = d;
        return v;
    }

    static Value makeString (std::string s)
    {
        Value v;
        v.type_ = Type::String;
        v.text_ = std::move (s);
        return v;
    }

    static Value makeArray()
    {
        Value v;
        v.type_ = Type::Array;
        return v;
    }

    static Value makeObject()
    {
        Value v;
        v.type_ = Type::Object;
        return v;
    }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool boolean() const { return boolean_; }
    double number() const { return number_; }
    const std::string& text() const { return text_; }

    const std::vector<Value>& items() const { return items_; }
    const std::vector<std::pair<std::string, Value>>& members() const { return members_; }

    /** Appends an element; only valid on arrays. */
    void push (Value v) { items_.push_back (std::move (v)); }

    /** Sets a member, replacing any existing key so output never has duplicates. */
    void set (const std::string& key, Value v)
    {
        for (auto& m : members_)
        {
            if (m.first == key)
            {
                m.second = std::move (v);
                return;
            }
        }
        members_.emplace_back (key, std::move (v));
    }

    /** Returns the member with `key`, or nullptr. */
    const Value* find (const std::string& key) const
    {
        if (type_ != Type::Object)
            return nullptr;
        for (const auto& m : members_)
            if (m.first == key)
                return &m.second;
        return nullptr;
    }

    /** Number member by key, or `fallback` when absent / not a number. */
    double numberOr (const std::string& key, double fallback) const
    {
        const Value* v = find (key);
        return (v != nullptr && v->isNumber()) ? v->number_ : fallback;
    }

    /** String member by key, or `fallback` when absent / not a string. */
    std::string stringOr (const std::string& key, const std::string& fallback) const
    {
        const Value* v = find (key);
        return (v != nullptr && v->isString()) ? v->text_ : fallback;
    }

    bool boolOr (const std::string& key, bool fallback) const
    {
        const Value* v = find (key);
        return (v != nullptr && v->isBool()) ? v->boolean_ : fallback;
    }

    /** Array of numbers; non-numeric elements are rejected, not coerced. */
    std::vector<double> numberArray (const std::string& key) const
    {
        std::vector<double> out;
        const Value* v = find (key);
        if (v == nullptr || ! v->isArray())
            return out;
        out.reserve (v->items_.size());
        for (const auto& item : v->items_)
        {
            if (! item.isNumber())
                throw std::runtime_error ("array \"" + key + "\" contains a non-number");
            out.push_back (item.number_);
        }
        return out;
    }

    std::vector<std::string> stringArray (const std::string& key) const
    {
        std::vector<std::string> out;
        const Value* v = find (key);
        if (v == nullptr || ! v->isArray())
            return out;
        out.reserve (v->items_.size());
        for (const auto& item : v->items_)
        {
            if (! item.isString())
                throw std::runtime_error ("array \"" + key + "\" contains a non-string");
            out.push_back (item.text_);
        }
        return out;
    }

    /** Compact serialisation. Non-finite numbers become null so the output is
        always valid JSON even if a metric overflowed. */
    std::string dump() const
    {
        std::string out;
        dumpTo (out);
        return out;
    }

private:
    void dumpTo (std::string& out) const
    {
        switch (type_)
        {
            case Type::Null:   out += "null"; return;
            case Type::Bool:   out += boolean_ ? "true" : "false"; return;
            case Type::Number: out += formatNumber (number_); return;
            case Type::String: dumpString (out, text_); return;
            case Type::Array:
            {
                out += '[';
                for (std::size_t i = 0; i < items_.size(); ++i)
                {
                    if (i != 0) out += ',';
                    items_[i].dumpTo (out);
                }
                out += ']';
                return;
            }
            case Type::Object:
            {
                out += '{';
                for (std::size_t i = 0; i < members_.size(); ++i)
                {
                    if (i != 0) out += ',';
                    dumpString (out, members_[i].first);
                    out += ':';
                    members_[i].second.dumpTo (out);
                }
                out += '}';
                return;
            }
        }
    }

    static std::string formatNumber (double d)
    {
        if (! std::isfinite (d))
            return "null";
        char buf[40];
        std::snprintf (buf, sizeof buf, "%.9g", d);
        // Normalise negative zero, which is valid but noisy in diffs.
        if (buf[0] == '-' && buf[1] == '0' && buf[2] == '\0')
            return "0";
        return std::string (buf);
    }

    static void dumpString (std::string& out, const std::string& s)
    {
        out += '"';
        for (const char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char> (c) < 0x20u)
                    {
                        char buf[8];
                        std::snprintf (buf, sizeof buf, "\\u%04x",
                                       static_cast<unsigned> (static_cast<unsigned char> (c)));
                        out += buf;
                    }
                    else
                    {
                        out += c;
                    }
                    break;
            }
        }
        out += '"';
    }

    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string text_;
    std::vector<Value> items_;
    std::vector<std::pair<std::string, Value>> members_;
};

namespace detail
{

inline void appendUtf8 (std::string& out, unsigned int cp)
{
    if (cp < 0x80u)
    {
        out += static_cast<char> (cp);
    }
    else if (cp < 0x800u)
    {
        out += static_cast<char> (0xc0u | (cp >> 6));
        out += static_cast<char> (0x80u | (cp & 0x3fu));
    }
    else if (cp < 0x10000u)
    {
        out += static_cast<char> (0xe0u | (cp >> 12));
        out += static_cast<char> (0x80u | ((cp >> 6) & 0x3fu));
        out += static_cast<char> (0x80u | (cp & 0x3fu));
    }
    else
    {
        out += static_cast<char> (0xf0u | (cp >> 18));
        out += static_cast<char> (0x80u | ((cp >> 12) & 0x3fu));
        out += static_cast<char> (0x80u | ((cp >> 6) & 0x3fu));
        out += static_cast<char> (0x80u | (cp & 0x3fu));
    }
}

class Parser
{
public:
    explicit Parser (const std::string& source) : s_ (source) {}

    Value parse()
    {
        skipWhitespace();
        Value v = parseValue();
        skipWhitespace();
        if (i_ != s_.size())
            fail ("trailing content after the top-level value");
        return v;
    }

private:
    [[noreturn]] void fail (const std::string& what) const
    {
        throw std::runtime_error ("JSON parse error at byte " + std::to_string (i_)
                                  + ": " + what);
    }

    void skipWhitespace()
    {
        while (i_ < s_.size())
        {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++i_;
            else
                break;
        }
    }

    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }

    void expect (char c)
    {
        if (peek() != c)
            fail (std::string ("expected '") + c + "'");
        ++i_;
    }

    Value parseValue()
    {
        if (i_ >= s_.size())
            fail ("unexpected end of input");
        switch (s_[i_])
        {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value::makeString (parseString());
            case 't': expectLiteral ("true");  return Value::makeBool (true);
            case 'f': expectLiteral ("false"); return Value::makeBool (false);
            case 'n': expectLiteral ("null");  return Value::makeNull();
            default:  return parseNumber();
        }
    }

    void expectLiteral (const char* lit)
    {
        const std::string token (lit);
        if (s_.compare (i_, token.size(), token) != 0)
            fail ("bad literal");
        i_ += token.size();
    }

    Value parseObject()
    {
        Value v = Value::makeObject();
        expect ('{');
        skipWhitespace();
        if (peek() == '}')
        {
            ++i_;
            return v;
        }
        for (;;)
        {
            skipWhitespace();
            if (peek() != '"')
                fail ("object key must be a string");
            std::string key = parseString();
            skipWhitespace();
            expect (':');
            skipWhitespace();
            v.set (key, parseValue());
            skipWhitespace();
            if (peek() == ',')
            {
                ++i_;
                continue;
            }
            expect ('}');
            return v;
        }
    }

    Value parseArray()
    {
        Value v = Value::makeArray();
        expect ('[');
        skipWhitespace();
        if (peek() == ']')
        {
            ++i_;
            return v;
        }
        for (;;)
        {
            skipWhitespace();
            v.push (parseValue());
            skipWhitespace();
            if (peek() == ',')
            {
                ++i_;
                continue;
            }
            expect (']');
            return v;
        }
    }

    std::string parseString()
    {
        expect ('"');
        std::string out;
        while (i_ < s_.size())
        {
            const char c = s_[i_++];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                if (static_cast<unsigned char> (c) < 0x20u)
                    fail ("unescaped control character in string");
                out += c;
                continue;
            }
            if (i_ >= s_.size())
                break;
            const char e = s_[i_++];
            switch (e)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    if (i_ + 4 > s_.size())
                        fail ("truncated \\u escape");
                    unsigned int cp = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const char h = s_[i_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')      cp |= static_cast<unsigned> (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned> (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned> (h - 'A' + 10);
                        else fail ("bad hex digit in \\u escape");
                    }
                    appendUtf8 (out, cp);
                    break;
                }
                default: fail ("unknown escape");
            }
        }
        fail ("unterminated string");
    }

    Value parseNumber()
    {
        const std::size_t start = i_;
        if (peek() == '-')
            ++i_;
        if (i_ >= s_.size() || ! isDigit (s_[i_]))
            fail ("expected a value");

        while (i_ < s_.size() && isDigit (s_[i_]))
            ++i_;
        if (peek() == '.')
        {
            ++i_;
            if (i_ >= s_.size() || ! isDigit (s_[i_]))
                fail ("fraction has no digits");
            while (i_ < s_.size() && isDigit (s_[i_]))
                ++i_;
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++i_;
            if (peek() == '+' || peek() == '-')
                ++i_;
            if (i_ >= s_.size() || ! isDigit (s_[i_]))
                fail ("exponent has no digits");
            while (i_ < s_.size() && isDigit (s_[i_]))
                ++i_;
        }

        const std::string token = s_.substr (start, i_ - start);
        char* end = nullptr;
        const double value = std::strtod (token.c_str(), &end);
        if (end == nullptr || *end != '\0')
            fail ("malformed number");
        return Value::makeNumber (value);
    }

    static bool isDigit (char c) { return c >= '0' && c <= '9'; }

    const std::string& s_;
    std::size_t i_ = 0;
};

} // namespace detail

/** Parses `text` as JSON. Throws std::runtime_error on malformed input. */
inline Value parse (const std::string& text)
{
    detail::Parser parser (text);
    return parser.parse();
}

} // namespace rhythmjson
