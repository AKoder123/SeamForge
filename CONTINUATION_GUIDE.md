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
- 32/32 Catch2 tests green in ~0.3 s.

Read KNOWN_LIMITATIONS.md for the honest gap list.

## How to verify the world before you change it
```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
build/src/tools/make-test-meshes data/meshes
bash experiments/run_all.sh          # drives experiments 1-4 via the CLI
```

## Implementation sketches for the next priorities

### 1. Bézier boundary fitting (finish ROADMAP #12)
- New: `sf::fitBezierSegments(const RegularizedLoop&, double tol)` in
  Regularize. Between consecutive corners (`isCorner`), run Schneider's
  least-squares cubic fitting (Graphics Gems I, public domain algorithm)
  over the RAW points of that span (indices are `keptIdx` ranges over
  `raw`). Store as `struct BezierSegment { Vec2 p0, c0, c1, p1; }` added
  to RegularizedLoop (+ JSON: extend schema, bump minor field only —
  loaders ignore unknown keys, so schemaVersion can stay 1).
- Straight runs (`isStraight`) stay lines.
- Constraint: total curve length per seam segment must stay within 0.5%
  of the raw polyline length (seam compatibility) — add a test.
- SVG: emit `C` path commands; keep polyline as the revert layer.

### 2. Independent-boundary seam matcher (ROADMAP #11 extension)
- New module `Matching.{h,cpp}`:
  `std::vector<SeamRelation> proposeBoundaryMatches(panels, mesh)`.
- Candidates: all pairs of `BoundarySegment`s with `seamId == -1` from
  different panels. Score = w1·lengthSimilarity + w2·meanClosestPoint
  distance in ORIGINAL 3D (use `toOrigV` to get 3D positions) +
  w3·endpoint proximity. Emit `source="boundary-matching"`,
  confidence = normalised score (cap 0.9), both directions tested and
  `reversed` set by whichever endpoint pairing is closer.
- Test: split the skirt into two separate OBJ panel meshes, import as
  one scene (2 components), verify the waist/hem stay unmatched and the
  two side pairs are found.

### 3. D-Charts developable baseline (ROADMAP #16)
- New `SegmentationBaselines.{h,cpp}`: greedy chart growth; fitting
  proxy per chart = cone (Julius et al. eq. 2: normals lie on a circle);
  merge/regularise; convert chart boundaries to seam paths (shortest
  edge paths along chart frontier), then reuse the normal pipeline.
- Evaluate with `seamforge-cli auto --baseline dcharts`.

### 4. Procedural T-shirt/trousers (TEST_STRATEGY dataset)
- Extend Procedural.cpp: trousers = two frustum legs + Y-join, known
  seam paths at inseam/outseam; T-shirt = torso tube + two sleeve tubes
  with raglan or set-in boundaries. Keep exact face labels. Darts:
  radial slit in a disk panel (seam pairing with itself → exercises
  KNOWN_LIMITATIONS #4).

### 5. XPBD validation prototype (ROADMAP #17)
- `experiments/resim/`: load `.sfrproj`; build particle system from
  panel UVs (rest lengths from UV edges — that's the point: patterns,
  not the 3D mesh, define rest state); constraints: edge distance +
  seam-pair pinning (relations give exact vertex pairs); relax under
  gravity=0 toward the source shape? No — relax freely, then rigid-align
  (Procrustes) and measure bidirectional Chamfer, normal deviation,
  silhouette IoU from 3 axis views. Report into `metrics.json`.
  Accept: Chamfer < 1% of bbox diag on `skirt_simple`.

### 6. BFF flattener
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
