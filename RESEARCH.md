# Research Review — Reverse Pattern Reconstruction from 3D Garments

Scope: methods relevant to reconstructing 2D construction panels from a
completed 3D garment mesh. For each method: inputs, outputs, assumptions,
strengths, weaknesses, cost, licensing, implementation availability, and
suitability for the **first skirt milestone** (two-panel skirt →
front/back panels → flattening → seam pairing → SVG).

All statements below are from the primary literature and public
implementations; nothing here derives from proprietary CLO3D internals.

---

## 1. Computational pattern making from 3D garment models

### 1.1 Classical "flattening for apparel" line
- **McCartney, Hinds, Seow — *The flattening of triangulated surfaces incorporating darts and gussets* (CAD 1999)**; **Wang, Smith, Yuen — *Surface flattening based on energy model* (CAD 2002)**.
- Inputs: segmented 3D surface patch. Outputs: 2D pattern with strain
  metrics, optional darts. Assumptions: patch is nearly developable or
  darts absorb the Gaussian curvature.
- Strengths: directly aimed at garment panels; introduces
  strain-energy-based flattening (a precursor of ARAP).
- Weaknesses: no panel/seam detection — segmentation is assumed given;
  older mesh-quality assumptions.
- Cost: low (sparse solves / relaxation). No maintained public code.
- Skirt milestone: conceptually the blueprint (segment-then-flatten);
  superseded numerically by LSCM/ARAP/BFF.

### 1.2 Decaudin et al. — *Virtual Garments: A Fully Geometric Approach for Clothing Design* (EG 2006)
Forward direction (sketch → garment → pattern by developable
approximation). Confirms that garment panels are deliberately
near-developable — the core geometric prior our reverse pipeline exploits.

### 1.3 Bang, Korosteleva, Lee — *Estimating Garment Patterns from Static Scan Data* (CGF 2021)
- Inputs: single static scan of a dressed body; garment template priors.
- Outputs: parametric sewing patterns fitted by optimisation
  (pattern parameters → drape → compare).
- Assumptions: garment class known; template exists.
- Strengths: end-to-end scan→pattern; simulation in the loop.
- Weaknesses: template-bound; heavy (repeated cloth simulation).
- Availability: paper; partial research code. Licence: research-only.
- Skirt milestone: too heavy; validates our "resimulation validation"
  roadmap stage instead.

---

## 2. Garment-specific surface segmentation

### 2.1 Curvature/feature-based mesh segmentation (generic)
- **Shlafman, Tal, Katz (k-means face clustering)**; **Katz & Tal
  (hierarchical fuzzy clustering + cuts, SIGGRAPH 2003)**; **Shapira et al.
  SDF-based**; survey: **Shamir, *A survey on mesh segmentation techniques*
  (CGF 2008)**.
- Inputs: manifold mesh. Outputs: face partition.
- Assumptions: parts are separated by concave creases (minima rule).
- Weakness for garments: construction seams are frequently **not**
  geometric creases (pressed-open side seams on a smooth skirt are
  geometrically invisible), and drape folds create creases that are not
  seams. This is the central failure mode; it is why our design treats
  automatic proposals as low-confidence and keeps the human in the loop.
- Skirt milestone: not sufficient alone; used only as evidence features
  (dihedral deviation) feeding candidate scoring.

### 2.2 Geodesic / spectral segmentation
- Spectral clustering on the cotan Laplacian (Liu & Zhang 2004); geodesic
  distance features (Hilaga et al. Reeb graphs).
- Strengths: global, topology-aware; finds tubular decompositions
  (sleeves vs torso) well.
- Weaknesses: k must be chosen; boundaries are not construction seams;
  eigen-solves cost O(n·k) memory and can be slow on dense scans.
- Skirt milestone: overkill for two panels; candidate for sleeve/torso
  splitting later (roadmap stage 16+).

