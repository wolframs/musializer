#!/bin/sh
# Headless UI capture harness.
#
# Runs the built application on a PRIVATE Xvfb display sized exactly to the
# requested window, so a review session never appears on, steals focus from, or
# opens an audio stream in the operator's real desktop session. One display per
# state keeps captures independent: a state that rejects its own probe spec or
# hangs cannot corrupt its neighbours.
#
# Usage: tools/ui_capture.sh OUTDIR [CATALOGUE]
#
# CATALOGUE defaults to tools/ui_states.txt. Lines are
# "name|WIDTHxHEIGHT|args..."; blank lines and '#' comments are ignored. The
# token PROJECT in an argument list is replaced by the fixture project built by
# tools/ui_fixture.sh.
#
# Requires: a debug or release ./nob build, Xvfb, and ffmpeg. See
# tools/UI_REVIEW.md for the review loop this feeds.
set -u

APP=./build/musializer
OUTDIR=${1:-}
CATALOGUE=${2:-tools/ui_states.txt}
FIXTURE_DIR=${MUSIALIZER_UI_FIXTURE_DIR:-build/ui-review}
DISPLAY_NUM=${MUSIALIZER_CAPTURE_DISPLAY:-77}
# Seconds between launching a state and grabbing it. The window must be mapped
# and the first frames drawn. With play=1 the captured playhead is therefore
# roughly `time + SETTLE`, which matters on short fixtures.
SETTLE=${MUSIALIZER_CAPTURE_SETTLE:-6}

[ -n "$OUTDIR" ] || { echo "usage: $0 OUTDIR [CATALOGUE]" >&2; exit 2; }
[ -x "$APP" ] || { echo "missing $APP - run ./nob build debug" >&2; exit 1; }
[ -f "$CATALOGUE" ] || { echo "missing catalogue $CATALOGUE" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Xvfb is not installed" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is not installed" >&2; exit 1; }

MASTER="$FIXTURE_DIR/demo.musi"
if ! [ -f "$MASTER" ]; then
    echo "missing fixture $MASTER - run tools/ui_fixture.sh" >&2
    exit 1
fi

mkdir -p "$OUTDIR"

# Every state gets a throwaway copy of the fixture, never the master. The
# application autosaves a dirty project about 1.5 s after it changes, so a state
# that dirties the workspace used to rewrite the shared fixture and silently
# change what every later capture photographed: a --scene state left the fixture
# on that scene, and the next run's panel captures were no longer comparable with
# the previous run's. Startup flags no longer mark a project dirty, but the
# harness must not depend on that to stay reproducible.
WORKDIR="$OUTDIR/.fixture"
PROJECT="$WORKDIR/demo.musi"
master_before=$(sha256sum "$MASTER" | cut -d' ' -f1)

# Never inherit the operator's session: no real DISPLAY, no Wayland handle, and
# an audio server path that cannot resolve, so a capture never opens a client
# stream on the desktop being worked in.
unset WAYLAND_DISPLAY
PULSE_SERVER=/nonexistent/musializer-capture
export PULSE_SERVER

status=0
while IFS='|' read -r name size args; do
    case "$name" in ''|\#*) continue ;; esac
    width=${size%x*}
    height=${size#*x}
    out="$OUTDIR/$name.png"
    log="$OUTDIR/$name.log"
    resolved=$(printf '%s' "$args" | sed "s#PROJECT#$PROJECT#g")

    # The asset bundle is addressed relative to the project stem, so the copy has
    # to keep the name. Refresh it per state, not per run: one state that writes
    # must not change what the next one sees.
    rm -rf "$WORKDIR"
    mkdir -p "$WORKDIR"
    cp "$MASTER" "$PROJECT"
    if [ -d "$FIXTURE_DIR/demo.assets" ]; then
        cp -r "$FIXTURE_DIR/demo.assets" "$WORKDIR/demo.assets"
    fi

    Xvfb ":$DISPLAY_NUM" -screen 0 "${width}x${height}x24" -nolisten tcp \
        >"$OUTDIR/.xvfb.log" 2>&1 &
    xvfb_pid=$!
    sleep 2

    # shellcheck disable=SC2086 # resolved is a deliberate word-split argv list.
    DISPLAY=":$DISPLAY_NUM" $APP --mute $resolved >"$log" 2>&1 &
    app_pid=$!
    sleep "$SETTLE"

    if kill -0 "$app_pid" 2>/dev/null; then
        ffmpeg -loglevel error -y -f x11grab -video_size "${width}x${height}" \
            -i ":$DISPLAY_NUM.0" -frames:v 1 "$out" >>"$log" 2>&1
    else
        # Exiting before the settle window means the state rejected its own
        # probe spec. Report it rather than leaving a missing PNG unexplained.
        wait "$app_pid" 2>/dev/null
        echo "FAIL $name (application exited early; see $log)" >&2
        status=1
    fi

    kill "$app_pid" 2>/dev/null
    sleep 1
    kill "$xvfb_pid" 2>/dev/null
    wait "$xvfb_pid" 2>/dev/null

    if [ -f "$out" ]; then
        if [ "$(sha256sum "$PROJECT" | cut -d' ' -f1)" = "$master_before" ]; then
            echo "ok   $name -> $out"
        else
            # Not fatal, but every later comparison is now suspect: this state
            # photographed a project the fixture no longer describes.
            echo "ok   $name -> $out (WROTE the project; captures may drift)"
        fi
    else
        echo "FAIL $name (no capture)" >&2
        status=1
    fi
done < "$CATALOGUE"

rm -rf "$WORKDIR"
if [ "$(sha256sum "$MASTER" | cut -d' ' -f1)" != "$master_before" ]; then
    echo "FAIL the master fixture changed during the run; captures are not comparable" >&2
    status=1
fi

exit $status
