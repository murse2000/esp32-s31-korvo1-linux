#!/bin/sh
# SPDX-License-Identifier: MIT
# Exact framebuffer comparison with pixel reuse enabled/disabled.
# Usage on board: sh check-scroll-reuse.sh /root/interaction-bench /root/proof-dir
set -eu
bench=${1:?interaction-bench executable is required}
output=${2:?output directory is required}
if [ -e "$output" ]; then echo 'Use a new output directory.' >&2; exit 2; fi
mkdir -p "$output"
/etc/init.d/S97lvgl-music-demo stop
trap '/etc/init.d/S97lvgl-music-demo start' EXIT HUP INT TERM
for scene in vertical horizontal changed small-change coalesced transparent tabview custom floating overlay gradient; do
  mode=scroll
  frames=90
  [ "$scene" != horizontal ] || mode=scroll-x
  case $scene in floating|overlay|gradient) frames=6;; esac
  for renderer in reference reused; do
    disable=
    [ "$renderer" != reference ] || disable=1
    # The port uses presence of KORVO_SCROLL_DISABLE, not its numeric value.
    unset KORVO_SCROLL_DISABLE KORVO_INTERACTION_CONTENT_CHANGE KORVO_INTERACTION_COALESCE
    unset KORVO_INTERACTION_FLOATING KORVO_INTERACTION_OVERLAY KORVO_INTERACTION_GRADIENT
    unset KORVO_INTERACTION_TRANSPARENT KORVO_INTERACTION_CUSTOM
    unset KORVO_INTERACTION_TABVIEW
    unset KORVO_INTERACTION_SMALL_CHANGE
    [ -z "$disable" ] || export KORVO_SCROLL_DISABLE=1
    case $scene in
      changed) export KORVO_INTERACTION_CONTENT_CHANGE=1;;
      small-change) export KORVO_INTERACTION_SMALL_CHANGE=1;;
      coalesced) export KORVO_INTERACTION_COALESCE=1;;
      floating) export KORVO_INTERACTION_FLOATING=1;;
      overlay) export KORVO_INTERACTION_OVERLAY=1;;
      gradient) export KORVO_INTERACTION_GRADIENT=1;;
      transparent) export KORVO_INTERACTION_TRANSPARENT=1;;
      custom) export KORVO_INTERACTION_CUSTOM=1;;
      tabview) export KORVO_INTERACTION_TABVIEW=1;;
    esac
    KORVO_UI_STATS=1 KORVO_INTERACTION_CAPTURE="$output/$scene-$renderer" \
      "$bench" "$mode" "$frames" > "$output/$scene-$renderer.log" 2>&1
    cat "$output/$scene-$renderer.log"
    if [ "$renderer" = reused ]; then
      if [ "$scene" = small-change ]; then
        grep -Eq 'korvo-cache: .*dirty_reused=[1-9][0-9]*' "$output/$scene-$renderer.log" || {
          echo 'FAIL small-change: bounded dirty redraw was not exercised' >&2; exit 1;
        }
      fi
      case $scene in
        overlay|gradient) :;;
        *) grep -Eq 'korvo-scroll: .*moves=[1-9][0-9]*' "$output/$scene-$renderer.log" || {
          echo "FAIL $scene: pixel reuse was never exercised" >&2; exit 1;
        };;
      esac
    fi
  done
  for reference in "$output/$scene-reference-"*.rgb565; do
    reused=$(printf '%s\n' "$reference" | sed 's/-reference-/-reused-/')
    cmp "$reference" "$reused" || {
      echo "FAIL $scene: $reference $reused" >&2
      exit 1
    }
    if [ "${KORVO_PROOF_KEEP:-0}" != 1 ]; then rm -f "$reference" "$reused"; fi
  done
  echo "PASS $scene: all captured frames match exactly"
done
echo 'Scroll reuse pixel comparison passed.'