### 2.3 Developability-driven segmentation
- **Julius, Kraevoy, Sheffer — *D-Charts: quasi-developable mesh
  segmentation* (EG 2005)**: grows charts that stay within a
  developability proxy threshold.
- **Stein, Grinspun, Crane — *Developability of triangle meshes*
  (SIGGRAPH 2018)**: variational definition of discrete developability;
  drives meshes toward piecewise-developable with emergent creases.
- Inputs: manifold mesh (+ threshold). Outputs: charts that each flatten
  with bounded distortion — exactly the property a sewing panel needs.
- Strengths: directly encodes "panels are developable pieces";
  D-Charts is simple (region growing + fitting cones).
- Weaknesses: chart boundaries are optimised for distortion, not for
  garment construction conventions (may zig-zag across the surface;
  may split a princess-seam panel arbitrarily); sensitive to thresholds.
- Cost: D-Charts is cheap; Stein et al. is a heavier optimisation.
- Licensing/availability: D-Charts — no official code (reimplementable
  from paper); Stein et al. — reference code (MIT) exists.
- Skirt milestone: the **best automatic baseline concept** after the
  silhouette prior; planned as the second `experiments/` segmentation
  baseline (see ROADMAP stage 16).

---

## 3. Curvature-based seam candidate detection
- Ridge/valley extraction: **Ohtake, Belyaev, Seidel (SIGGRAPH 2004)**
  crest lines via implicit fitting; simpler: per-edge dihedral deviation
  thresholds + hysteresis linking (Canny-style on meshes).
- Inputs: mesh (+ curvature estimates). Outputs: feature polylines.
- Assumptions: seams leave geometric traces (topstitching ridge, pressed
  crease, slight normal discontinuity).
- Reality check: true for denim topstitched seams and scanned garments
  with visible stitching; false for smooth simulated garments (our
  frustum skirt has literally zero curvature signal at the side seams).
- Cost: linear; trivial to add.
- Skirt milestone: implemented as evidence (`CurvatureField::dihedral`)
  feeding proposal confidence — deliberately **not** trusted alone.

## 4. Symmetry & garment-class priors
- Global extrinsic symmetry detection: **Mitra, Guibas, Pauly — *Partial
  and approximate symmetry detection* (SIGGRAPH 2006)**.
- Garment priors: side seams at silhouette extremes; front/back symmetry
  about the sagittal plane; vertical grain. These conventions come from
  standard pattern-making practice (public domain knowledge, e.g.
  Armstrong, *Patternmaking for Fashion Design*).
- Skirt milestone: **this is what actually works** on a clean skirt —
  our proposer (silhouette-extreme geodesics between waist and hem loops)
  achieves IoU ≈ 1.0 on the clean benchmark precisely because the prior,
  not the geometry, carries the information. Confidence is capped (0.35
  base + curvature support) to reflect that it is a prior, not evidence.

---

## 5. Flattening / parameterisation methods

### 5.1 LSCM — Lévy, Petitjean, Ray, Maillot (SIGGRAPH 2002)
- Inputs: disk-topology triangle patch, ≥2 pinned vertices.
- Outputs: conformal (angle-preserving) UV.
- Assumptions: none beyond disk topology; linear least squares.
- Strengths: fast, robust, no initialisation, deterministic.
- Weaknesses: area distortion unbounded (scale drifts across the patch);
  free-boundary solution can self-overlap on curved patches; pins
  influence the result.
- Cost: one sparse LDLT solve (~n unknowns ×2).
- Availability: libigl (`igl::lscm`, MPL2), OpenNL/Blender (GPL),
  Graphite. Reimplemented natively here (~120 lines, Eigen).
- Skirt milestone: **implemented** — used as ARAP initialiser and as a
  selectable flattener.

### 5.2 ABF / ABF++ — Sheffer & de Sturler 2001; Sheffer, Lévy et al. (TOG 2005)
- Inputs: disk patch. Outputs: per-corner angles then reconstructed UV;
  near-optimal conformal quality.
