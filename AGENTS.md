# AGENTS.md — Working on SeamForge Reverse

Guidance for AI agents (and humans) continuing this codebase.

## What this project is
Reverse-engineering 2D sewing patterns from 3D garment meshes:
mesh → validate → seams (manual/proposed) → cut into panels → flatten
(LSCM/ARAP) → distortion + seam pairing → regularise → SVG/DXF/JSON.
The two-panel skirt workflow is the reference milestone and is fully
working. Read ARCHITECTURE.md first, then CONTINUATION_GUIDE.md.

## Build & test (the loop you'll run constantly)
```bash
cmake -S . -B build -G Ninja            # Qt6 auto-detected; core builds without it
cmake --build build
ctest --test-dir build                  # 32 tests, must stay green
build/src/tools/make-test-meshes data/meshes
build/src/tools/seamforge-cli pipeline --mesh data/meshes/skirt_simple.obj \
  --seams data/meshes/skirt_simple.seams.json --out out/skirt --project out/skirt/p.sfrproj
xvfb-run -a build/src/app/seamforge --smoke        # GUI alive check
```

## Hard rules
1. **Never present uncertain output as certain.** Proposals carry
   confidence < 1.0; solver failures return messages; segmentation
   problems are reported strings. Preserve this in anything you add.
2. **Never silently repair destructive defects** (non-manifold,
   non-orientable). Report, block, let the user decide.
3. **Preserve correspondence invariants**: `Panel.toOrigV/origFace`
   exact; seam relations vertex-exact along the original seam path.
   If you touch Segmentation.cpp, run `[segmentation]` and `[relations]`
   tests before anything else.
4. **Determinism is a feature under test.** Fixed iteration orders,
   fixed precision in exports, canonical UV frame. A nondeterministic
   map/set iteration will break `test_export_project`.
5. **Core stays Qt-free.** GUI code only under `src/app/`.
6. **No CLO3D anything** — no proprietary code, assets, UI imitation, or
   file formats. Public research + original code only.
7. Numeric thresholds in tests are documented acceptance criteria;
   loosening one requires a DECISION_LOG.md entry.
8. Update ROADMAP.md status and KNOWN_LIMITATIONS.md when you finish or
   discover something; add DECISION_LOG entries for design choices.

## Code conventions
- C++20, Eigen for math, `sf::` namespace, `.h/.cpp` pairs per module.
- Errors: return bool/status + `std::string* err` or message fields; no
  exceptions across module boundaries.
- Tests: Catch2 v3, tag per module (`[mesh]`, `[flatten]`...), analytic
  fixtures from `sf::makeSkirt` — no binary test assets.
- Comments explain constraints/invariants, not narration.

## Where things live
| Area | Files |
|---|---|
| Mesh + paths | `src/core/Mesh.{h,cpp}` |
| Validation | `src/core/Validation.{h,cpp}` |
| Import/export meshes | `src/core/Io.{h,cpp}` |
| Seams + proposal | `src/core/Seam.{h,cpp}`, `Curvature.*` |
| Cutting/panels | `src/core/Segmentation.{h,cpp}` |
| Flattening + distortion | `src/core/Flatten.{h,cpp}` |
| Seam pairing | `src/core/Relations.{h,cpp}` |
| Regularisation | `src/core/Regularize.{h,cpp}` |
| SVG/DXF | `src/core/Export.{h,cpp}` |
| Project JSON | `src/core/Project.{h,cpp}` |
| Benchmark shapes | `src/core/Procedural.{h,cpp}` |
| CLI | `src/tools/seamforge_cli.cpp` |
| GUI | `src/app/*` (AppState = document, Viewport3D = 3D, PatternView = 2D) |

## Environment notes
- Ubuntu 24.04: `apt install qt6-base-dev libqt6opengl6-dev libeigen3-dev
  libassimp-dev catch2 nlohmann-json3-dev libgl1-mesa-dev xvfb`.
- GUI verification headless: `xvfb-run -a ... --open f.sfrproj --screenshot s.png`.
- Sample end-to-end outputs land in `out/` (gitignored).

## Current next task
See ROADMAP.md "Next priorities" — #1 is Bézier boundary fitting;
CONTINUATION_GUIDE.md has per-task implementation sketches.
