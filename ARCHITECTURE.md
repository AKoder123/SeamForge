# Architecture

## Overview

SeamForge Reverse converts a completed 3D garment mesh into editable 2D
construction panels. The system is a **pipeline of replaceable stages**
around a shared document model; the GUI is a thin shell over the same
core library that the headless CLI drives.

```
            ┌────────────────────────────────────────────────────────┐
mesh file ─▶│ Io (Assimp / native OBJ)                               │
            └──────────────┬─────────────────────────────────────────┘
                           ▼
            ┌────────────────────────────┐   issues, repairs
            │ Validation (validate/repair)│──────────────────▶ report
            └──────────────┬─────────────┘
                           ▼ TriMesh (edge adjacency, boundary loops)
            ┌────────────────────────────┐
            │ Curvature (features)        │─▶ evidence for proposals & display
            └──────────────┬─────────────┘
                           ▼
   user drawing ─────▶ ┌───────────────┐ ◀───── proposeSideSeams (priors)
   (GUI Dijkstra snap) │ Seams          │        confidence + evidence
                       └──────┬────────┘
                              ▼
            ┌────────────────────────────┐
            │ Segmentation (cut+ancestry)│─▶ Panels (exact 3D↔local maps,
            └──────────────┬─────────────┘    boundary ancestry segments)
                           ▼
            ┌────────────────────────────┐
            │ Flatten (IFlattener:       │─▶ UV per panel + solver status
            │  LSCM / ARAP)              │
            └──────────────┬─────────────┘
                           ▼
        ┌───────────────┐  ┌──────────────────┐  ┌─────────────────┐
        │ Distortion     │  │ Relations         │  │ Regularize       │
        │ (σ1,σ2,flips)  │  │ (cut ancestry →   │  │ (DP + corners,   │
        │                │  │  seam pairing)    │  │  raw kept)       │
        └──────┬────────┘  └──────┬───────────┘  └──────┬──────────┘
               ▼                  ▼                     ▼
            ┌────────────────────────────────────────────────┐
            │ Export (SVG, DXF)  +  Project (.sfrproj JSON)  │
            └────────────────────────────────────────────────┘
```

## Module map (src/core, no UI dependencies)

| Module | Responsibility | Replaceable interface |
|---|---|---|
| `Mesh` | TriMesh, edge adjacency, boundary loops, components, Dijkstra | — (foundation) |
| `Io` | import (Assimp, native OBJ), OBJ export | `IMeshImporter` |
| `Validation` | detect + conservatively repair defects, full reporting | options struct |
| `Curvature` | Gaussian/mean curvature, dihedral deviations | — |
| `Seam` | seam paths w/ provenance+confidence, path validation, side-seam proposal | proposal is free function → future `ISeamProposer` |
| `Segmentation` | cut along seams, panels with exact correspondence + boundary ancestry | — (deterministic core) |
| `Flatten` | LSCM, ARAP, distortion metrics | **`IFlattener`** (`makeFlattener(name)`) |
| `Relations` | deterministic seam pairing from ancestry, 2D length mismatch | — |
| `Regularize` | boundary simplification, corners, straight runs, revertible | options struct |
| `Export` | deterministic SVG + minimal DXF R12 | — |
| `Project` | versioned self-contained JSON (.sfrproj) | schema version gate |
| `Procedural` | benchmark meshes with analytic ground truth | — |

## Frontends

- **`src/tools/seamforge-cli`** — headless pipeline driver
  (`validate | propose | pipeline | auto | roundtrip`). Every capability is
  exercisable and testable without a display; the experiments and CI use
  this.
- **`src/tools/make-test-meshes`** — writes the benchmark dataset.
- **`src/app/seamforge`** — Qt 6 GUI.
  - `AppState`: document + snapshot undo/redo. Snapshots reuse the
    project JSON serialisation so undo state and the file format cannot
    diverge.
  - `Viewport3D` (QOpenGLWidget): orbit/pan/zoom, plain/curvature/
    segmentation/confidence colouring, wireframe, seam display, seam
    drawing (screen-space vertex picking; anchors joined by shortest
    edge paths = geodesic snapping approximation), seam select/delete.
  - `PatternView` (QGraphicsView): panels with angle/area distortion
    heatmaps, raw vs regularised boundary, draggable control points,
    seam labels, dimensions, double-click relabelling.
  - Automated-check flags: `--smoke`, `--open <file>`, `--screenshot <png>`.

## Key design decisions (details in DECISION_LOG.md)

1. **Semi-automatic first.** Every automatic result carries
   `confidence` + `evidence` and is user-editable; validation
   distinguishes auto-repairs from unresolved problems; solver failures
   surface as messages, never silently degrade.
2. **Correspondence is sacred.** `Panel.toOrigV`/`origFace` map every
   local element to the source mesh; seam cutting duplicates vertices
   per panel, so 3D↔2D mapping is exact by construction and seam pairing
   is deterministic (`cut-ancestry`, confidence 1.0).
3. **Determinism everywhere.** Fixed pin selection, deterministic 2D
   frame normalisation (orientation, principal axis, sign convention),
   stable panel ordering, fixed-precision export → byte-identical
   reruns, meaningful diffs, reproducible tests.
4. **Core is Qt-free.** The GUI can be replaced (or absent, e.g. CI)
   without touching algorithms.
5. **Units are metres end-to-end**; UV is in metres too (ARAP is
   scale-preserving), so seam lengths compare directly; export converts
   to mm.

## Threading & performance

Current sizes (≤ ~50k triangles) run interactively single-threaded:
full skirt pipeline ≈ 0.1 s. Eigen sparse solves are the hotspot;
ARAP prefactors once per panel. If scans get large: move flattening to a
worker thread (GUI already routes everything through `AppState` signals)
and switch distortion display to vertex buffers.

## Error-handling policy

- Import/solve/serialisation functions return status + message; no
  exceptions cross module boundaries (JSON parse wrapped).
- Validation severities: Info (expected garment traits), Warning
  (quality risks), Error (blocking: non-manifold, non-orientable).
  `flattenable()` gates the pipeline.
- Segmentation reports non-separating seams, unanchored endpoints and
  per-panel topology problems as strings the UI shows verbatim.
