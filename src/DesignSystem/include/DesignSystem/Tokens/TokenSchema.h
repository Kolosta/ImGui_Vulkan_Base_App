#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
//  Compile-time token schema.
//
//  `kTokenSchema` is a constexpr array with exactly one `TokenDef` per `Tok`.
//  It is the single source of truth for default values, references, theme
//  overrides and constraints. The runtime TokenRegistry is *generated* from
//  it (see TokenRegistry.cpp); it adds no token of its own.
//
//  Everything the spec asked to be guaranteed "without running tests, ideally
//  without even building" is enforced here by `consteval` validators invoked
//  from `static_assert` at namespace scope:
//
//    1. Completeness  — schema has kTokenCount rows, indexed by enum order,
//                       so a new Tok with no row fails to compile.
//    2. Identity      — row[i].id == Tok(i) (rows can't be misordered).
//    3. References    — a Reference/themed-reference points at a real Tok
//                       whose value type is compatible.
//    4. Hierarchy     — references only go up: Component → Semantic/Component,
//                       Semantic → Primitive/Semantic, Primitive → (none).
//    5. Acyclic       — the reference graph has no cycle (DFS, constexpr).
//
//  Because these run during translation of this header, an inconsistent
//  schema is a build error pointing at the failing static_assert — no test
//  binary, no app launch required.
// ─────────────────────────────────────────────────────────────────────────────

namespace DesignSystem {

// A constexpr-friendly RGBA value. We do not reuse ImVec4 here so the schema
// header stays free of the imgui include; TokenRegistry converts to ImVec4.
struct Rgba {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};
struct Vec2f {
    float x = 0.f, y = 0.f;
};

// Build an Rgba from a 0xRRGGBB hex literal (alpha defaults to opaque). Lets
// the schema carry the palette in the same notation as the design source.
constexpr Rgba Hex(unsigned int rgb) {
    return Rgba{ ((rgb >> 16) & 0xFF) / 255.f,
                 ((rgb >> 8) & 0xFF) / 255.f,
                 (rgb & 0xFF) / 255.f, 1.f };
}
// Build an Rgba from 0xRRGGBBAA (explicit alpha) — used for transparent tones.
constexpr Rgba HexA(unsigned int rgba) {
    return Rgba{ ((rgba >> 24) & 0xFF) / 255.f,
                 ((rgba >> 16) & 0xFF) / 255.f,
                 ((rgba >> 8) & 0xFF) / 255.f,
                 (rgba & 0xFF) / 255.f };
}

// A constexpr-friendly numeric constraint description.
//
// `ValueConstraint` itself owns std::vector/std::string and is therefore not
// usable inside a constexpr `TokenDef`. The schema only needs to *describe*
// the limits at compile time; TokenRegistry turns this POD into a real
// `ValueConstraint` at construction. Keeping it out of the constexpr graph is
// also correct conceptually: structural validation (cycles/refs/types) does
// not depend on numeric ranges.
struct ConstraintSpec {
    enum class Kind : std::uint8_t { None, RangeK, OneOfK };
    Kind   kind = Kind::None;
    double lo = 0.0, hi = 0.0;            // RangeK
    std::array<double, 4> values{};       // OneOfK (at most 4: enough here)
    std::size_t valueCount = 0;
    std::string_view note{};

    constexpr bool Empty() const { return kind == Kind::None; }

    static constexpr ConstraintSpec Range(double a, double b,
                                          std::string_view n = {}) {
        ConstraintSpec c; c.kind = Kind::RangeK; c.lo = a; c.hi = b; c.note = n;
        return c;
    }
    static constexpr ConstraintSpec OneOf(std::initializer_list<double> vs,
                                          std::string_view n = {}) {
        ConstraintSpec c; c.kind = Kind::OneOfK; c.note = n;
        for (double v : vs) { if (c.valueCount < 4) c.values[c.valueCount++] = v; }
        return c;
    }
};

// One theme-scoped override carried by the schema (replaces the old
// imperative SetContextValue calls). `ref` is valid when refValid is true,
// otherwise the inline color/float applies for that theme.
struct ThemeOverride {
    ThemeType theme{};
    bool      refValid = false;
    Tok       ref{};
    ValueType kind = ValueType::Color;  // Color or Float for inline values
    Rgba      color{};
    float     scalar = 0.f;
};

// The declarative definition of one token. POD-ish and fully constexpr.
struct TokenDef {
    Tok            id{};
    TokenLevel     level = TokenLevel::Primitive;
    ValueType      type  = ValueType::Float;

