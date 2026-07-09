# Continuation Guide

For the next agent (Codex or otherwise) picking up this repository.
Companion to AGENTS.md (rules) and ROADMAP.md (priorities); this file is
the "how", with concrete implementation sketches.

## State of the world (verified, not aspirational)

Working and tested end-to-end:
- `seamforge-cli pipeline` takes `skirt_simple.obj` + two side seams and
  produces: 2 labelled panels, ARAP-flattened UVs (0 flips, ~0 distortion
  on the developable case), deterministic seam pairing (mismatch
  ~1e-7 %), regularised boundaries, `pattern.svg`, `pattern.dxf`,
  per-panel OBJs, `metrics.json`, and a lossless `.sfrproj`.
- `seamforge-cli auto` (no seams supplied) proposes side seams and hits
  IoU 1.0 / 0.999 vs ground truth on clean/hard skirts.
- Qt GUI renders 3D + 2D, draws/deletes seams, segments, flattens,
  exports, saves/loads, undo/redo. Verified by Xvfb screenshot
  (docs/images/gui_skirt_project.png).
- 49/49 Catch2 tests green in ~0.9 s (Bezier fitting, boundary matching, D-Charts baseline, tee/trousers/dart garments).

Read KNOWN_LIMITATIONS.md for the honest gap list.

## How to verify the world before you change it
```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
build/src/tools/make-test-meshes data/meshes
bash experiments/run_all.sh          # drives experiments 1-4 via the CLI
```

## Implementation sketches for the next priorities

### ~~1. Bézier boundary fitting~~ — DONE
Implemented as `sf::fitLoopCurves` (Regularize.h/.cpp): Schneider
least-squares cubics per corner span with an arc-length re-fit loop
(≤ 0.5% per span, see DECISION_LOG D15), true lines for straight spans,
`curves` serialised in `.sfrproj`, SVG `<path>` with C commands, GUI
draws the fitted outline (polyline remains the editable/revert layer).
Tests in `tests/test_bezier.cpp`. Remaining niceties are listed in
KNOWN_LIMITATIONS #8 (curve-native handle editing, smooth join on
corner-free loops).

### ~~Independent-boundary seam matcher~~ — DONE
Implemented as `sf::proposeBoundaryMatches` (Matching.h/.cpp): boundary
loops split at 3D corners into arcs, scored by length ratio + mean
Chamfer distance, greedy best-first assignment, orientation from
endpoint correspondence, ambiguity/unmatched reporting. CLI
`seamforge-cli match` runs the full pre-cut workflow (welding disabled
there — D16); GUI has a "Match Boundaries" action. Tests in
`tests/test_matching.cpp`; remaining gaps in KNOWN_LIMITATIONS #5
(no partial-arc matching, panels must be near sewn position).

### ~~D-Charts developable baseline~~ — DONE
Implemented as `sf::dchartsSegment` (SegmentationBaselines.h/.cpp):
cone-proxy fitting (least normal variance axis), greedy growth with a
compactness term, Lloyd re-fit/reseed iterations, and incremental
Euler-characteristic disk enforcement (forces ≥2 charts on tubes, every
chart flattenable). Evaluated via `seamforge-cli auto --baseline
dcharts`; measured numbers + failure analysis in experiments/README.md.
Not converted to seam paths / production pipeline on purpose — it is an
analysis baseline (construction-blind by nature).

### ~~Procedural T-shirt/trousers/darts~~ — DONE
Implemented via a generic two-sheet sewn construction
(`buildTwoSheet` in Procedural.cpp): front/back bulged grid panels over
a silhouette region sharing boundary vertices exactly where sewn.
`makeBoxyTee` (kimono tee: 4 openings, 4 seams), `makeFlatTrousers`
(pyjama trousers: 3 openings, 3 seams), and `SkirtOptions::darts`
(crease geometry + `GroundTruth::dartPaths`). Cell diagonals are chosen
to never connect two sewn vertices (would be non-manifold). Tube-based
set-in-sleeve tees and 4-panel trousers remain future work
(KNOWN_LIMITATIONS #13); dart *cutting/pairing* is still open (#4).

### 1. XPBD validation prototype (ROADMAP #17)
- `experiments/resim/`: load `.sfrproj`; build particle system from
  panel UVs (rest lengths from UV edges — that's the point: patterns,
  not the 3D mesh, define rest state); constraints: edge distance +
  seam-pair pinning (relations give exact vertex pairs); relax under
  gravity=0 toward the source shape? No — relax freely, then rigid-align
  (Procrustes) and measure bidirectional Chamfer, normal deviation,
  silhouette IoU from 3 axis views. Report into `metrics.json`.
  Accept: Chamfer < 1% of bbox diag on `skirt_simple`.

### 2. BFF flattener
- Either port the reference implementation's core (MIT, ~2k lines,
  cholmod-dependent — replace with Eigen) or implement from the paper
  (boundary curvature → conformal factors → extension). Slot in as
  `makeFlattener("bff")`. Use prescribed boundary lengths = mean of the
  two paired seam lengths to equalise seams exactly.

## Gotchas that will bite you
- **OBJ vertex indexing**: seam/GT files index the FILE's vertex order.
  Only the native OBJ importer preserves it. Don't "simplify" imports to
  Assimp-only (see DECISION_LOG D4).
- **Rebuild adjacency** after any V/F mutation (`buildAdjacency()`);
  `edgeBetween` silently uses stale maps otherwise.
- **Panel ids** = index in `panels` after deterministic sort; relations
  reference ids, tests index `seg.panels[rel.a.panelId]` — keep that
  equality.
- **normalizeFrame flips y for reflection fixes**; if you touch it,
  `test_flatten` reproducibility and `test_export_project` determinism
  are the canaries.
- **Catch2 macro limitation**: no `a || b` expressions with complex
  operands inside REQUIRE (wrap in bool first).
- **Qt6.4 API**: `QAction` text+shortcut menu helpers used here compile
  on 6.4; avoid newer convenience overloads.
- GUI colour mode enum order matters to MainWindow's action group setup.

## Repository etiquette
- Branch: work lands on `claude/seamforge-reverse-init-tpfn8w` (or its
  successor per instructions).
- Every phase: build + ctest + `experiments/run_all.sh` green before
  commit; update ROADMAP/KNOWN_LIMITATIONS/DECISION_LOG in the same
  commit as the code.
