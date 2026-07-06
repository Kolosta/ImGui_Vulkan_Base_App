# Ink — automated performance testing

Every performance-relevant lot ships with measurements, not impressions.
`ink_bench` is a headless benchmark executable built next to the app; the
user's manual F7 checks complement it, they don't replace it.

## 1. The harness

- Target: `ink_bench` (CMake, under `src/Ink/tests/perf/`), links `Ink` only —
  no SDL window, no ImGui. Creates its own Vulkan device (same feature set as
  the app) and renders offscreen through the exact same render graph
  (PresentPass swapped for a no-op/readback).
- Run: `ink_bench --scene <name> --frames 300 --warmup 60 --out results/<file>.json`
  (defaults run the full suite). Deterministic scenes: fixed RNG seed, fixed
  virtual viewport 1920×1080, fixed camera script per scene.
- Output: one JSON per run (schema below) written under `bench/results/`
  (git-ignored), plus a one-line console summary per scene.

## 2. Metrics (per scene)

| Metric | Source |
|--------|--------|
| `compileMs` | Scene::Compile CPU time (avg / p99) |
| `geomMs` | GeometryCache::Refresh CPU time |
| `syncMs` | GpuScene::Sync (staging + submit record) |
| `batchMs` | Batcher::Build |
| `recordMs` | command buffer recording |
| `gpuPassMs.*` | per-pass GPU timestamps (content, composite, overlay, picking, resolve) |
| `frameMs` | wall-clock frame (avg / p50 / p99) |
| `triangles`, `drawCalls`, `batches`, `instances` | Batcher counters |
| `poolBytes`, `peakStagingBytes` | allocator stats |

All exposed through `Ink::Stats` — the same struct the app's Dev panel HUD
reads, so bench numbers and in-app numbers are the same counters.

## 3. Scene suite (grows with the lots)

| Scene | Stresses | Added with |
|-------|----------|-----------|
| `empty` | frame overhead floor | Lot 1 |
| `paths_10k` | 10 000 random filled+stroked paths, static | Lot 2 |
| `edit_heavy` | one 5 000-anchor path re-tessellated every frame (simulated drag) | Lot 3 |
| `zoom_sweep` | camera zoom ×0.1→×100 over paths_10k (tier churn) | Lot 3 |
| `blend_groups` | 500 nested groups with non-Normal blends (isolation stress) | Lot 4 |
| `instances_100k` | 100 000 instances of 10 definitions (instancing claim check) | Lot 5 |
| `pattern_fill` | large region, dense motif lattice | Lot 5 |
| `along_path` | 10 000 instances along long curves (modifier eval) | Lot 5 |
| `images` | 200 large images, mixed with vectors | Lot 6 |
| `pick_storm` | picking query every frame over instances_100k | Lot 8 |

Scene definitions are code (`tests/perf/scenes/*.cpp`) building documents via
the public Document API — they double as integration tests of that API.

## 4. Baselines & comparison

- `bench/baselines/<scene>.json` (tracked in git) stores the accepted
  reference numbers **per machine profile** (key: GPU name + driver major).
- `ink_bench --compare` prints a table vs the baseline and exits non-zero if
  a metric regresses beyond its threshold (default: frameMs p50 +10 %,
  gpu passes +15 %) — usable manually or in CI later.
- Updating a baseline is an explicit `--write-baseline` run, reviewed in the
  PR like any code change.

## 5. JSON schema (v1)

```json
{
  "schema": 1,
  "scene": "paths_10k",
  "machine": { "gpu": "...", "driver": "...", "cpu": "..." },
  "build": { "commit": "...", "config": "Debug|Release" },
  "frames": 300,
  "metrics": {
    "frameMs":   { "avg": 0.0, "p50": 0.0, "p99": 0.0 },
    "compileMs": { "avg": 0.0, "p99": 0.0 },
    "geomMs":    { "avg": 0.0, "p99": 0.0 },
    "syncMs":    { "avg": 0.0, "p99": 0.0 },
    "batchMs":   { "avg": 0.0, "p99": 0.0 },
    "recordMs":  { "avg": 0.0, "p99": 0.0 },
    "gpuPassMs": { "content": 0.0, "composite": 0.0, "overlay": 0.0,
                   "picking": 0.0, "resolve": 0.0 },
    "counters":  { "triangles": 0, "drawCalls": 0, "batches": 0,
                   "instances": 0, "poolBytes": 0 }
  }
}
```

## 6. Non-perf tests

`tests/unit/` (ctest, like `ds_token_tests`): geometry correctness (stroker
golden outlines incl. open-path inside/outside, dash arc-lengths, fill rules,
bounds), document invariants (id uniqueness, cycle refusal, change-log
completeness). These gate the perf work: a fast wrong stroke is worthless.
