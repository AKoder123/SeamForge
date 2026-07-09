# Roadmap

Stages follow the mandated implementation order. ✅ = done and tested in
this repository; 🔶 = partial; ⬜ = not started.

| # | Stage | Status | Notes |
|---|---|---|---|
| 1 | Repository & build system | ✅ | CMake + Ninja, core/tools/app/tests, Catch2 wired |
| 2 | Research review | ✅ | RESEARCH.md, ALGORITHM_COMPARISON.md |
| 3 | Mesh representation | ✅ | native TriMesh + edge adjacency (half-edge equivalent) |
| 4 | Import & validation | ✅ | Assimp + native OBJ; 10-check validate/repair with reporting |
| 5 | 3D viewer | ✅ | orbit/pan/zoom, wireframe, curvature, segmentation & confidence colouring |
| 6 | Manual seam drawing | ✅ | vertex picking + Dijkstra snapping, commit/cancel, select/delete |
| 7 | Cutting & segmentation | ✅ | ancestry-preserving cut, problem reporting, undo/redo |
| 8 | One-panel flattening | ✅ | LSCM + ARAP behind IFlattener (experiment 1) |
| 9 | Two-panel skirt extraction | ✅ | full pipeline, CLI + GUI (experiment 2) |
| 10 | Distortion visualisation | ✅ | angle/area heatmaps 2D, metrics in CLI/log |
| 11 | Seam correspondence | ✅ | deterministic cut-ancestry pairing, reversed toggle, mismatch % |
| 12 | Boundary regularisation | ✅ | DP + corners + straight runs + Schneider cubic Bézier fitting (seam-length budget 0.5%), raw revert |
| 13 | SVG export | ✅ | deterministic, labelled; DXF R12 minimal as bonus |
| 14 | Project save/load | ✅ | .sfrproj schema v1, lossless round-trip |
| 15 | Assisted seam proposal | ✅ | silhouette-prior geodesics + curvature evidence, capped confidence |
| 16 | Automatic segmentation baseline | 🔶 | proposal-based baseline: IoU 1.0 clean / 0.999 hard; D-Charts baseline pending |
| 17 | Reconstruction validation | ⬜ | XPBD prototype: pin paired seam vertices, relax, Chamfer vs source |
| 18 | Learned experiments | ⬜ | isolated, behind interfaces; Korosteleva & Lee dataset |

## Next priorities (in order)

1. **Independent-boundary seam matcher** (extends stage 11): score
   candidate pairings by length/orientation/original-proximity for
   boundaries *not* created by the same cut (needed for garments whose
   seams coincide with existing mesh boundaries).
2. **D-Charts quasi-developable baseline** (stage 16): second automatic
   segmentation baseline; compare on benchmark IoU vs the silhouette
   prior; failure analysis on `skirt_hard`, T-shirt.
3. **Dataset growth** (TEST_STRATEGY): procedural T-shirt (tube + two
   sleeve tubes), trousers, dart cases (a dart = interior seam whose two
   sides pair with each other — the relation model already supports it).
4. **XPBD validation prototype** (stage 17): assemble panels from
   `.sfrproj`, pin seam pairs, relax, report Chamfer/normal/silhouette
   metrics into `metrics.json`.
5. **BFF flattener** (ALGORITHM_COMPARISON §1) for prescribed boundary
   lengths → exact seam-length equalisation across paired seams.
6. **Learned experiments** (stage 18): seam-candidate scoring first
   (smallest, best-supervised task), then panel-count/topology.

## Deliberately deferred (per product direction)
Full garment authoring, general cloth simulation, avatars, fabric
libraries, photorealistic rendering, animation, collaboration,
marketplaces, AI image generation, accounts, grading, complex
manufacturing exports.
