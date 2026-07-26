#!/bin/sh
# Builds the deterministic fixture project used by tools/ui_capture.sh.
#
# Produces a 40 s synthetic track with a steady pulse and a sweeping partial
# (so audio-reactive scenes have something to react to), an analysis bridge
# carrying eight lyric cues and three scene sections, and the .musi project the
# capture catalogue opens. Everything lands under build/, which is ignored by
# Git: no fixture audio, bridge, or project is ever committed.
#
# Usage: tools/ui_fixture.sh [OUTDIR]     (default build/ui-review)
set -eu

OUT=${1:-build/ui-review}
APP=./build/musializer
DISPLAY_NUM=${MUSIALIZER_CAPTURE_DISPLAY:-77}

[ -x "$APP" ] || { echo "missing $APP - run ./nob build debug" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is not installed" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Xvfb is not installed" >&2; exit 1; }

mkdir -p "$OUT"

if ! [ -f "$OUT/demo.wav" ]; then
    ffmpeg -loglevel error -y -f lavfi -i \
"aevalsrc='0.45*sin(2*PI*100*t)*exp(-9*mod(t,0.5)) \
+ 0.22*sin(2*PI*(300+180*sin(2*PI*0.08*t))*t) \
+ 0.12*sin(2*PI*1500*t)*exp(-30*mod(t,0.25))':d=40:s=44100:c=stereo" \
        -c:a pcm_s16le "$OUT/demo.wav"
fi

python3 - "$OUT" <<'PY'
import base64, hashlib, pathlib, sys

out = pathlib.Path(sys.argv[1])
digest = hashlib.sha256((out / "demo.wav").read_bytes()).hexdigest()
b64 = lambda text: base64.b64encode(text.encode()).decode()

# Lines of varying length on purpose: short cues and long cues expose different
# caption wrapping and cue-row truncation behaviour.
lyrics = [
    (1,  1500,  5200, "We were carving light out of the quiet"),
    (2,  5400,  9100, "and the room was holding its breath"),
    (3,  9300, 13000, "Every wire hummed a different colour"),
    (4, 13200, 17400, "until the morning let it rest"),
    (5, 17600, 21300, "Hold on"),
    (6, 21500, 26000, "the signal is still ours to keep"),
    (7, 26200, 31000, "and nothing that we made will fall asleep"),
    (8, 31200, 36500, "not while the speakers still remember how to breathe"),
]
sections = [
    (11,     0, 13000, "spectrum", 700, '["measured onset density"]'),
    (12, 13000, 26000, "loom",     820, '["sustained harmonic bed"]'),
    (13, 26000, 40000, "cadence",  640, '["closing decay"]'),
]

rows = ["MUSIALIZER_BRIDGE\t1", f"AUDIO\t{digest}\t40000"]
rows += [f"LYRIC\t{i}\t{s}\t{e}\t-1\tnone\t{b64(t)}" for i, s, e, t in lyrics]
rows += [f"SECTION\t{i}\t{s}\t{e}\t{scene}\t{strength}\t{b64(reasons)}"
         for i, s, e, scene, strength, reasons in sections]
(out / "demo.bridge.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
PY

# The project is built by the application itself, so the fixture exercises the
# real import/save path rather than a hand-written .musi the codec never saw.
rm -f "$OUT/demo.musi"
Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp >"$OUT/.xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 2
DISPLAY=":$DISPLAY_NUM" PULSE_SERVER=/nonexistent/musializer-capture \
    "$APP" --mute "$OUT/demo.wav" \
    --analysis-bridge "$OUT/demo.bridge.tsv" \
    --save-project "$OUT/demo.musi" >"$OUT/fixture.log" 2>&1 || true
kill "$xvfb_pid" 2>/dev/null || true
wait "$xvfb_pid" 2>/dev/null || true

[ -f "$OUT/demo.musi" ] || { echo "fixture project was not written; see $OUT/fixture.log" >&2; exit 1; }
grep -c 'applied 8 lyrics, 3 scene sections' "$OUT/fixture.log" >/dev/null || {
    echo "bridge did not apply as expected; see $OUT/fixture.log" >&2; exit 1; }
# A family list for the caption face browser. Written here rather than fetched,
# because a capture run must never be the thing that contacts Google Fonts. The
# entries are real families, so the coverage notes a review reads are the ones
# the product would show.
cat > "$OUT/fonts.tsv" <<'CATALOGUE'
musializer.font-catalogue/v1	8
Roboto	Sans Serif	cyrillic,cyrillic-ext,greek,greek-ext,latin,latin-ext,vietnamese
Open Sans	Sans Serif	cyrillic,cyrillic-ext,greek,greek-ext,latin,latin-ext,vietnamese
Inter	Sans Serif	cyrillic,cyrillic-ext,greek,greek-ext,latin,latin-ext,vietnamese
Lato	Sans Serif	latin,latin-ext
Playfair Display	Serif	cyrillic,latin,latin-ext,vietnamese
EB Garamond	Serif	cyrillic,cyrillic-ext,greek,greek-ext,latin,latin-ext,vietnamese
Space Mono	Monospace	latin,latin-ext,vietnamese
Caveat	Handwriting	cyrillic,cyrillic-ext,latin,latin-ext
CATALOGUE

echo "fixture ready: $OUT/demo.musi"
