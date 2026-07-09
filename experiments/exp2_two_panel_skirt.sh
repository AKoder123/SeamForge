#!/usr/bin/env bash
# Experiment 2: deterministic two-panel skirt extraction with manual seams.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp2
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

$CLI pipeline --mesh data/meshes/skirt_simple.obj \
  --seams data/meshes/skirt_simple.seams.json \
  --out "$OUT" --flattener arap --project "$OUT/skirt.sfrproj" --heatmap
$CLI roundtrip --project "$OUT/skirt.sfrproj"
echo "front/back panels, deterministic pairing and SVG in $OUT"
