#!/bin/sh
# SPDX-License-Identifier: MIT
# On-board paired Goodix drag test. Arguments: before after helper output-dir.
set -eu
before=${1:?before executable}
after=${2:?after executable}
helper=${3:?ui-drag-replay executable}
output=${4:?new output directory}
[ ! -e "$output" ] || exit 2
mkdir -p "$output"
touch_device=
for name in /sys/class/input/event*/device/name; do
    case $(cat "$name") in *Goodix*) node=${name%/device/name}; touch_device=/dev/input/${node##*/}; break;; esac
done
[ -n "$touch_device" ]
/etc/init.d/S97lvgl-music-demo stop
gui_pid=
cleanup() {
    [ -z "$gui_pid" ] || kill -TERM "$gui_pid" 2>/dev/null || true
    /etc/init.d/S97lvgl-music-demo start
}
trap cleanup EXIT HUP INT TERM
for attempt in 1 2 3; do
    for version in before after; do
        executable=$before
        [ "$version" != after ] || executable=$after
        KORVO_UI_STATS=1 "$executable" widgets > "$output/$version-$attempt.log" 2>&1 &
        gui_pid=$!
        ready=0
        for poll in 1 2 3 4 5 6 7 8 9 10; do
            if grep -q 'running widgets' "$output/$version-$attempt.log"; then ready=1; break; fi
            sleep 1
        done
        [ "$ready" = 1 ]
        sleep 3
        [ -r /proc/"$gui_pid"/stat ]
        first=$(awk '{print $14+$15}' /proc/"$gui_pid"/stat)
        start=$(cut -d' ' -f1 /proc/uptime)
        "$helper" "$touch_device" 10 400 10 130 30 20
        sleep 1
        "$helper" "$touch_device" 10 130 10 400 30 20
        sleep 1
        last=$(awk '{print $14+$15}' /proc/"$gui_pid"/stat)
        end=$(cut -d' ' -f1 /proc/uptime)
        echo "widgets $version run=$attempt cpu_ticks=$((last-first)) start=$start end=$end"
        kill -TERM "$gui_pid"
        wait "$gui_pid"
        gui_pid=
        cat "$output/$version-$attempt.log"
    done
done
echo WIDGET_COMPARISON_DONE
