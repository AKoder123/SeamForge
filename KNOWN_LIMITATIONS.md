# Known Limitations

Honest accounting of what the current system does not do, with failure
modes observed during development.

1. **Seam proposal is prior-driven.** On smooth garments the side seams
   leave no geometric trace; the proposer finds silhouette-extreme
   geodesics between the two largest boundary loops. It reports low
   confidence (~0.41 on the clean skirt) by design. It will misfire on:
   garments whose widest silhouette is not at the seams (wrap skirts),
   garments with ≠2 openings (trousers have 3, shirts 4), asymmetric
   drape. Fix path: more priors per garment class + curvature/texture
   evidence + eventually learned scoring (ROADMAP #7).

2. **Segmentation requires seam endpoints anchored** on the mesh
   boundary or another seam. Dangling cuts are detected and reported,
   not auto-extended. Auto-extension to the nearest boundary is a
   planned convenience.

3. **Seam paths live on mesh edges.** Drawing snaps to shortest edge
   paths, not true geodesics; on coarse meshes seams zig-zag (visible on
   `skirt_noisy`). Fix path: edge-subdivision or exact polyline-on-mesh
   cutting (needs vertex insertion machinery in Segmentation).

4. **Relations for seams that don't separate two panels are flagged,
   not resolved.** A single cut on a tube (both sides on one panel) or a
   seam bordering >2 panels yields confidence 0.3 + note. Darts (a seam
   pairing with itself) therefore parse but aren't specially handled yet.

5. **Boundary matching is whole-arc and geometry-driven.** The matcher
   (`proposeBoundaryMatches`, `seamforge-cli match`) pairs boundary arcs
   split at 3D corners; it does not split arcs mid-way, so partial seam
   matches (one long edge sewn to two short ones) are not found. Panels
   must lie near their sewn position: separated or flattened-out panel
   layouts defeat the proximity score. Closed loops (no corners) match
   with unresolved orientation (confidence capped at 0.5). Also note the
   GUI import path welds exactly-coincident duplicate vertices, which
   sews digitally pre-cut panels back together before matching can run -
   use `seamforge-cli match` (welding disabled, DECISION_LOG D16) for
   such inputs.

6. **ARAP does not guarantee flip-free output.** Zero flips on the whole
   benchmark, but pathological scans could flip; flips are counted and
   reported. Fallback documented: SLIM-style inversion-free descent.

7. **normalizeFrame uses vertex PCA** — panels with heavily asymmetric
   sampling could get a slightly rotated "vertical". Purely cosmetic
   (frame is still deterministic); grain-aware orientation is future
   work.

8. **Curve editing is not yet curve-native.** Boundaries are fitted with
   cubic Béziers (Schneider) between preserved corners — straight spans
   stay true lines, arc-length error is held under the 0.5% seam budget,
   and the raw polyline is always kept for revert. But the GUI's
   draggable control points still edit the simplified *polyline*; moving
   one does not re-fit the curves (re-running Flatten does). Direct
   manipulation of Bézier handles is future work. A smooth closed loop
   with no detected corners is split at two arbitrary kept points, so
   the join there is C0, not smooth.

9. **Validation heuristics are heuristics.** Small-hole vs garment
   opening uses a length ratio (5%); scale check assumes metres;
   orientation check assumes +Y up. All report-only (no destructive
   action), but real scans will need per-case judgement.

10. **DXF export is a minimal R12 ENTITIES stream.** Opens in
    LibreCAD/QCAD; strict consumers wanting full HEADER/TABLES sections
    may reject it. SVG is the primary export.

11. **No body/multi-layer handling.** Input must be a clean garment-only
    single-layer mesh, per product scope. Body removal is a future
    subsystem.

12. **GUI interaction is manually tested.** `--smoke`/`--screenshot`
    verify startup and rendering headlessly; drag/pick flows have no
    scripted QTest coverage yet. Also: panel-boundary control points are
    drag-editable, but there is no snap/constraint system, and the seam
    *3D-drawing* editing (extend/shorten existing seam) is
    delete-and-redraw rather than in-place editing.

13. **Benchmark covers skirts only** (6 variants). T-shirts, trousers,
    dresses, darts, curved seams are specified in TEST_STRATEGY but not
    yet generated.

14. **Undo granularity is whole-document snapshots** (project JSON).
    Simple and correct, but memory-heavy for very large scans (capped at
    64 snapshots).

15. **`Seam.id` collisions across sources are possible in the CLI** if a
    seams file supplies duplicate ids — the file's ids are trusted.
    The GUI always allocates fresh ids.

16. **Non-manifold meshes are rejected, not repaired.** By policy
    (destructive repair must not be silent); a guided repair tool is
    future work.
