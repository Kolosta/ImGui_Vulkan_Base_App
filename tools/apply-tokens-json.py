#!/usr/bin/env python3
"""Apply a Carto Token-Graph JSON export onto the REAL token system, by writing a
generated include that the registry applies at init time.

This is a dev tool: export a style from the Token Graph window, run this, rebuild
(F5), relaunch — the exported values/references become the system's actual token
values, without hand-editing every token.

How it works
------------
The exported JSON (format "carto-tokens-v1") describes one THEME LAYER:
  * isBaseLayer = true  -> the Dark / base layer (the token's default value/ref)
  * isBaseLayer = false -> a specific theme (Light / MutedGreen / HighContrast);
                           only tokens that REDEFINE the layer are present, the
                           rest keep inheriting the base.

For each token we emit a `SetBase*` / `SetTheme*` call into a per-layer section
of src/DesignSystem/src/Tokens/TokenStyle.generated.inc. TokenRegistry includes
that file at the end of initialisation, so the calls run on every launch and the
values are the system's real ones (NOT runtime OverrideManager overrides).

Running base mode replaces the BASE section; running a theme mode replaces just
that theme's section. Sections are delimited by markers so applying one layer
never disturbs the others.

Usage:
    python tools/apply-tokens-json.py style.tokens.json            # dry run
    python tools/apply-tokens-json.py style.tokens.json --write    # apply
"""

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INC = REPO / "src/DesignSystem/src/Tokens/TokenStyle.generated.inc"

THEME_INDEX = {"Dark": 0, "Light": 1, "MutedGreen": 2, "HighContrast": 3}


def cpp_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def fnum(v) -> str:
    # Always produce a valid C++ float literal: a decimal point is required
    # before the 'f' suffix (e.g. "1.0f", not "1f").
    s = repr(float(v))
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s + "f"


def emit_value_call(tid, layer, is_base, theme_idx):
    """Return the C++ call for a 'value' layer, or None if unsupported."""
    val = layer["value"]
    vt = val.get("type")
    sid = cpp_str(tid)
    if is_base:
        if vt == "Color":
            r, g, b, a = val["rgba"]
            return f'        SetBaseColor({sid}, {fnum(r)}, {fnum(g)}, {fnum(b)}, {fnum(a)});'
        if vt == "Float":
            return f'        SetBaseFloat({sid}, {fnum(val["value"])});'
        if vt == "Int":
            return f'        SetBaseInt({sid}, {int(val["value"])});'
        if vt == "Ratio":
            return f'        SetBaseRatio({sid}, {fnum(val["value"])});'
        if vt == "Vec2":
            x, y = val["value"]
            return f'        SetBaseVec2({sid}, {fnum(x)}, {fnum(y)});'
        if vt == "Bezier":
            a, b, c, d = val["value"]
            return f'        SetBaseBezier({sid}, {fnum(a)}, {fnum(b)}, {fnum(c)}, {fnum(d)});'
        if vt == "FontFamily":
            return f'        SetBaseFontFamily({sid}, {cpp_str(val["value"])});'
        if vt == "Reference":
            return f'        SetBaseRef({sid}, {cpp_str(val["ref"])});'
        return None
    else:
        # Per-theme: colour / float / reference are the practical cases.
        if vt == "Color":
            r, g, b, a = val["rgba"]
            return f'        SetThemeColor({sid}, {theme_idx}, {fnum(r)}, {fnum(g)}, {fnum(b)}, {fnum(a)});'
        if vt in ("Float", "Ratio"):
            return f'        SetThemeFloat({sid}, {theme_idx}, {fnum(val["value"])});'
        if vt == "Reference":
            return f'        SetThemeRef({sid}, {theme_idx}, {cpp_str(val["ref"])});'
        return None


def emit_calls(doc):
    theme = doc.get("theme", "Dark")
    is_base = bool(doc.get("isBaseLayer", theme == "Dark"))
    theme_idx = THEME_INDEX.get(theme, 0)
    lines, skipped = [], 0
    for tid, tok in sorted(doc.get("tokens", {}).items()):
        layer = tok.get("layer")
        if not layer:
            continue
        if layer.get("kind") == "reference":
            sid = cpp_str(tid)
            ref = cpp_str(layer["ref"])
            lines.append(f'        SetBaseRef({sid}, {ref});' if is_base
                         else f'        SetThemeRef({sid}, {theme_idx}, {ref});')
        elif layer.get("kind") == "value":
            call = emit_value_call(tid, layer, is_base, theme_idx)
            if call:
                lines.append(call)
            else:
                skipped += 1
    section = "BASE" if is_base else theme
    return section, lines, skipped


def replace_section(text, section, body):
    begin = f"// === GENERATED STYLE: {section} BEGIN ==="
    end = f"// === GENERATED STYLE: {section} END ==="
    block = begin + "\n" + body + "\n" + end
    pat = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.DOTALL)
    if pat.search(text):
        return pat.sub(block, text)
    # Append a new section before the trailing newline.
    return text.rstrip() + "\n\n" + block + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    doc = json.loads(Path(args.json).read_text(encoding="utf-8"))
    if doc.get("format") != "carto-tokens-v1":
        print("error: not a carto-tokens-v1 export", file=sys.stderr)
        return 1

    section, lines, skipped = emit_calls(doc)
    print(f"layer: {section}   token calls: {len(lines)}   skipped: {skipped}")

    if not args.write:
        print("\n--- preview ---")
        print("\n".join(lines[:40]) + ("\n  ..." if len(lines) > 40 else ""))
        print("\nDry run. Re-run with --write to update TokenStyle.generated.inc.")
        return 0

    text = INC.read_text(encoding="utf-8") if INC.exists() else ""
    body = "\n".join(lines) if lines else "        // (no entries for this layer)"
    text = replace_section(text, section, body)
    INC.write_text(text, encoding="utf-8")
    print(f"\nWrote section '{section}' into {INC}")
    print("Rebuild (F5) and relaunch to bake the new values.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
