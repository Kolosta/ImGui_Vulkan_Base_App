#!/usr/bin/env python3
"""One-shot icon reorganiser.

Takes the flat, verbosely-named Material-Symbols dump in resources/icons/ and:
  * renames each file to a short kebab-case concept name,
  * sorts it into a functional-category sub-folder,
  * makes every SVG conformant with docs/SVG_FORMAT.md: a single `ds-primary`
    ink (class added to every painted element; the literal fill is just a
    placeholder — the runtime recolours from a token),
  * drops obvious duplicates.

Idempotent-ish: re-running on an already-clean tree is a no-op for files that
no longer match the verbose pattern. The icon id used by the engine is the
file stem, so callers must use the NEW short names.
"""
import os
import re
import sys
import shutil

ICONS = os.path.join(os.path.dirname(__file__), "..", "resources", "icons")
ICONS = os.path.abspath(ICONS)

VERBOSE_SUFFIX = re.compile(r"_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24(?:\(\d+\))?$")

# concept-name -> category. Anything not listed falls back to a keyword guess.
CATEGORY = {
    # navigation (chevrons / arrows / compass)
    "chevron-down": "navigation", "chevron-up": "navigation",
    "chevron-left": "navigation", "chevron-right": "navigation",
    "arrow-downward-alt": "navigation", "arrow-upward-alt": "navigation",
    "arrow-left-alt": "navigation", "arrow-right-alt": "navigation",
    "arrow-warm-up": "navigation",
    "north": "navigation", "south": "navigation", "east": "navigation",
    "west": "navigation", "north-east": "navigation",
    "north-west": "navigation", "south-east": "navigation",
    "south-west": "navigation",
    # tools
    "draw": "tools", "pen": "tools", "ink-eraser": "tools",
    "select": "tools", "find-replace": "tools", "polyline": "tools",
    "diagonal-line": "tools", "line-end": "tools",
    "line-end-diamond": "tools", "line-start-square": "tools",
    # actions
    "settings": "actions", "reset-settings": "actions",
    "checklist": "actions", "rotate-right": "actions",
    "add-photo-alternate": "actions", "forms-add-on": "actions",
    "new-label": "actions", "label": "actions", "label-off": "actions",
    # text / typography
    "format-align-center": "text", "format-align-justify": "text",
    "format-align-left": "text", "format-align-right": "text",
    "format-clear": "text", "format-color-text": "text",
    "format-indent-increase": "text",
    "format-letter-spacing-standard": "text",
    "format-list-bulleted": "text", "format-shapes": "text",
    "format-strikethrough": "text", "format-text-wrap": "text",
    "format-textdirection-l-to-r": "text",
    "format-underlined-squiggle": "text",
    "text-decrease": "text", "text-fields": "text", "title": "text",
    "subscript": "text", "superscript": "text", "insert-text": "text",
    "language-japanese-kana": "text", "functions": "text",
    # view / canvas
    "background-dot-small": "view", "background-grid-small": "view",
    "grid-on": "view", "texture": "view", "texture-add": "view",
    "texture-minus": "view", "shadow": "view", "contrast-square": "view",
    "image": "view", "image-aspect-ratio": "view", "image-inset": "view",
    # shapes / crop
    "crop-free": "shapes", "crop-landscape": "shapes",
    "crop-square": "shapes", "crop-21-9": "shapes", "crop-2-3": "shapes",
    "crop-3-2": "shapes", "crop-7-5": "shapes", "crop-9-16": "shapes",
    "rounded-corner": "shapes",
    # transform / align / distribute
    "align-flex-end": "transform", "align-flex-start": "transform",
    "align-justify-center": "transform",
    "align-justify-flex-end": "transform",
    "align-justify-stretch": "transform",
    "align-space-around": "transform", "align-space-even": "transform",
    "align-vertical-bottom": "transform",
    "align-vertical-center": "transform",
    "align-vertical-top": "transform",
    "horizontal-align-left": "transform",
    "horizontal-align-right": "transform",
    "horizontal-distribute": "transform",
    "vertical-align-bottom": "transform",
    "vertical-align-top": "transform",
    "height": "transform",
}

# Files that are duplicates / unwanted -> skip entirely.
SKIP = {
    "crop_square_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24(2).svg",
    "crop_square_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24(3).svg",
    "manga_icon.svg", "pen_icon.svg", "pen_icon2.svg",
}

# Multicolour brand/test assets kept as-is (NOT normalised to one ink).
KEEP_AS_IS = {"logo_carto.svg", "three_balls.svg"}


def short_name(filename: str) -> str:
    stem = filename[:-4] if filename.lower().endswith(".svg") else filename
    stem = VERBOSE_SUFFIX.sub("", stem)
    stem = stem.replace("_", "-").strip("-").lower()
    return stem


def guess_category(name: str) -> str:
    if name in CATEGORY:
        return CATEGORY[name]
    for key, cat in (
        ("chevron", "navigation"), ("arrow", "navigation"),
        ("format", "text"), ("text", "text"), ("align", "transform"),
        ("crop", "shapes"), ("image", "view"), ("texture", "view"),
        ("background", "view"),
    ):
        if name.startswith(key) or key in name:
            return cat
    return "misc"


def make_conformant(svg: str) -> str:
    """Add class="ds-primary" to every painted element so the icon is a
    single token-driven zone (see docs/SVG_FORMAT.md). Leaves a literal
    placeholder fill; the engine recolours via the token at runtime."""
    def add_class(m):
        tag = m.group(0)
        if "ds-primary" in tag:
            return tag
        if 'class="' in tag:
            return tag.replace('class="', 'class="ds-primary ', 1)
        # insert class right after the element name
        return re.sub(r"^<(\w+)", r'<\1 class="ds-primary"', tag, count=1)

    # Only opening tags of drawable elements.
    return re.sub(r"<(path|circle|rect|polygon|polyline|line|ellipse)\b[^>]*",
                  add_class, svg)


def main():
    if not os.path.isdir(ICONS):
        print("icons dir not found:", ICONS)
        return 1

    moved, normalised, skipped = 0, 0, 0
    for fn in sorted(os.listdir(ICONS)):
        src = os.path.join(ICONS, fn)
        if not os.path.isfile(src) or not fn.lower().endswith(".svg"):
            continue
        if fn in SKIP:
            os.remove(src)
            skipped += 1
            continue
        if fn in KEEP_AS_IS:
            continue

        name = short_name(fn)
        cat = guess_category(name)
        dst_dir = os.path.join(ICONS, cat)
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, name + ".svg")

        with open(src, "r", encoding="utf-8") as f:
            content = f.read()
        content = make_conformant(content)
        with open(dst, "w", encoding="utf-8") as f:
            f.write(content)
        os.remove(src)
        moved += 1
        normalised += 1

    print(f"moved={moved} normalised={normalised} skipped(dups)={skipped}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
