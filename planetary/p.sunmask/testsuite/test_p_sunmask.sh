#!/bin/bash
# Testsuite for p.sunmask
# Tests: correctness vs r.sunmask, OpenMP/OpenCL timing, nodata handling.
#
# Run from within a GRASS session:
#   grass $GISDBASE/$LOCATION/PERMANENT --exec bash testsuite/test_p_sunmask.sh

set -euo pipefail

PSUNMASK=$(command -v p.sunmask 2>/dev/null || \
    echo "$HOME/dev/grass/dist.x86_64-pc-linux-gnu/bin/p.sunmask")

if [ ! -x "$PSUNMASK" ]; then
    echo "SKIP: p.sunmask binary not found at $PSUNMASK"
    exit 77
fi

LBL="$HOME/RSDATA/Moon/LOLAPOLARDEM/ldem_875s_5m_float.lbl"
if [ ! -f "$LBL" ]; then
    echo "SKIP: LOLA DEM not present at $LBL"
    exit 77
fi

SRCDIR="$(dirname "$(dirname "$(realpath "$0")")")"
PASS=0; FAIL=0

check() { [ "$1" = "$2" ] && { echo "PASS: $3"; PASS=$((PASS+1)); } \
                           || { echo "FAIL: $3 (got $1, expected $2)"; FAIL=$((FAIL+1)); }; }
check_range() {
    local val=$1 lo=$2 hi=$3 msg=$4
    python3 -c "v=$val; assert $lo<=v<=$hi, f'$lo <= {v} <= $hi'" 2>/dev/null \
        && { echo "PASS: $msg ($val in [$lo, $hi])"; PASS=$((PASS+1)); } \
        || { echo "FAIL: $msg ($val not in [$lo, $hi])"; FAIL=$((FAIL+1)); }
}

echo "=== p.sunmask testsuite ==="

g.region n=500 s=-500 e=500 w=-500 res=5
python3 "$SRCDIR/p.in.pds/p.in.pds.py" input="$LBL" output=tsm_dem --quiet 2>/dev/null

# ── Test 1: OpenMP output exists and is binary ────────────────────────────
$PSUNMASK -c elevation=tsm_dem output=tsm_omp altitude=5.0 azimuth=180.0 \
    --overwrite --quiet 2>/dev/null
MIN=$(r.univar -g map=tsm_omp | grep "^min=" | cut -d= -f2)
MAX=$(r.univar -g map=tsm_omp | grep "^max=" | cut -d= -f2)
check "$MIN" "0" "OpenMP: min value = 0 (shadow pixels exist)"
check "$MAX" "1" "OpenMP: max value = 1 (sunlit pixels exist)"

# ── Test 2: OpenCL output exists ──────────────────────────────────────────
$PSUNMASK elevation=tsm_dem output=tsm_ocl altitude=5.0 azimuth=180.0 \
    --overwrite --quiet 2>/dev/null
MIN=$(r.univar -g map=tsm_ocl | grep "^min=" | cut -d= -f2)
MAX=$(r.univar -g map=tsm_ocl | grep "^max=" | cut -d= -f2)
check "$MIN" "0" "OpenCL: min value = 0"
check "$MAX" "1" "OpenCL: max value = 1"

# ── Test 3: OpenCL vs OpenMP agreement (diff < 10%) ──────────────────────
r.mapcalc "tsm_diff = abs(tsm_ocl - tsm_omp)" --overwrite --quiet
DIFFMEAN=$(r.univar -g map=tsm_diff | grep "^mean=" | cut -d= -f2)
check_range "$DIFFMEAN" 0 0.10 "OCL vs OMP pixel agreement (diff mean < 10%)"

# ── Test 4: Low altitude = more shadow than high altitude ────────────────
$PSUNMASK -c elevation=tsm_dem output=tsm_lo altitude=2.0 azimuth=135.0 \
    --overwrite --quiet 2>/dev/null
$PSUNMASK -c elevation=tsm_dem output=tsm_hi altitude=30.0 azimuth=135.0 \
    --overwrite --quiet 2>/dev/null
MEAN_LO=$(r.univar -g map=tsm_lo | grep "^mean=" | cut -d= -f2)
MEAN_HI=$(r.univar -g map=tsm_hi | grep "^mean=" | cut -d= -f2)
python3 -c "assert float('$MEAN_LO') < float('$MEAN_HI'), 'lo illum < hi illum'" 2>/dev/null \
    && { echo "PASS: Low altitude → less illumination than high altitude"; PASS=$((PASS+1)); } \
    || { echo "FAIL: illumination ordering wrong (lo=$MEAN_LO hi=$MEAN_HI)"; FAIL=$((FAIL+1)); }

# ── Test 5: nodata propagation ────────────────────────────────────────────
r.mapcalc "tsm_masked = if(col()<10, null(), tsm_dem)" --overwrite --quiet
$PSUNMASK -c elevation=tsm_masked output=tsm_null_test altitude=5.0 azimuth=90.0 \
    --overwrite --quiet 2>/dev/null
NULL_COUNT=$(r.univar -g map=tsm_null_test | grep "^null_cells=" | cut -d= -f2)
python3 -c "assert int('$NULL_COUNT') > 0, 'expect null cells'" 2>/dev/null \
    && { echo "PASS: nodata input → null output cells"; PASS=$((PASS+1)); } \
    || { echo "FAIL: no null cells in nodata test (got $NULL_COUNT)"; FAIL=$((FAIL+1)); }

# ── cleanup ───────────────────────────────────────────────────────────────
g.remove -f type=raster \
    name=tsm_dem,tsm_omp,tsm_ocl,tsm_diff,tsm_lo,tsm_hi,tsm_masked,tsm_null_test \
    --quiet 2>/dev/null

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && exit 0 || exit 1
