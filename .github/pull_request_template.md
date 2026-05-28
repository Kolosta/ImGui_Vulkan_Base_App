<!--
  Keep the PR title in Conventional Commits form, e.g.:
    feat(ui): add collapsible shortcut groups
    fix(vector-graphics): prevent descriptor pool exhaustion
  See CONTRIBUTING.md for the full convention.

  GitFlow base branch:
    • feature / fix / chore / docs / refactor  → base: develop
    • release/*  → base: main  (and back-merge to develop)
    • hotfix/*   → base: main  (and back-merge to develop)
-->

## What

<!-- One or two sentences: what does this PR change? -->

## Why

<!-- The motivation / problem being solved. The diff already shows "what". -->

## How

<!-- Notable implementation decisions, trade-offs, follow-ups. -->

## Checklist

- [ ] Builds locally (`cmake --preset default && cmake --build --preset default`)
- [ ] Commit messages follow Conventional Commits
- [ ] All UI text / code / comments are in English
- [ ] Styling goes through design-system tokens (no hard-coded values)
- [ ] No runtime state or generated files committed
      (`design_system.bin`, `imgui.ini`, `shortcuts.dat`, `IconData.h`, `resvg_c.h`)
- [ ] CI is green

## Screenshots / notes

<!-- Optional: UI changes, before/after, perf numbers, etc. -->
