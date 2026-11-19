#!/bin/sh
# SPDX-License-Identifier: MIT
# On-board integration check for native 800x480, as root. This drives the GUI.
# Build ui-event-replay.c with the target toolchain and copy to /tmp first.
set -eu
helper=${1:-/tmp/ui-event-replay}
touch_device=
for name in /sys/class/input/event*/device/name; do
    case $(cat "$name") in *Goodix*)
        node=${name%/device/name}
        touch_device=/dev/input/${node##*/}
        break;;
    esac
done
[ -n "$touch_device" ] && [ -x "$helper" ]
gui_pid=
resume() { [ -z "$gui_pid" ] || kill -CONT "$gui_pid" 2>/dev/null || true; }
trap resume EXIT HUP INT TERM
wait_screen()
{
    if [ "$1" = audio ]; then
        process=lvgl-music-demo
        pattern=lvgl-music-demo
    else
        process=korvo-ui
        pattern=" $1 "
    fi
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        gui_pid=$(pidof "$process" || true)
        if [ -n "$gui_pid" ] &&
           tr '\000' ' ' < /proc/"$gui_pid"/cmdline | grep -q "$pattern" &&
           ls -l /proc/"$gui_pid"/fd | grep -q /dev/input/; then return 0; fi
        sleep 1
    done
    echo "Expected $1 screen" >&2
    return 1
}
queued_touch()
{
    # Force both press and release to queue before the GUI can read either.
    # The old evdev driver collapsed this into RELEASED and lost the click.
    kill -STOP "$gui_pid"
    "$helper" "$touch_device" "$1" "$2"
    kill -CONT "$gui_pid"
}
/etc/init.d/S97lvgl-music-demo restart
wait_screen menu
queued_touch 142 115
wait_screen widgets
queued_touch 750 24
wait_screen menu
queued_touch 142 410
wait_screen audio
queued_touch 750 24
wait_screen menu
# Audio returns with argv[0]="korvo-ui". The following exec must still use
# an absolute executable path, otherwise it fails with ENOENT.
queued_touch 385 410
wait_screen camera
queued_touch 750 24
wait_screen menu
echo 'Queued touch, widgets/HOME and audio/HOME/camera/HOME passed.'
