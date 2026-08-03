#!/usr/bin/env bash
set -euo pipefail

RUNS=${RUNS:-20}
BIN=${BIN:-./bench}

for _ in $(seq "$RUNS"); do
    stdbuf -oL "$BIN" 2>/dev/null | grep -oP '[0-9.]+(?= Mmsg/s)' || true
done | sort -n | awk '
{ a[NR] = $1 }
END {
    n = NR
    med = (n % 2) ? a[(n+1)/2] : (a[n/2] + a[n/2+1]) / 2
    printf "runs   %d\nmin    %.2f\nmedian %.2f\nmax    %.2f\n", n, a[1], med, a[n]
}'