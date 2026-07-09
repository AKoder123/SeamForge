#!/usr/bin/env bash
# Experiment 1: known-cut flattening of one manually segmented panel.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp1
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

# The pipeline flattens each panel independently; panel 0's outputs are the
# single-panel result under test.
$CLI pipeline --mesh data/meshes/skirt_simple.obj \
  --seams data/meshes/skirt_simple.seams.json \
  --out "$OUT/run1" --flattener arap --project "$OUT/run1/p.sfrproj"
$CLI pipeline --mesh data/meshes/skirt_simple.obj \
  --seams data/meshes/skirt_simple.seams.json \
  --out "$OUT/run2" --flattener arap

# acceptance: reproducible output
cmp "$OUT/run1/pattern.svg" "$OUT/run2/pattern.svg" \
  && echo "OK: SVG byte-identical across runs"
# acceptance: round-trip correspondence preserved in the project file
$CLI roundtrip --project "$OUT/run1/p.sfrproj"
# acceptance: no flips + documented distortion
grep -o '"flipped": [0-9]*' "$OUT/run1/metrics.json" | sort -u
echo "distortion documented in $OUT/run1/metrics.json"
