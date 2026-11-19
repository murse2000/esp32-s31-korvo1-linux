#!/bin/sh
# SPDX-License-Identifier: MIT
# Compare explicit hardware fill offload with the default CPU-fill threshold.
set -eu
bench=${1:?extra interaction-bench executable is required}
output=${2:?new output directory is required}
[ ! -e "$output" ] || exit 2
mkdir -p "$output"
/etc/init.d/S97lvgl-music-demo stop
trap 'unset KORVO_PPA_EXTRA KORVO_PPA_CPU_FILL_LIMIT; /etc/init.d/S97lvgl-music-demo start' EXIT HUP INT TERM
export KORVO_PPA_EXTRA=1
for strategy in cached hardware; do
    export KORVO_PPA_CPU_FILL_LIMIT=4096
    [ "$strategy" != cached ] || export KORVO_PPA_CPU_FILL_LIMIT=65536
    KORVO_UI_STATS=1 KORVO_INTERACTION_CAPTURE="$output/$strategy" \
        "$bench" slider 90 > "$output/$strategy.log" 2>&1
    cat "$output/$strategy.log"
done
for reference in "$output/cached-"*.rgb565; do
    candidate=$(printf '%s\n' "$reference" | sed 's,/cached-,/hardware-,')
    cmp "$reference" "$candidate"
    rm -f "$reference" "$candidate"
done
cached=$(sed -n 's/.* fill=\([0-9]*\).*/\1/p' "$output/cached.log")
hardware=$(sed -n 's/.* fill=\([0-9]*\).*/\1/p' "$output/hardware.log")
[ "$hardware" -gt "$cached" ] || { echo 'Hardware fill offload was not exercised' >&2; exit 1; }
unset KORVO_PPA_CPU_FILL_LIMIT
echo 'PASS slider: all captured frames match exactly; explicit hardware fill offload exercised.'
