#!/usr/bin/env bash
# Experiment 4: automatic segmentation baseline vs labelled ground truth.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp4
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

for case in skirt_simple skirt_noisy skirt_hard skirt_fourpanel; do
  for baseline in silhouette dcharts; do
    echo "--- $case / $baseline ---"
    $CLI auto --mesh "data/meshes/$case.obj" \
      --truth "data/meshes/$case.gt.json" \
      --out "$OUT/$case-$baseline" --baseline "$baseline" || true
  done
done
echo "reports: $OUT/*/auto_segmentation_report.json (failure analysis in experiments/README.md)"
