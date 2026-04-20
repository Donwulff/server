#!/usr/bin/env bash
#
# Compare two bench.sh result directories.
#
# Usage:
#   compare.sh <baseline-dir> <patched-dir>
#
# Emits a Markdown table with median TPS / QPS / p95 latency for each
# (test, threads) pair, showing absolute values and % change.  % change
# is (patched - baseline) / baseline * 100 — positive means patched is
# faster for TPS/QPS, slower for latency.

set -euo pipefail

[ $# -eq 2 ] || { echo "usage: $0 <baseline-dir> <patched-dir>" >&2; exit 1; }

BASE=$1
PATCHED=$2

for d in "$BASE" "$PATCHED"; do
  [ -f "$d/medians.csv" ] || { echo "missing $d/medians.csv" >&2; exit 1; }
done

awk -v base="$BASE/medians.csv" -v patched="$PATCHED/medians.csv" '
  BEGIN {
    FS=","
    while ((getline line < base) > 0) {
      n = split(line, f, ",")
      if (f[1] == "test") continue
      k = f[1] "|" f[2]
      b_tps[k] = f[3]; b_qps[k] = f[4]
      b_avg[k] = f[5]; b_p95[k] = f[6]
    }
    while ((getline line < patched) > 0) {
      n = split(line, f, ",")
      if (f[1] == "test") continue
      k = f[1] "|" f[2]
      p_tps[k] = f[3]; p_qps[k] = f[4]
      p_avg[k] = f[5]; p_p95[k] = f[6]
      keys[k] = 1
    }
    printf("| test | threads | baseline TPS | patched TPS | dTPS%% | baseline p95 | patched p95 | dp95%% |\n")
    printf("|------|---------|--------------|-------------|-------|--------------|-------------|-------|\n")
    n = 0
    for (k in keys) ks[++n] = k
    # sort by test then threads
    for (i = 1; i < n; i++) for (j = i+1; j <= n; j++) {
      ia = ks[i]; jb = ks[j]
      split(ia, a, "|"); split(jb, b, "|")
      if (a[1] > b[1] || (a[1] == b[1] && a[2]+0 > b[2]+0)) {
        tmp = ks[i]; ks[i] = ks[j]; ks[j] = tmp
      }
    }
    for (i = 1; i <= n; i++) {
      k = ks[i]; split(k, f, "|")
      dtps = (p_tps[k]-b_tps[k])/b_tps[k]*100
      dp95 = (p_p95[k]-b_p95[k])/b_p95[k]*100
      printf("| %s | %s | %.2f | %.2f | %+.2f%% | %.3f | %.3f | %+.2f%% |\n",
        f[1], f[2], b_tps[k], p_tps[k], dtps, b_p95[k], p_p95[k], dp95)
    }
  }'
