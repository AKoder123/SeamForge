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

- Script: `exp4_auto_segmentation.sh` — runs **two baselines**:
  the silhouette prior (`--baseline silhouette`) and a D-Charts-style
  quasi-developable chart grower (`--baseline dcharts`).
- Measured mean IoU vs construction ground truth:

  | case | silhouette | d-charts | d-charts panel count |
  |---|---|---|---|
  | skirt_simple | 1.000 | 0.939 | 2 (truth 2) |
  | skirt_noisy | 1.000 | 0.892 | 2 (truth 2) |
  | skirt_hard | 0.999 | 0.583 | 2 (truth 2) |
  | skirt_aline | — | 0.548 | 3 (truth 2) |
  | skirt_fourpanel | 0.500 | 0.477 | 2 (truth 4) |
  | tshirt_boxy | 0.468 | 0.873 | 4 (truth 2) |
  | trousers_flat | 0.480 | 0.705 | 2 (truth 2) |

- Failure analysis:
  - *Silhouette prior*: depends on exactly two dominant boundary loops
    and a widest-silhouette assumption — it hard-codes two side seams,
    so the four-panel skirt caps at IoU 0.5, the T-shirt (4 openings)
    degrades to 0.47, and on trousers (3 openings) it connects waist to one
    ankle and scores 0.48
    (KNOWN_LIMITATIONS #1).
  - *D-Charts*: developability is blind to construction. The whole
    frustum is one perfect cone, so the wrap-around cut position is
    arbitrary (0.94 on skirt_simple is seed luck, asserted < 0.95 in
    tests); curvature (A-line/hard) fragments or misplaces charts
    (0.55–0.58); it also cannot see the four construction panels of a
    smooth tube (0.48). Disk topology is enforced, so every chart is
    flattenable — useful as a fallback segmentation, not as a
    construction-panel predictor (RESEARCH.md §2.3).
  - Reports land in `out/experiments/exp4/*/auto_segmentation_report.json`.

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