- Strengths: better conditioning than LSCM on extreme patches.
- Weaknesses: nonlinear (Newton on angle space); reconstruction step can
  reintroduce error; more code; patent history around ABF variants has
  expired but implementations are mostly GPL (Blender).
- Cost: several Newton iterations of sparse solves.
- Skirt milestone: not implemented; LSCM+ARAP already reach distortion
  ≪ thresholds on garment panels. Documented as alternative.

### 5.3 ARAP parameterisation — Liu, Zhang, Gotsman, Gortler (SGP 2008)
- Inputs: disk patch + initial UV (LSCM). Outputs: as-rigid-as-possible
  UV balancing angle and area distortion.
- Assumptions: local/global iterations converge (monotone energy).
- Strengths: near-isometric on developable patches — ideal for sewing
  panels; simple (per-triangle 2×2 SVD + one prefactored Poisson solve
  per iteration); preserves scale (UV in metres).
- Weaknesses: local minima (init-dependent); no bijectivity guarantee
  (flips possible on pathological patches — we count and report them).
- Cost: prefactor once + ~10–60 cheap iterations.
- Availability: libigl (MPL2). Reimplemented natively here.
- Skirt milestone: **implemented, default flattener**. Measured on the
  developable skirt panel: mean angle distortion 1.0000, mean |log area|
  ≈ 3e-10, boundary length change ≈ 5e-9 %.

### 5.4 Boundary-First Flattening — Sawhney & Crane (TOG 2017)
- Inputs: disk patch (+ optional target boundary lengths/angles).
- Outputs: conformal map with **direct boundary control**; bijective
  variant available.
- Strengths: boundary length/angle prescription is exactly what
  seam-length-preserving flattening wants; very fast (a few linear
  solves); reference implementation (MIT licence) exists.
- Weaknesses: conformal family only (area distortion still free);
  cone/dart insertion needs extra machinery.
- Skirt milestone: not needed yet; **top candidate** for the
  "seam-length-compatible flattening" roadmap item because target
  boundary lengths can equalise paired seams. Interface slot exists
  (`IFlattener`).

### 5.5 Distortion-minimising / bijective parameterisation
- **SLIM** (Rabinovich et al. 2017): scalable inversion-free descent on
  symmetric Dirichlet energy — guarantees no flips if init is flip-free.
