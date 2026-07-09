#!/usr/bin/env bash
# Experiment 5: independent-boundary seam matching on a pre-cut garment
# (two separate panel meshes in one file, no cut ancestry available).
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp5
mkdir -p "$OUT"
[ -f data/meshes/skirt_precut.obj ] || $GEN data/meshes

$CLI match --mesh data/meshes/skirt_precut.obj \
  --out "$OUT" --flattener arap --project "$OUT/precut.sfrproj"
$CLI roundtrip --project "$OUT/precut.sfrproj"
echo "matched seams with confidence + unmatched arcs reported above; SVG in $OUT"
