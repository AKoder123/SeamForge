# Experiments

Isolated, reproducible experiments driven through `seamforge-cli`
(see DECISION_LOG D14 — experiments share the production code paths so
results cannot drift from what ships). Run everything:

```bash
bash experiments/run_all.sh          # builds nothing; expects build/ to exist
```

Each experiment prints its metrics and writes artefacts under
`out/experiments/`. Acceptance criteria are enforced permanently by the
Catch2 suite (TEST_STRATEGY.md); the scripts here are the human-readable
demonstration.

## Experiment 1 — known-cut flattening
One manually segmented skirt panel → flattened 2D panel + distortion
statistics + SVG.

- Script: `exp1_known_cut_flattening.sh`
- Acceptance (all met):
  - no flipped triangles ✓ (measured 0)
  - reproducible output ✓ (SVG byte-identical across runs; asserted)
  - round-trip correspondence ✓ (`toOrigVertex` in the saved project maps
    every local vertex to its source vertex; verified by `roundtrip`)
  - documented distortion ✓ (`metrics.json`: mean angle 1.0000,
    mean |log area| ≈ 3e-10, boundary Δ ≈ 5e-9 %)

## Experiment 2 — deterministic two-panel skirt
Skirt mesh + manually supplied side seams → front/back panels, flattened
pieces, deterministic seam pairing, SVG.

- Script: `exp2_two_panel_skirt.sh`
- Measured: 2 panels (front/back), 0 flips, seam relations
  `cut-ancestry` confidence 1.0, 2D length mismatch ~1e-7 %,
  project round-trip IDENTICAL.

## Experiment 3 — assisted seam proposal
Clean skirt mesh, no seams supplied → ranked seam candidates with
confidence, editable in the GUI (proposals appear colour-coded by
confidence; select/delete/redraw supported).

- Script: `exp3_assisted_proposal.sh`
- Measured: 2 proposals, confidence 0.41 each (silhouette prior with no
  curvature support — honest), both valid edge paths connecting waist
  to hem.

## Experiment 4 — automatic segmentation baseline
Simple skirt mesh → predicted two-panel segmentation, compared against
labelled ground truth, with failure analysis.

- Script: `exp4_auto_segmentation.sh`
- Measured mean IoU: `skirt_simple` 1.000, `skirt_hard`
  (bulge 0.12 + noise) 0.999, `skirt_noisy` see report.
- Failure analysis: the proposer depends on exactly two dominant
  boundary loops and a widest-silhouette prior; distorted or additional
  openings degrade it (KNOWN_LIMITATIONS #1). Reports land in
  `out/experiments/exp4_*/auto_segmentation_report.json`.

## Experiment 5 — pre-cut boundary matching
Two separate panel meshes in one file (disconnected components, no cut
ancestry) → matched seam proposals with confidence, unmatched arcs
reported, flattened panels, SVG, project.

- Script: `exp5_precut_matching.sh`
- Measured: both side seams matched (score 1.0, confidence capped 0.9,
  reversed direction detected), waist/hem arcs correctly unmatched (4),
  2D seam-length mismatch ~1e-7 %, project round-trip IDENTICAL.
- Note: duplicate welding is disabled in this workflow (DECISION_LOG
  D16) so digitally-cut coincident boundaries survive import.

## Not yet run
- Learned experiments (panel count, seam scoring, NeuralTailor-style
  reconstruction) — deliberately deferred until after the deterministic
  baseline (this milestone). See ROADMAP #18.