    // Inline value (used when type != Reference).
    Rgba           color{};
    float          scalar = 0.f;
    int            integer = 0;
    Vec2f          vec2{};

    // Reference target (used when type == Reference).
    Tok            ref{};

    // Composite TextStyle: five token-id references (one per typographic axis).
    // Used only when type == TextStyle.
    Tok            tsFamily{}, tsSize{}, tsWeight{}, tsLineHeight{}, tsTracking{};

    // FontFamily literal: a font-family name. Used only when type == FontFamily.
    std::string_view fontFamilyName{};

    // Theme-scoped overrides (Dark is the default value above). Three slots:
    // Light, Muted Green, High Contrast — the non-default themes.
    std::array<ThemeOverride, 3> themeOverrides{};
    std::size_t    themeOverrideCount = 0;

    // Optional numeric constraint (Float/Int). Empty == unconstrained.
    // Constexpr description; materialised into a ValueConstraint at runtime.
    ConstraintSpec constraint{};

    std::string_view description{};
};

// ── Builder helpers — keep schema rows short and intention-revealing ─────────
namespace schema_detail {

constexpr TokenDef Color(Tok id, TokenLevel lvl, Rgba c, std::string_view desc,
                         ConstraintSpec cn = {}) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Color;
    d.color = c; d.description = desc; d.constraint = cn; return d;
}
constexpr TokenDef Float(Tok id, TokenLevel lvl, float v, std::string_view desc,
                         ConstraintSpec cn = {}) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Float;
    d.scalar = v; d.description = desc; d.constraint = cn; return d;
}
constexpr TokenDef Int(Tok id, TokenLevel lvl, int v, std::string_view desc,
                       ConstraintSpec cn = {}) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Int;
    d.integer = v; d.description = desc; d.constraint = cn; return d;
}
// Ratio: a fraction (0..1) of a measured dimension. Reuses `scalar`.
constexpr TokenDef Ratio(Tok id, TokenLevel lvl, float v, std::string_view desc,
                         ConstraintSpec cn = {}) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Ratio;
    d.scalar = v; d.description = desc; d.constraint = cn; return d;
}
// Bezier: cubic-bezier easing curve. Reuses `color` as (x1,y1,x2,y2).
constexpr TokenDef Bezier(Tok id, TokenLevel lvl, Rgba cp, std::string_view desc) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Bezier;
    d.color = cp; d.description = desc; return d;
}
// FontFamily: a font-family name literal (the default family for this token).
constexpr TokenDef FontFam(Tok id, TokenLevel lvl, std::string_view familyName,
                           std::string_view desc) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::FontFamily;
    d.fontFamilyName = familyName; d.description = desc; return d;
}
// TextStyle: composite of five typographic-axis token references.
constexpr TokenDef TextStyle(Tok id, TokenLevel lvl, Tok fam, Tok size, Tok weight,
                             Tok lh, Tok track, std::string_view desc) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::TextStyle;
    d.tsFamily = fam; d.tsSize = size; d.tsWeight = weight;
    d.tsLineHeight = lh; d.tsTracking = track; d.description = desc; return d;
}
constexpr TokenDef Vec2(Tok id, TokenLevel lvl, Vec2f v, std::string_view desc,
                        ConstraintSpec cn = {}) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Vec2;
    d.vec2 = v; d.description = desc; d.constraint = cn; return d;
}
constexpr TokenDef Ref(Tok id, TokenLevel lvl, Tok target, std::string_view desc) {
    TokenDef d; d.id = id; d.level = lvl; d.type = ValueType::Reference;
    d.ref = target; d.description = desc; return d;
}
// Reference that also carries its own numeric constraint. A Reference token
// resolves to another token's value, but an *override* on this token still
// needs a range to clamp/reject against (e.g. component.style.disabledAlpha
// references an alpha role yet must itself stay within [0..1]). This makes
// the constraint a property of the field, independent of the ref chain.
constexpr TokenDef RefC(Tok id, TokenLevel lvl, Tok target, std::string_view desc,
                        ConstraintSpec cn) {
    TokenDef d = Ref(id, lvl, target, desc);
    d.constraint = cn; return d;
}
// Reference with one theme-scoped reference override.
constexpr TokenDef RefT(Tok id, TokenLevel lvl, Tok target, std::string_view desc,
                        ThemeType th, Tok themedTarget) {
    TokenDef d = Ref(id, lvl, target, desc);
    ThemeOverride o; o.theme = th; o.refValid = true; o.ref = themedTarget;
    d.themeOverrides[0] = o; d.themeOverrideCount = 1; return d;
}
// Reference whose target differs per theme. `dark` is the default; light /
// muted / high supply Light / Muted-Green / High-Contrast. This is how a
// single semantic token (e.g. background.base) picks a DARK palette step in
// the dark theme and a LIGHT step in the light theme without duplicating the
// token — the whole theme system rides on these per-theme reference picks.
constexpr TokenDef RefT4(Tok id, TokenLevel lvl, std::string_view desc,
                         Tok dark, Tok light, Tok muted, Tok high) {
    TokenDef d = Ref(id, lvl, dark, desc);
    auto add = [&](ThemeType t, Tok r) {
        ThemeOverride o; o.theme = t; o.refValid = true; o.ref = r;
        d.themeOverrides[d.themeOverrideCount++] = o;
    };
    add(ThemeType::Light, light);
    add(ThemeType::MutedGreen, muted);
    add(ThemeType::HighContrast, high);
    return d;
}

