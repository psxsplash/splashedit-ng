// What a number field takes when typed into (#376): a number, or simple
// arithmetic on numbers, or arithmetic on the value already there. Kept apart
// from the widget (no ImGui) so editor_tests can check it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace ui {

// A typed entry that works on the field's own value: each value it applies
// to becomes value * scale + offset ("+15" is 1, 15; "*2" is 2, 0; "/4+1"
// is 0.25, 1). `on` is false for an entry that is just a value ("90+15").
struct Relative {
    bool on = false;
    double scale = 1, offset = 0;
};

namespace expr_detail {

// A value as a * current + b: the field's value enters a relative entry only
// once, as its first operand, so every step of one stays of this form.
struct Affine {
    double a = 0, b = 0;
};

struct Parser {
    const char* s;
    bool ok = true;

    void space() {
        while (*s == ' ' || *s == '\t') ++s;
    }
    Affine fail() {
        ok = false;
        return {};
    }
    // A number as strtod reads it (decimals, exponents), from a digit or a point only:
    // signs are the operators', and "inf", "nan" are no numbers here.
    Affine primary(int depth) {
        space();
        if (*s == '(') {
            ++s;
            Affine v = sum(nullptr, depth + 1);
            space();
            if (*s != ')') return fail();
            ++s;
            return v;
        }
        if (!(std::isdigit(static_cast<unsigned char>(*s)) || (*s == '.' && std::isdigit(static_cast<unsigned char>(s[1])))))
            return fail();
        char* end = nullptr;
        const double v = std::strtod(s, &end);
        if (!end || end == s) return fail();
        s = end;
        return {0, v};
    }
    Affine unary(int depth) {
        if (depth > 64) return fail();  // "-------...", "((((...": no number anyone types
        space();
        if (*s == '-' || *s == '+') {
            const bool neg = *s == '-';
            ++s;
            Affine v = unary(depth + 1);
            return neg ? Affine{-v.a, -v.b} : v;
        }
        return primary(depth);
    }
    Affine product(const Affine* first, int depth) {
        Affine v = first ? *first : unary(depth);
        for (;;) {
            if (!ok) return v;
            space();
            const char op = *s;
            if (op != '*' && op != '/') return v;
            ++s;
            const Affine r = unary(depth);
            if (!ok || r.a != 0) return fail();  // never current * current
            if (op == '*') v = {v.a * r.b, v.b * r.b};
            else if (r.b == 0) return fail();
            else v = {v.a / r.b, v.b / r.b};
        }
    }
    Affine sum(const Affine* first, int depth) {
        Affine v = product(first, depth);
        for (;;) {
            if (!ok) return v;
            space();
            const char op = *s;
            if (op != '+' && op != '-') return v;
            ++s;
            const Affine r = product(nullptr, depth);
            v = op == '+' ? Affine{v.a + r.a, v.b + r.b} : Affine{v.a - r.a, v.b - r.b};
        }
    }
};

}  // namespace expr_detail

// The value typed `text` stands for in a field holding `current`: a number
// ("-5", "0.25", "1e3"), or + - * / on numbers with the usual precedence,
// parentheses and spaces ("90+15", "2*(3+4)"). Starting with +, * or / it
// works on `current`, as if `current` were typed first: "+15" on 90 is 105,
// "*2" 180, "/2" 45, "*2+1" 181. A leading - is a negative number, not
// subtraction ("-5" is -5, as people type it); "+-5" takes 5 off. `rel`,
// when given, gets the relative entry as a scale and offset, so a field
// standing for several values can apply it to each. False, `out` and `rel`
// untouched, for anything else: empty, words, a half-typed "2+", division
// by zero, a result out of float's range.
inline bool evalNumber(const std::string& text, float current, float& out, Relative* rel = nullptr) {
    expr_detail::Parser p{text.c_str()};
    p.space();
    const bool relative = *p.s == '+' || *p.s == '*' || *p.s == '/';
    const expr_detail::Affine self{1, 0};
    const expr_detail::Affine v = p.sum(relative ? &self : nullptr, 0);
    p.space();
    if (!p.ok || *p.s != 0) return false;
    const double result = v.a * double(current) + v.b;
    if (!std::isfinite(result) || std::fabs(result) > 3.4e38 || !std::isfinite(v.a) || !std::isfinite(v.b)) return false;
    out = float(result);
    if (out == 0) out = 0;  // no "-0"
    if (rel) *rel = relative ? Relative{true, v.a, v.b} : Relative{};
    return true;
}

// A relative entry applied to `own`, one of the values a mixed field stands
// for (#263): clamped to [lo, hi] for each on its own; a whole-number field
// rounds it.
inline float relativeTo(float own, const Relative& r, float lo, float hi, bool integer) {
    double v = r.on ? double(own) * r.scale + r.offset : double(own);
    if (!std::isfinite(v)) v = own;
    const float c = std::clamp(float(std::clamp(v, -3.4e38, 3.4e38)), lo, hi);
    return integer ? std::round(c) : c;
}

}  // namespace ui
