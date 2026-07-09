# Decision Log

Chronological record of significant technical decisions and their
rationale. Reversing any of these deserves a new entry.

## D1 — Native TriMesh instead of OpenMesh/CGAL (2026-07)
Cutting along seam curves while recording boundary ancestry and exact
3D↔panel correspondence is the heart of the product; owning the ~400-line
mesh structure makes that logic transparent and dependency-free.
OpenMesh (installed, BSD-3) remains available if half-edge circulators
become necessary; CGAL rejected for core because of GPL exposure of the
relevant packages.

## D2 — Non-manifold edges are blocking errors, never repaired (2026-07)
Any automatic fix (edge splitting, face deletion) is destructive and
changes garment topology silently — violating the core product
principle. Detection + refusal with a clear report instead.

## D3 — Weld/winding/degenerate repairs ARE automatic, but reported (2026-07)
These repairs are information-preserving (duplicate positions, face
orientation, zero-area faces). Each is listed in the validation report
with `autoRepaired=true` so nothing happens silently.

## D4 — Native OBJ parser alongside Assimp (2026-07)
Discovered during end-to-end testing: Assimp expands OBJ vertices
per-face (5056 duplicates on the 1088-vertex skirt), destroying the
vertex indexing that seam files and ground truth reference. OBJ is now
parsed natively (order-preserving); Assimp handles glTF/GLB/PLY/STL
where we generate no external vertex references.

## D5 — ARAP (init LSCM) as default flattener (2026-07)
Sewing panels are near-developable; the isometric family preserves the
lengths that sewing requires. Measured on the benchmark: boundary-length
error ~5e-9 % (developable) / 4.5 % (deliberately non-developable
A-line, honestly reported). LSCM kept as selectable alternative and as
the ARAP initialiser. BFF documented as the upgrade for prescribed
boundary lengths.

## D6 — UV in metres, area-true (2026-07)
LSCM output is rescaled so 2D area equals 3D area; ARAP is naturally
scale-preserving. Seam lengths and export dimensions are then physically
meaningful without per-panel scale bookkeeping.

## D7 — Deterministic frame normalisation (2026-07)
Flattening solutions are defined up to rigid motion; tests, diffs and
stable UX need one canonical representative. Chosen: fix reflection by
majority triangle orientation, rotate UV-PCA major axis to +Y, resolve
sign by mass convention, translate bbox min to origin.

## D8 — Seam pairing via cut ancestry, not geometric matching (2026-07)
When we cut a seam, both resulting boundaries are known exactly —
matching them geometrically afterwards would throw information away and
reintroduce ambiguity. Ancestry is recorded during segmentation
(edge→seamId), giving vertex-exact deterministic relations with
confidence 1.0. Geometric matching remains necessary only for
boundaries we did not create (ROADMAP #2).

## D9 — Confidence is capped for proposals (2026-07)
`proposeSideSeams` returns 0.35 base + curvature support, capped at 0.9.
A proposal can never present as certain; only user confirmation yields
1.0. This encodes the "never hide ambiguity" principle in the type
system's habits rather than UI copy alone.

## D10 — Undo/redo as project-JSON snapshots (2026-07)
One serialisation to maintain; undo state provably equals persisted
state; trivially correct across all operations. Accepted cost: memory on
huge meshes (capped at 64 snapshots). Command-pattern undo can replace
it behind the same AppState API if needed.

## D11 — GUI on legacy fixed-function GL with a compatibility profile (2026-07)
The viewer draws <100k triangles; immediate mode + QOpenGLWidget is the
shortest correct path and works on Mesa/llvmpipe (verified via Xvfb
screenshot). Migrating to buffered core-profile rendering is a contained
change inside Viewport3D when mesh sizes demand it.

## D12 — Screen-space vertex picking instead of ray-triangle intersection (2026-07)
Picking projects all vertices and takes the nearest within an 18px
radius with a depth preference — robust to coarse meshes, no BVH needed
at current sizes, and it degrades gracefully (nothing picked = no-op).

## D13 — Procedural benchmark with analytic ground truth (2026-07)
Downloading garment datasets adds licence review and network dependence
to CI; a parametric skirt generator gives exact face labels and seam
paths, seeds reproduce failures, and difficulty is dialable (bulge,
noise, density, panel count). External datasets (Korosteleva & Lee,
CC BY 4.0) enter later for learned experiments.

## D14 — Experiments run through the CLI, not separate binaries (2026-07)
`experiments/` documents each experiment and drives `seamforge-cli`;
acceptance criteria live in TEST_STRATEGY and are enforced by the same
Catch2 suite. Avoids code drift between "experiment" and "production"
implementations while still keeping unmet-criteria work out of the
default pipeline.

## D15 — Schneider fitting with an arc-length re-fit loop (2026-07)
Boundary curves are fitted per corner-span with Schneider's
least-squares cubic algorithm (Graphics Gems I, public domain). Plain
Schneider bounds *deviation* but not *arc length*; sewing needs paired
seam lengths to agree, so after fitting each span we measure the
fitted-vs-raw arc-length error and re-fit with a 4× tighter tolerance
(up to 4 attempts) until it is ≤ 0.5%. Straight spans (line deviation
< 0.35·tol) become true lines. Corners are hard C0 break points —
garment patterns need exact corners at seam junctions, so smoothing
across them would be wrong. The raw polyline remains the revert layer
and the DXF export still uses polylines (R12 has no cubics).