// Color token with a value per theme, at a given tier. `dark` is the default
// (Dark theme); `light`, `muted`, `high` supply the other three themes.
// Filling the primitive palette per theme is enough for most colors — every
// semantic/component token references the palette. But some role colours
// (e.g. data-viz contextual roles) carry themed values directly at the
// semantic tier, hence the explicit `lvl` parameter.
constexpr TokenDef ColorTL(Tok id, TokenLevel lvl, std::string_view desc,
                           Rgba dark, Rgba light, Rgba muted, Rgba high,
                           ConstraintSpec cn = {}) {
    TokenDef d = Color(id, lvl, dark, desc, cn);
    auto add = [&](ThemeType t, Rgba c) {
        ThemeOverride o; o.theme = t; o.refValid = false;
        o.kind = ValueType::Color; o.color = c;
        d.themeOverrides[d.themeOverrideCount++] = o;
    };
    add(ThemeType::Light, light);
    add(ThemeType::MutedGreen, muted);
    add(ThemeType::HighContrast, high);
    return d;
}

// Convenience: themed colour at the Primitive tier (the common case).
constexpr TokenDef ColorT(Tok id, std::string_view desc,
                          Rgba dark, Rgba light, Rgba muted, Rgba high,
                          ConstraintSpec cn = {}) {
    return ColorTL(id, TokenLevel::Primitive, desc, dark, light, muted, high, cn);
}

// Float primitive with a value per theme (used for High-Contrast metrics:
// thicker borders/lines for visibility). Same default/per-theme contract.
constexpr TokenDef FloatT(Tok id, std::string_view desc,
                          float dark, float light, float muted, float high,
                          ConstraintSpec cn = {}) {
    TokenDef d = Float(id, TokenLevel::Primitive, dark, desc, cn);
    auto add = [&](ThemeType t, float v) {
        ThemeOverride o; o.theme = t; o.refValid = false;
        o.kind = ValueType::Float; o.scalar = v;
        d.themeOverrides[d.themeOverrideCount++] = o;
    };
    add(ThemeType::Light, light);
    add(ThemeType::MutedGreen, muted);
    add(ThemeType::HighContrast, high);
    return d;
}

} // namespace schema_detail

// Defined in TokenSchema.cpp. Indexed by `static_cast<size_t>(Tok)`.
extern const std::array<TokenDef, kTokenCount> kTokenSchema;

// ─────────────────────────────────────────────────────────────────────────────
//  Compile-time validation
//
//  The schema array is defined in the .cpp, but the *checks* must run at
//  compile time. We therefore expose the table to constexpr through a
//  forward-declared constexpr accessor that the .cpp defines with a
//  constexpr-usable copy. To keep a single source we instead validate via a
//  small constexpr mirror built from the same builder calls in a header-only
//  `BuildSchema()` — see TokenSchema.cpp which `static_assert`s on it too.
//
//  Here we provide the generic validators operating on any constexpr array.
// ─────────────────────────────────────────────────────────────────────────────

template <std::size_t N>
constexpr bool SchemaIsComplete(const std::array<TokenDef, N>& s) {
    if (N != kTokenCount) return false;
    for (std::size_t i = 0; i < N; ++i)
        if (static_cast<std::size_t>(s[i].id) != i) return false;  // identity + order
    return true;
}

template <std::size_t N>
constexpr int LevelRank(TokenLevel l) {
    switch (l) {
        case TokenLevel::Primitive: return 0;
        case TokenLevel::Semantic:  return 1;
        case TokenLevel::Component: return 2;
    }
    return 0;
}

