# Data Models

## Core in-memory types (`src/core`)

### TriMesh
- `V: vector<Vector3d>` — positions, metres.
- `F: vector<array<int,3>>` — CCW (outward) vertex indices.
- Derived (rebuilt via `buildAdjacency()`): `edges` (undirected, v0<v1,
  incident-face list preserving non-manifold fans), `faceEdges`,
  `vertexFaces`, `vertexEdges`.
- Invariant: algorithms may only rely on adjacency when
  `adjacencyValid()`; topology edits must invalidate.

### Seam
- `id` — stable within a project; never reused after deletion
  (`AppState::nextSeamId` is max+1).
- `vertices` — ordered vertex path; consecutive entries share a mesh
  edge; closed if `front()==back()`.
- `source` — `Manual | Proposed | MeshBoundary`; `confidence` ∈ (0,1]
  (1.0 = user-confirmed); `evidence` — human-readable support.
- Validity (checked by `validateSeamPath`): ≥2 vertices, edges exist,
  no interior revisits.

### Panel
- `id` — stable, deterministic (sorted by first original face id).
- `label` — `front|back|sleeve|collar|unknown`, user-editable.
- Local mesh `V/F` plus **exact correspondence**:
  - `toOrigV[localVertex] = originalVertex`
  - `origFace[localFace] = originalFace` (== `faces`)
  - vertices on cut seams appear once **per panel** (duplication is the
    cut).
- `boundaryLoopsLocal` — ordered local vertex loops.
- `segments: vector<BoundarySegment{seamId, localVerts}>` — the boundary
  loop split into maximal runs by ancestry; `seamId == -1` means the run
  came from the original mesh boundary (waist/hem), otherwise it names
  the seam whose cut created it.
- `UV: vector<Vector2d>` — filled by flattening; metres; deterministic
  frame (no majority flip → principal axis vertical → sign convention →
  min corner at origin).

### SeamRelation
- `seamId`, sides `a`/`b` = `{panelId, localVerts}` where `localVerts`
  is ordered **along the original seam path** — index i on side a is
  sewn to index i on side b (vertex-exact).
- `reversed` — user-facing sewing-direction toggle (display/export
  semantics; correspondence order itself never mutates).
- `confidence` (1.0 for cut ancestry), `source` (`cut-ancestry`),
  `length3d`, `lengthMismatch2d` (|lenA−lenB|/max, after flattening),
  `note` (ambiguity: non-separating seam, >2 panels, partial match).

### RegularizedLoop
- `raw` — the untouched flattened boundary polyline (revert target).
- `keptIdx` → `simplified`; `isCorner` per kept point; `isStraight` per
  kept segment; `maxDeviation` — measured raw↔simplified bound.
- `curves: vector<BezierSegment{p0,c0,c1,p1,isLine}>` — smooth outline:
  ordered closed chain of cubic Béziers/true lines fitted between
  corners (Schneider least-squares with adaptive splitting); filled by
  `fitLoopCurves`. `curveMaxDeviation` (fit accuracy) and
  `curveMaxLengthError` (worst per-span relative arc-length error;
  fitting re-subdivides until ≤ 0.5% for seam-length compatibility)
  are measured, not assumed.

### Distortion
- Per face: singular values σ1 ≥ σ2 of the 3D→2D Jacobian;
  `flipped = det ≤ 0`; angle distortion σ1/σ2; area ratio σ1·σ2
  (1 = isometric, absolute because UV is metric).
- Summary: flips, area-weighted mean/max angle distortion, mean
  |log area|, min/max area ratio, max stretch σ1, max compression σ2,
  worst relative boundary-length change.

## Project file format `.sfrproj` (schema v1)

Self-contained JSON; loaders reject `schemaVersion` > supported.

```jsonc
{
  "format": "seamforge-reverse-project",
  "schemaVersion": 1,
  "units": "m",
  "sourcePath": "data/meshes/skirt_simple.obj",   // informational
  "flattener": "arap",
  "validationText": "...",                         // last report, verbatim
  "mesh":   { "vertices": [[x,y,z],...], "faces": [[a,b,c],...] },
  "seams":  [ { "id": 0, "vertices": [v0,v1,...],
                "source": "manual|proposed|mesh-boundary",
                "confidence": 1.0, "evidence": "..." } ],
  "panels": [ { "id": 0, "label": "front",
                "vertices": [[x,y,z],...], "faces": [[a,b,c],...],
                "toOrigVertex": [...], "origFaces": [...],
                "uv": [[u,v],...],
                "segments": [ { "seamId": 0, "localVerts": [...] } ],
                "boundaryLoops": [[...]] } ],
  "relations": [ { "seamId": 0,
                   "panelA": 0, "vertsA": [...],
                   "panelB": 1, "vertsB": [...],
                   "reversed": false, "confidence": 1.0,
                   "source": "cut-ancestry",
                   "length3d": 0.632, "lengthMismatch2d": 1e-9,
                   "note": "" } ],
  "regularized": [ [ { "raw": [[u,v],...], "keptIdx": [...],
                       "isCorner": [...], "isStraight": [...],
                       "maxDeviation": 0.002,
                       // present when curves were fitted:
                       "curves": [ { "p0": [u,v], "c0": [u,v],
                                     "c1": [u,v], "p1": [u,v],
                                     "line": false } ],
                       "curveMaxDeviation": 0.006,
                       "curveMaxLengthError": 0.0019 } ] ]  // per panel, per loop
}
```

Guarantees (tested): save→load→save is byte-identical
(`seamforge-cli roundtrip`); wrong `format` or newer schema fails with a
message. GUI undo snapshots are exactly this JSON.

## Seam input format (CLI `--seams`)

```json
{ "seams": [ { "id": 0, "vertices": [0, 64, 128, ...] } ] }
```
Vertex indices refer to the mesh file's vertex order — this is why .obj
files are parsed natively (Assimp re-indexes OBJ vertices).

## Ground-truth format (benchmark, `*.gt.json`)

```json
{ "panelCount": 2, "faceLabel": [0,0,1,...], "seamPaths": [[...],[...]] }
```

## SVG export conventions
- Units mm (`unitsToMm = 1000`), y-down document space.
- Per panel `<g id="panel-N" data-label="...">`: optional per-triangle
  angle-distortion heatmap, raw boundary (grey 0.3), regularised outline
  (black 0.6) — a `<path>` with `L`/`C` commands when Bézier curves are
  fitted, else a `<polygon>` — with corner dots, seam labels `S<n>` at
  segment midpoints,
  panel label + bbox dimensions; document footer lists seam relations
  with confidence and length mismatch.
- Fixed 3-decimal precision → deterministic, diffable output.

## DXF export
Minimal R12 `ENTITIES`-only: one closed `POLYLINE` per boundary loop on
layer `PANEL_<id>`, regularised outline when present, mm units. Reads
back in LibreCAD/QCAD (header-less R12 is widely accepted); a fuller
header block is a known limitation (#10).
