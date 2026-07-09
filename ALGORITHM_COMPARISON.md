# Algorithm Comparison

Decision tables for each replaceable pipeline stage. "Status" refers to
this repository. Measured numbers come from `seamforge-cli` runs on the
procedural benchmark (`data/meshes`, see TEST_STRATEGY.md).

## 1. Flattening (`sf::IFlattener`)

| Method | Type | Angle dist. | Area dist. | Flip safety | Boundary control | Cost | Licence of ref. impl. | Status |
|---|---|---|---|---|---|---|---|---|
| **LSCM** (Lévy 02) | linear conformal | excellent | unbounded | none | pins only | 1 sparse solve | libigl MPL2 / native here | **implemented** (`lscm`) |
| **ARAP** (Liu 08) | local/global isometric | very good | very good | counted, not prevented | pins | prefactor + k iters | libigl MPL2 / native here | **implemented, default** (`arap`) |
| ABF++ (Sheffer 05) | nonlinear conformal | excellent | unbounded | none | none | Newton iters | Blender GPL | documented only |
| **BFF** (Sawhney-Crane 17) | conformal w/ boundary data | excellent | free | bijective variant | **target boundary lengths/angles** | few solves | ref. code MIT | planned for seam-length equalisation |
| SLIM (Rabinovich 17) | inversion-free descent | good | good | **guaranteed no flips** | pins | many cheap iters | libigl MPL2 | fallback if flips occur |

Measured (skirt front panel, 1024 tris):
- ARAP: mean angle 1.0000, mean |log area| 3.2e-10, boundary Δ 5.5e-9 %, 0 flips (developable case).
- ARAP on A-line (bulge 0.06): mean angle 1.027, mean |log area| 0.024, boundary Δ 4.5 %, 0 flips — honest non-developability reporting.
- LSCM: mean angle ≤ 1.0002 on both; area rescaled globally to match 3D.

Selection rationale: sewing panels are near-developable by construction,
so the isometric family (ARAP) dominates; conformal-only methods leave
scale gradients that corrupt seam lengths.

## 2. Segmentation / panel extraction

| Method | Needs | Produces | Garment fit | Cost | Status |
|---|---|---|---|---|---|
| **Cut along confirmed seam curves** (flood fill across non-seam edges) | seam vertex paths | exact panels + ancestry | perfect when seams known | O(F) | **implemented, primary** |
| Silhouette-prior side-seam proposal + cut | clean tube-like garment, 2 openings | 2 panels, confidence | skirts/dresses: IoU 1.00 clean, 0.999 hard | O(E log V) | **implemented** (experiment 3/4) |
| Dihedral-crease region growing | visible seam geometry | candidate boundaries | scans w/ stitching ridges | O(F) | evidence only (feeds confidence) |
| D-Charts quasi-developable growth | manifold mesh, threshold | dev. charts | good baseline, boundaries not construction-aware | O(F log F) | planned baseline #2 |
| Spectral/geodesic clustering | Laplacian eigs | k parts | sleeve/torso split | eigensolve | deferred |
| Learned (NeuralTailor-style) | trained model | panels+stitches | broad but unguaranteed | GPU | deferred, isolated experiments |

## 3. Seam correspondence

| Method | Precondition | Guarantee | Status |
|---|---|---|---|
| **Cut ancestry** (both sides of one cut curve) | seam was cut by us | deterministic, vertex-exact, confidence 1.0 | **implemented** |
| **Boundary matching** (corner-split arcs, length ratio + 3D Chamfer, greedy best-first) | independent boundaries, panels near sewn position | heuristic; confidence ≤ 0.9; ambiguity + unmatched arcs reported | **implemented** (`proposeBoundaryMatches`) |
| Learned stitch prediction | trained GNN | probabilistic | deferred |

## 4. Boundary regularisation

| Method | Pros | Cons | Status |
|---|---|---|---|
| **Corner-aware Douglas-Peucker** + straight-run detection | simple, deterministic, tolerance = deviation bound | polyline representation | **implemented** (revert layer) |
| **Least-squares cubic Bézier fitting per corner span (Schneider 90)** | smooth curves, few segments (skirt panel: 96 raw pts → 4 segments), arc-length error ≤ 0.5% enforced by adaptive re-fitting | C0 joins on corner-free loops; GUI handles not curve-native yet | **implemented** (`fitLoopCurves`) |
| Active contours on distortion field | seam-length aware | complex, global | deferred |

## 5. Mesh representation

| Option | Pros | Cons | Decision |
|---|---|---|---|
| **Native TriMesh + edge adjacency** | zero deps, exact control of cut/ancestry, non-manifold tolerated for reporting | fewer built-ins | **chosen** |
| OpenMesh half-edge | mature kernel | BSD-3, heavier API; cutting with ancestry still custom | available on system, not required |
| CGAL Surface_mesh | rich algorithms | GPL/commercial licence risk for parts | rejected for core |

## 6. Import

| Option | Behaviour | Decision |
|---|---|---|
| Assimp (all formats) | expands OBJ vertices per-face → breaks external vertex indexing | used for glTF/GLB/PLY/STL |
| **Native OBJ parser** | preserves file vertex order exactly (seam files reference indices) | **used for .obj** |

## 7. Validation-by-resimulation (planned, stage 17)

| Simulator | Licence | Fit | Notes |
|---|---|---|---|
| **Custom XPBD cloth** | ours | prototype-grade | seam pins from exact vertex pairing; simplest controlled option |
| Project Chrono | BSD-3 | production-grade | heavier dependency |
| Blender cloth (external) | GPL (process boundary OK) | oracle-grade | scriptable offline check, no linkage |
| DiffCloth | MIT | differentiable refinement | stage 18 |