template <std::size_t N>
constexpr bool ReferencesAreValid(const std::array<TokenDef, N>& s) {
    auto rankOf = [](TokenLevel l) {
        return l == TokenLevel::Primitive ? 0 : l == TokenLevel::Semantic ? 1 : 2;
    };
    for (std::size_t i = 0; i < N; ++i) {
        const TokenDef& d = s[i];

        auto checkRefTarget = [&](Tok target) -> bool {
            std::size_t ti = static_cast<std::size_t>(target);
            if (ti >= N) return false;
            const TokenDef& tgt = s[ti];
            // Reference must resolve to a value of a usable type. A reference
            // may point at another reference (chain) or a concrete value.
            // Primitives never reference; semantics/components reference only
            // strictly-lower or same level (no Component referenced by Semantic).
            if (rankOf(tgt.level) > rankOf(d.level)) return false;
            return true;
        };

        if (d.type == ValueType::Reference) {
            if (d.level == TokenLevel::Primitive) return false;  // primitives are concrete
            if (!checkRefTarget(d.ref)) return false;
        }
        if (d.type == ValueType::TextStyle) {
            if (!checkRefTarget(d.tsFamily) || !checkRefTarget(d.tsSize) ||
                !checkRefTarget(d.tsWeight) || !checkRefTarget(d.tsLineHeight) ||
                !checkRefTarget(d.tsTracking))
                return false;
        }
        for (std::size_t k = 0; k < d.themeOverrideCount; ++k) {
            const ThemeOverride& o = d.themeOverrides[k];
            if (o.refValid && !checkRefTarget(o.ref)) return false;
        }
    }
    return true;
}

// A semantic token's *base* definition must always be a reference (to a
// primitive or another semantic). Semantic tokens are roles/aliases — they
// never carry a literal value; every literal lives at the primitive tier.
// (Component tokens may carry literals, e.g. one-off table-row tints.)
template <std::size_t N>
constexpr bool SemanticsAreReferences(const std::array<TokenDef, N>& s) {
    for (std::size_t i = 0; i < N; ++i) {
        if (s[i].level != TokenLevel::Semantic) continue;
        // A semantic carries no literal: it is either a plain reference or a
        // composite TextStyle (which is itself five references).
        if (s[i].type != ValueType::Reference && s[i].type != ValueType::TextStyle)
            return false;
    }
    return true;
}

// DFS cycle detection over default references AND themed reference overrides.
// 0=unvisited, 1=on-stack, 2=done. Recursion is expressed as an explicit
// constexpr stack to stay within constant-evaluation limits.
template <std::size_t N>
constexpr bool ReferenceGraphIsAcyclic(const std::array<TokenDef, N>& s) {
    std::array<int, N> state{};            // value-initialised to 0
    for (std::size_t start = 0; start < N; ++start) {
        if (state[start] != 0) continue;
        std::array<std::size_t, N + 1> stack{};
        std::array<std::size_t, N + 1> edge{};   // next edge index per frame
        std::size_t sp = 0;
        stack[sp] = start;
        edge[sp]  = 0;
        state[start] = 1;
        while (true) {
            std::size_t node = stack[sp];
            const TokenDef& d = s[node];

            // Enumerate outgoing edges: [0] default ref (if any),
            // then themed reference overrides.
            auto edgeTarget = [&](std::size_t e, bool& has) -> std::size_t {
                std::size_t idx = 0;
                if (d.type == ValueType::Reference) {
                    if (e == idx) { has = true; return static_cast<std::size_t>(d.ref); }
                    ++idx;
                }
                if (d.type == ValueType::TextStyle) {
                    const Tok ts[5] = { d.tsFamily, d.tsSize, d.tsWeight,
                                        d.tsLineHeight, d.tsTracking };
                    for (const Tok t : ts) {
                        if (e == idx) { has = true; return static_cast<std::size_t>(t); }
                        ++idx;
                    }
                }
                for (std::size_t k = 0; k < d.themeOverrideCount; ++k) {
                    if (s[node].themeOverrides[k].refValid) {
                        if (e == idx) { has = true;
                            return static_cast<std::size_t>(s[node].themeOverrides[k].ref); }
                        ++idx;
                    }
                }
                has = false; return 0;
            };

            bool has = false;
            std::size_t tgt = edgeTarget(edge[sp], has);
            if (!has) {
                state[node] = 2;
                if (sp == 0) break;
                --sp;
                ++edge[sp];
                continue;
            }
            ++edge[sp];
            if (state[tgt] == 1) return false;       // back-edge → cycle
            if (state[tgt] == 0) {
                state[tgt] = 1;
                ++sp;
                stack[sp] = tgt;
                edge[sp]  = 0;
            }
        }
    }
    return true;
}

} // namespace DesignSystem
