# Test Strategy

## Principles
1. Every pipeline stage is testable headless (core is Qt-free; the CLI
   drives the full workflow).
2. Tests use **procedural meshes with analytic ground truth**
   (`sf::makeSkirt`) — no binary fixtures to rot, failures reproduce
   from a seed.
3. Uncertainty handling is itself under test: proposals must never claim
   confidence 1.0; non-separating seams must produce problem reports;
   solver failures must return messages.
4. Determinism is asserted, not assumed (byte-identical SVG, identical
   reruns of segmentation/flattening, lossless project round-trip).

## Layers

### Unit + integration tests (Catch2, `tests/`, 45 cases)
Run: `ctest --test-dir build` — currently **45/45 passing**, ~0.5 s.

| File | Covers |
|---|---|
| `test_mesh.cpp` | adjacency counts, 2 boundary loops, winding, Dijkstra path validity |
| `test_validation.cpp` | weld+report, winding repair, degenerate drop, **non-manifold = blocking error**, scale flag, clean-mesh pass |
| `test_segmentation.cpp` | exact GT partition (2 & 4 panels), correspondence exactness, seam-vertex duplication, boundary ancestry (2 seams + waist + hem), non-separating seam reporting, determinism |
| `test_flatten.cpp` | ARAP/LSCM acceptance thresholds (below), reproducibility, A-line honest distortion, failure exposure |
| `test_relations.cpp` | vertex-exact deterministic pairing, 2D length mismatch < 2%, ambiguity flag on non-separating seam |
| `test_regularize.cpp` | deviation ≤ tolerance, point reduction, corner preservation/detection, straight-run detection, raw kept |
| `test_bezier.cpp` | rectangle → 4 true lines; skirt panel → few cubics + line seams, closed connected chain, fit deviation bound, arc-length error ≤ 0.5%, whole-loop length within 1%, determinism, raw untouched; corner-free loop fallback |
| `test_matching.cpp` | pre-cut skirt: side arcs matched (conf < 1.0, endpoints coincide, 2D mismatch < 2%), waist/hem reported unmatched, determinism, graceful decline (<2 panels), cut-ancestry boundaries excluded |
| `test_dcharts.cpp` | full coverage, ≥2 charts on a tube, every chart a topological disk (χ=1), near-zero cone fit on the developable frustum, determinism, component separation, and an explicit construction-blindness bound (IoU < 0.95 vs GT) |
| `test_export_project.cpp` | SVG completeness + byte-determinism, DXF structure, **lossless project round-trip**, garbage rejection |
| `test_propose.cpp` | 2 proposals, valid paths, confidence ∈ (0,1), endpoints on openings, segmentation IoU > 0.9, graceful decline on closed mesh |

### Acceptance criteria — two-panel skirt milestone
Segmentation (given correct side seams):
- exactly 2 panels; face partition matches ground truth 100%;
- every panel single-component, manifold, ≥1 boundary loop;
- deterministic across reruns.

Flattening (developable panel, ARAP):
- flipped triangles **= 0**;
- mean angle distortion ≤ 1.02 (measured ~1.0000);
- mean |log area ratio| ≤ 0.02 (measured ~3e-10);
- boundary length change ≤ 1% (measured ~5e-9%);
- reproducible to < 1e-12.

Seam pairing:
- both relations `cut-ancestry`, confidence 1.0, vertex-exact;
- 2D seam-length mismatch < 2% (measured ~1e-7%).

Boundary curve fitting (Schneider):
- fitted-chain arc-length error per span ≤ 0.5% (measured 0.19% on the
  skirt panel); loop closed and connected; deterministic; raw polyline
  preserved for revert.

Export/persistence:
- SVG byte-identical across runs (including Bézier `<path>` outline);
  project save→load→save identical (curves included).

### End-to-end workflow verification (manual/CI script)
```
build/src/tools/make-test-meshes data/meshes
build/src/tools/seamforge-cli validate --mesh data/meshes/skirt_simple.obj
build/src/tools/seamforge-cli pipeline --mesh data/meshes/skirt_simple.obj \
    --seams data/meshes/skirt_simple.seams.json --out out/skirt \
    --flattener arap --project out/skirt/skirt.sfrproj --heatmap
build/src/tools/seamforge-cli roundtrip --project out/skirt/skirt.sfrproj
build/src/tools/seamforge-cli auto --mesh data/meshes/skirt_simple.obj \
    --truth data/meshes/skirt_simple.gt.json --out out/auto
build/src/tools/seamforge-cli match --mesh data/meshes/skirt_precut.obj \
    --out out/precut --project out/precut/p.sfrproj
```
All exit 0; `metrics.json` records the measured distortion.

### GUI checks
- Compile under Qt 6.4; `xvfb-run seamforge --smoke` exits 0.
- `xvfb-run seamforge --open <proj> --screenshot shot.png` renders the
  3D mesh + seams and the 2D panels (visually verified; screenshot in
  `docs/images/`). Interactive tools (seam drawing, dragging control
  points) are exercised manually — scripted QTest coverage is future
  work (KNOWN_LIMITATIONS #12).

## Benchmark dataset (`make-test-meshes`)
| Case | Property tested |
|---|---|
| `skirt_simple` | developable frustum, 2 panels — the milestone case |
| `skirt_aline` | curved profile → genuine distortion must be reported |
| `skirt_fourpanel` | 4 seams/panels, multi-panel ancestry |
| `skirt_noisy` | jittered vertices (scan-like) |
| `skirt_dense` | 10k faces, performance sanity |
| `skirt_hard` | strong bulge + noise — proposal stressor (IoU 0.999) |
| `skirt_precut` | two disconnected panel meshes — boundary-matching case |

Each ships `*.gt.json` (face labels + seam paths) and `*.seams.json`
(pipeline-ready). Planned extensions: T-shirt, trousers, dress, darts,
curved seams (KNOWN_LIMITATIONS #13).

### Metrics tracked (auto + pipeline reports)
panel-count accuracy, per-panel IoU vs GT, seam-pair accuracy
(implicitly via ancestry tests), 2D distortion suite, seam-length
mismatch, reconstruction distance (stage 17), manual-correction time
(future, GUI telemetry not implemented).

## Regression policy
Any bug fix lands with a test reproducing it. Numeric thresholds in
tests are the documented acceptance criteria — loosening one requires a
DECISION_LOG entry.
