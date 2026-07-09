#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for s in experiments/exp*.sh; do
  echo "==== $s ===="
  bash "$s"
done
echo "==== all experiments completed ===="
