# Contributing

Thanks for working on this project. This document is the single source of
truth for **how we develop**: build, branch, commit, and ship. Architecture
and coding rules live in [CLAUDE.md](CLAUDE.md); read both.

## TL;DR

```bash
# one-time: local preset overrides (absolute toolchain/vcpkg paths)
#   CMakeUserPresets.json is gitignored — create it from the template below

git checkout main && git pull
git checkout -b <type>/<short-topic>     # never work long on main
# …code…
cmake --preset default && cmake --build --preset default   # must pass
git commit -m "<type>(<scope>): <imperative subject>"
git push -u origin <branch>
gh pr create --base main                  # CI must be green before merge
```

## Build

**Toolchain:** GCC (MSYS2 MinGW64), C++20, CMake ≥ 3.22, Rust (target
`x86_64-pc-windows-gnu`), Vulkan SDK. SDL3 + ImGui are fetched by CMake
(`FetchContent`); the Rust `resvg-bindings` crate and `icon_compiler` are
built automatically during the CMake build.

### Presets

`CMakePresets.json` is **portable and versioned** — it resolves `gcc`/`g++`
from `PATH` and contains no machine-specific paths. Two presets:

| Preset    | Use                                    |
|-----------|----------------------------------------|
| `default` | Local development                      |
| `ci`      | Used by GitHub Actions (do not rename) |

Per-developer absolute paths (explicit MSYS2 compiler, vcpkg toolchain,
etc.) go in **`CMakeUserPresets.json`**, which is **gitignored**. Create it
once by inheriting `default`:

```jsonc
{
  "version": 8,
  "configurePresets": [
    {
      "name": "local",
      "inherits": "default",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "C:/msys64/mingw64/bin/gcc.exe",
        "CMAKE_CXX_COMPILER": "C:/msys64/mingw64/bin/g++.exe"
      }
    }
  ]
}
```

### Commands

```bash
cmake --preset default          # configure
cmake --build --preset default  # build (parallel)
```

The executable lands in `out/build/<preset>/Carto.exe`.

## Branching

- `main` is protected: **no direct pushes**; changes land via PR with a
  green CI.
- One branch per topic. Name it `<type>/<short-topic>`
  (`feat/shortcut-chords`, `fix/icon-pool-leak`, `chore/ci-setup`).
- Rebase or merge `main` into your branch to stay current; never let a PR
  rot far behind `main`.
- Delete the branch after merge (or keep it only if follow-up fixes are
  expected on it).

## Commits — Conventional Commits

`<type>(<scope>): <subject>` — imperative, ≤ 72 chars, no trailing period.
Body explains **why**. One logical change per commit. Full type/scope
tables and examples are in [CLAUDE.md](CLAUDE.md#git-conventions).

Do **not** commit generated files (`IconData.h`, `resvg_c.h`) or runtime
state (`design_system.bin`, `imgui.ini`, `shortcuts.dat`).

## Continuous Integration

`.github/workflows/ci.yml` builds the project on `windows-latest` (MSYS2 +
Rust + Vulkan SDK) and runs a smoke check on the produced binaries.

- Runs automatically on every PR to `main` and on push to `main`.
- Can be triggered **manually from any branch**: GitHub → *Actions* → *CI*
  → *Run workflow* (`workflow_dispatch`).
- A green CI run is **required** before a PR can be merged into `main`.

If CI fails, fix the underlying issue on the branch and push again — the PR
re-runs automatically. Do not merge red.

## Language & style

All code, comments, commit messages, and UI text are **English**. Any
styling work must go through design-system tokens — never hard-code
colors, sizes, radii. See [CLAUDE.md](CLAUDE.md) for specifics.
