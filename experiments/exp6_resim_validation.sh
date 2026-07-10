#!/usr/bin/env bash
# Experiment 6: reconstruction validation (stage 17 prototype).
# Reassembles the panels from the 2D pattern metric (UV rest lengths +
# seam pinning), relaxes, and compares against the source garment.
# The corrupted run demonstrates that inconsistent patterns are detected.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp6
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

$CLI pipeline --mesh data/meshes/skirt_simple.obj \
  --seams data/meshes/skirt_simple.seams.json \
  --out "$OUT/pipe" --flattener arap --project "$OUT/skirt.sfrproj" >/dev/null

echo "--- consistent pattern (expect ~0 drift, exit 0) ---"
$CLI resim --project "$OUT/skirt.sfrproj" --out "$OUT/consistent"

echo "--- corrupted pattern: panel 0 scaled x1.06 (expect drift, exit 2) ---"
if $CLI resim --project "$OUT/skirt.sfrproj" --out "$OUT/corrupted" --corrupt 1.06; then
  echo "ERROR: corrupted pattern passed the acceptance gate" >&2
  exit 1
else
  echo "corrupted pattern correctly rejected by the acceptance gate"
fi
