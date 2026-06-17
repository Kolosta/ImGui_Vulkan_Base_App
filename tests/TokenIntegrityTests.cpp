// ─────────────────────────────────────────────────────────────────────────────
//  Token integrity tests (runtime safety net).
//
//  Structural correctness — completeness, type/level validity, acyclicity —
//  is already PROVEN at compile time by the static_asserts in TokenSchema.cpp;
//  if those fail the library does not even build. This executable is the
//  complementary *value-level* check the spec asked for: it materialises the
//  registry exactly as the app does and verifies that every token actually
//  resolves to a concrete value on every theme, with the type the schema
//  declares (i.e. nothing throws "Token not found" / "is not a color" at
//  runtime, including through reference chains).
//
//  No test framework: returns non-zero on first failure, prints a summary.
//  Run it via the `ds_token_tests` target.
// ─────────────────────────────────────────────────────────────────────────────

#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Tokens/TokenSchema.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <DesignSystem/DesignSystem.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace DesignSystem;

namespace {

int g_failures = 0;

void Fail(const std::string& msg) {
    std::printf("  [FAIL] %s\n", msg.c_str());
    ++g_failures;
}

const char* ThemeName(ThemeType t) { return ThemeTypeToString(t).c_str(); }

} // namespace

int main() {
    std::printf("== Design System token integrity ==\n");

    // 1. Build the registry the same way the application does.
    TokenRegistry::Instance().InitializeDefaultTokens();
    auto all = TokenRegistry::Instance().GetAllTokens();

    std::printf("Registered %zu tokens (schema declares %zu).\n",
                all.size(), kTokenCount);

    // 2. Every schema row must have produced exactly one registered token.
    if (all.size() != kTokenCount)
        Fail("registered token count != schema size");

    for (const TokenDef& d : kTokenSchema) {
        std::string id = TokIdStr(d.id);
        auto tok = TokenRegistry::Instance().GetToken(id);
        if (!tok) { Fail("missing token: " + id); continue; }
        if (tok->GetLevel() != d.level)
            Fail("level mismatch for " + id);
        if (tok->GetValueType() != d.type)
            Fail("value-type mismatch for " + id);
    }

    // 3. Every token resolves to a concrete value on every theme, and that
    //    value has the type the schema declares for it (references resolve
    //    through to their terminal value).
    //    NB: `DesignSystem::DesignSystem` — namespace then class (same name).
    auto& ds = DesignSystem::DesignSystem::Instance();
    const ThemeType themes[] = { ThemeType::Dark, ThemeType::Light,
                                 ThemeType::MutedGreen, ThemeType::HighContrast };

    for (const TokenDef& d : kTokenSchema) {
        std::string id = TokIdStr(d.id);
        for (ThemeType th : themes) {
            try {
                TokenValue v = ds.ResolveTokenValue(id, th);
                // Whatever the token's declared kind, resolution must end at a
                // concrete value — never still a Reference.
                if (v.GetType() == ValueType::Reference) {
                    Fail(id + " still resolves to a Reference on theme " +
                         std::string(ThemeTypeToString(th)));
                }
                // For a concrete-typed token the resolved type must match
                // exactly. For a Reference token the resolved type is the
                // terminal target's type (Color/Float/…) by design, so only
                // the "not still a Reference" check above applies.
                else if (d.type != ValueType::Reference &&
                         v.GetType() != d.type) {
                    Fail(id + " resolves to " +
                         ValueTypeToString(v.GetType()) +
                         " but schema declares " +
                         ValueTypeToString(d.type) + " (theme " +
                         std::string(ThemeTypeToString(th)) + ")");
                }
            } catch (const std::exception& e) {
                Fail(id + " threw on theme " +
                     std::string(ThemeTypeToString(th)) + ": " + e.what());
            }
        }
    }

    // 4. Constraints (if any) must accept the token's own default scalar —
    //    a schema author can't ship a default that its constraint rejects.
    //    We read the *materialised* ValueConstraint off the registered token
    //    (the ConstraintSpec in the schema is just a constexpr description).
    for (const TokenDef& d : kTokenSchema) {
        auto tok = TokenRegistry::Instance().GetToken(TokIdStr(d.id));
        if (!tok || !tok->HasConstraint()) continue;
        const ValueConstraint& c = tok->GetConstraint();
        if (d.type == ValueType::Float && !c.Accepts(d.scalar))
            Fail(TokIdStr(d.id) + ": default float rejected by its constraint");
        if (d.type == ValueType::Int && !c.Accepts((double)d.integer))
            Fail(TokIdStr(d.id) + ": default int rejected by its constraint");
    }

    if (g_failures == 0) {
        std::printf("All checks passed (%zu tokens x 4 themes).\n", kTokenCount);
        return 0;
    }
    std::printf("\n%d failure(s).\n", g_failures);
    return 1;
}