- **Progressive/Simplicial embeddings**, **Tutte + untangling** lines.
- Skirt milestone: ARAP suffices; SLIM is the documented upgrade path if
  flipped triangles ever appear on real scans (KNOWN_LIMITATIONS #7).

### 5.6 Grain- and material-aware (anisotropic) flattening
- Woven cloth is stiff along warp/weft, shears on the bias → flattening
  should penalise shear relative to grain, not isotropic stretch.
- Research: anisotropic strain energies in cloth simulation
  (Volino et al.); grain-line constraints are standard practice in
  apparel CAD. Public parameterisation work with explicit fabric
  anisotropy is thin; typically done by scaling the metric before an
  isometric method.
- Skirt milestone: deferred. Data model reserves per-panel grain
  direction (the deterministic 2D frame already gives a stable vertical
  axis to interpret as grain).

---

## 6. Learned garment reconstruction

### 6.1 NeuralTailor — Korosteleva & Lee (SIGGRAPH 2022)
- Inputs: garment point cloud. Outputs: structured sewing pattern
  (panel polygons + stitch list), via hybrid LSTM/attention over a
  learned garment-token representation.
- Dataset: *Dataset of 3D garments with sewing patterns* (Korosteleva &
  Lee 2021) — ~23k synthetic garments, **MIT-licensed generator,
  CC BY 4.0 data** → usable for our future experiments.
- Strengths: recovers panel **topology** and stitches, generalises
  across garment types.
- Weaknesses: synthetic-drape domain gap; fixed garment-token vocabulary;
  no hard guarantee panels sew consistently; needs GPU training.
- Skirt milestone: excluded by design (deterministic first). Roadmap
  stage 18 keeps an `ILearnedProposer` slot; the dataset is the natural
  benchmark extension beyond procedural skirts.

### 6.2 Sewformer — Liu et al. (SIGGRAPH Asia 2023), *Towards Garment Sewing Pattern Reconstruction from a Single Image*
Transformer from a single image to panel+stitch tokens. Confirms the
"structured pattern as token sequence" trend. Image input is out of
scope for our mesh-first pipeline; relevant later for photo bootstrap.

### 6.3 ISP — Li et al. (NeurIPS 2023), *ISP: Multi-Layered Garment Draping with Implicit Sewing Patterns*
Implicit per-panel SDF pattern representation + learned draping.
Relevant as an intermediate representation idea (panel as implicit 2D
region with UV-to-3D map) — mirrors our Panel/UV correspondence design.

### 6.4 Differentiable cloth-based inverse pattern optimisation
- **DiffCloth** (Li et al. 2022, projective-dynamics-based, MIT code),
  **DiffXPBD**, Taichi-based simulators; garment-specific:
  *Inverse Garment Design* lines (e.g. Montes et al. 2020 computational
  pattern design with in-the-loop simulation).
- Concept: parameterise 2D patterns, simulate drape, backprop
  surface-distance loss to pattern parameters.
- Strengths: directly optimises the objective we ultimately care about
  (resimulated garment ≈ input garment); refines panel shapes beyond
  what geometry alone determines (ease, darts).
- Weaknesses: expensive, fragile contact gradients, needs good init —
  i.e. needs exactly the deterministic pipeline we built as the starting
  point.
- Skirt milestone: roadmap stage 17/18 (validation first with forward
  simulation only).

### 6.5 Topology-aware panel/seam prediction
- Graph neural networks over mesh segments predicting stitch adjacency
  (NeuralTailor's stitch head; Sewformer's edge tokens). Documented as
  the learned upgrade for our deterministic cut-ancestry pairing when
  seams are inferred rather than cut.

---

## 7. Structured intermediate garment representations
- Korosteleva & Lee's JSON sewing-pattern spec (panels as 2D polygons +
  edge curves + stitch pairs) — de-facto research standard, MIT/CC;
  our `.sfrproj` schema is deliberately isomorphic to it (panels,
  boundary curves, stitch/seam relations with orientation) so a future
  converter is mechanical. We do **not** read/write CLO3D formats.

## 8. Simulation-based validation
- Forward simulators: **ARCSim** (research licence), **Project Chrono**
  (BSD-3), **XPBD cloth** (easy to implement, stable), Blender cloth
  (GPL, usable as an external oracle via Python scripting), **C-IPC/IPC**
  (robust contact, slower, MIT).
- Metrics from the reconstruction literature: bidirectional Chamfer
  distance, normal consistency, silhouette IoU (NeuralTailor eval);
  seam-length consistency; panel distortion budgets.
- Skirt milestone: deferred by design (ROADMAP stage 17): assemble
  panels by pinning paired seam vertices (we have exact vertex-level
  correspondence), relax with XPBD, measure Chamfer to source. No
  production simulator required for the first version.

---

## 9. What this review changed in the design
1. **Segmentation cannot be trusted automatically** on smooth garments →
   semi-automatic first, confidence surfaced, manual tools primary.
2. **ARAP (init LSCM) is the right first flattener** for near-developable
   panels; BFF is the planned upgrade for seam-length control; SLIM is
   the fallback if flips appear.
3. **Cut-ancestry makes seam pairing deterministic** — segmentation and
   correspondence must be one system, not two.
4. **Panels-as-structured-JSON** (à la Korosteleva & Lee) is the right
   project format; enables later learned experiments and dataset reuse.
5. **Developability is the key measurable prior** — distortion metrics
   double as "was this segmentation plausible" scores.
