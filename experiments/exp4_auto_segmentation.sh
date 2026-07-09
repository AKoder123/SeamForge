#!/usr/bin/env bash
# Experiment 4: automatic segmentation baseline vs labelled ground truth.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp4
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

for case in skirt_simple skirt_noisy skirt_hard; do
  echo "--- $case ---"
  $CLI auto --mesh "data/meshes/$case.obj" \
    --truth "data/meshes/$case.gt.json" --out "$OUT/$case" || true
done
echo "reports: $OUT/*/auto_segmentation_report.json (failure analysis in experiments/README.md)"
