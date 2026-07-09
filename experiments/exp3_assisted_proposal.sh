#!/usr/bin/env bash
# Experiment 3: assisted seam proposal on a clean skirt with no seams given.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=build/src/tools/seamforge-cli
GEN=build/src/tools/make-test-meshes
OUT=out/experiments/exp3
mkdir -p "$OUT"
[ -f data/meshes/skirt_simple.obj ] || $GEN data/meshes

$CLI propose --mesh data/meshes/skirt_simple.obj --out "$OUT/proposed.json"
echo "--- ranked candidates with confidence ---"
grep -o '"confidence": [0-9.]*' "$OUT/proposed.json"
# proposals are directly usable (and editable in the GUI / as JSON):
$CLI pipeline --mesh data/meshes/skirt_simple.obj \
  --seams "$OUT/proposed.json" --out "$OUT/from_proposals" --flattener arap
