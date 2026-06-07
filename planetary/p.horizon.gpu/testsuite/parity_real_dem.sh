#!/bin/sh
# parity_real_dem.sh — compare p.horizon.gpu against r.horizon on a real
# LDEM clip, side by side, at a handful of azimuths. Reports per-azimuth
# diff stats and overall pass/fail.
#
# Run inside a GRASS session that holds the DEM, e.g.:
#
#   cd ~/dev/r_Landing_Planet/p.horizon.gpu
#   make -f Makefile.standalone       # produce ./p.horizon.gpu
#   grass ~/grassdata/Moon_SouthPole_5m/artemis_connecting_ridge --exec \
#       sh parity_real_dem.sh ldem_875s_5m_float 500 5000 \
#                             1737400 0,45,90,135,180,225,270,315
#
# Args: $1=dem  $2=clip_cells  $3=maxdist_m  $4=body_R_m  $5=az_list_csv
set -e

DEM=${1:?dem name}
CLIP=${2:-500}
MAXD=${3:-5000}
BODYR=${4:-1737400}
AZS=${5:-0,45,90,135,180,225,270,315}
THRESH_DEG=${THRESH_DEG:-0.5}

BIN="$(dirname "$0")/p.horizon.gpu"
[ -x "$BIN" ] || { echo "ERR: $BIN not built — run 'make -f Makefile.standalone' first"; exit 2; }

echo "DEM:       $DEM"
echo "Clip:      ${CLIP}x${CLIP}"
echo "max_dist:  ${MAXD} m"
echo "body_R:    ${BODYR} m"
echo "Azimuths:  $AZS"
echo "Threshold: ${THRESH_DEG}°"

# 1. Centred clip
eval "$(g.region -gp raster=$DEM | sed -n 's/^\(n\|s\|e\|w\|nsres\|ewres\)=/\1=/p')"
cy=$(awk -v a=$n -v b=$s 'BEGIN{printf "%.6f", (a+b)/2}')
cx=$(awk -v a=$e -v b=$w 'BEGIN{printf "%.6f", (a+b)/2}')
hn=$(awk -v c=$CLIP -v r=$nsres 'BEGIN{printf "%.6f", c*r/2}')
he=$(awk -v c=$CLIP -v r=$ewres 'BEGIN{printf "%.6f", c*r/2}')
n2=$(awk -v c=$cy -v h=$hn 'BEGIN{printf "%.6f", c+h}')
s2=$(awk -v c=$cy -v h=$hn 'BEGIN{printf "%.6f", c-h}')
e2=$(awk -v c=$cx -v h=$he 'BEGIN{printf "%.6f", c+h}')
w2=$(awk -v c=$cx -v h=$he 'BEGIN{printf "%.6f", c-h}')
g.region n=$n2 s=$s2 e=$e2 w=$w2 res=$ewres
g.region -p | head -5

# 2. Run both backends per azimuth, compare.
worst=0
echo
printf "%8s  %10s  %10s  %10s  %12s\n" "az" "max°" "p99°" "mean°" ">0.5° cells"
echo "$AZS" | tr , '\n' | while read az; do
    [ -z "$az" ] && continue
    suf=$(awk -v a=$az 'BEGIN{i=int(a); f=int((a-i)*10+0.5); printf "%03d_%d", i, f}')
    gpu_out=hgpu_p_${suf}
    rh_out=rhpr_p_${suf}

    "$BIN" elevation=$DEM output=hgpu_p direction=$az \
           maxdistance=$MAXD bodyradius=$BODYR --o --q
    r.horizon elevation=$DEM direction=$az maxdistance=$MAXD \
              output=rhpr_p -d --o --q

    # r.horizon's per-direction raster suffix may use only int part
    if ! g.findfile element=cell file=$rh_out mapset=. > /dev/null 2>&1; then
        rh_out_alt=rhpr_p_$(printf "%03d" $(echo $az | cut -d. -f1))
        if g.findfile element=cell file=$rh_out_alt mapset=. > /dev/null 2>&1; then
            rh_out=$rh_out_alt
        fi
    fi

    r.mapcalc "phgpu_diff = abs($gpu_out - $rh_out)" --o --q
    stats=$(r.univar map=phgpu_diff -ge percentile=99 2>/dev/null \
            | grep -E "^(max|mean|percentile_99)=" \
            | tr '\n' ' ')
    mx=$(echo "$stats" | sed -n 's/.*max=\([^ ]*\).*/\1/p')
    p9=$(echo "$stats" | sed -n 's/.*percentile_99=\([^ ]*\).*/\1/p')
    mn=$(echo "$stats" | sed -n 's/.*mean=\([^ ]*\).*/\1/p')
    big=$(r.mapcalc "phgpu_big = if(phgpu_diff > 0.5, 1, 0)" --o --q
          r.univar map=phgpu_big -g 2>/dev/null | sed -n 's/^sum=\(.*\)/\1/p')
    big=${big%.*}
    printf "%8s  %10.4f  %10.4f  %10.4f  %12s\n" "$az" "$mx" "$p9" "$mn" "$big"

    # track worst across azimuths via a tmp file (subshell loses vars)
    awk -v m=$mx -v cur=$worst 'BEGIN{print (m+0 > cur+0) ? m : cur}' > /tmp/.phgpu_worst
    worst=$(cat /tmp/.phgpu_worst)

    g.remove type=raster name=$gpu_out,$rh_out,phgpu_diff,phgpu_big \
        flags=f --q 2>/dev/null || true
done
worst=$(cat /tmp/.phgpu_worst 2>/dev/null || echo 0)

echo
if awk -v w=$worst -v t=$THRESH_DEG 'BEGIN{exit (w < t) ? 0 : 1}'; then
    echo "PASS: worst max-diff ${worst}° < threshold ${THRESH_DEG}°"
    exit 0
else
    echo "FAIL: worst max-diff ${worst}° >= threshold ${THRESH_DEG}°"
    exit 1
fi
