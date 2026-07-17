# Product readiness and packaging audit

This file records the operational boundary of the current build and packaging
workflow. It is intentionally stricter than the feature roadmap: a feature is
listed as packaged only when the produced artifact can locate its files and its
external runtime requirements are stated.

## Supported workflows

| Workflow | Linux | macOS | OpenBSD | MinGW-w64 | MSVC |
| --- | --- | --- | --- | --- | --- |
| Named release/debug/hotreload build | implemented | implemented | implemented | implemented | implemented |
| ASan + UBSan application build | implemented | implemented | rejected explicitly | rejected explicitly | rejected explicitly |
| Headless C tests | GCC/Clang host toolchain | GCC/Clang-compatible host required | GCC/Clang-compatible host required | not native-MSVC compatible | not native-MSVC compatible |
| Distribution recipe | tar.gz | `.app` directory | tar.gz | zip | not implemented |
| Analysis support files in artifact | included | included in `.app` resources | included | included | not applicable |

`./nob dist` performs a fresh release build before packaging. Linux distribution
objects live under `build/raylib/linux-dist` and omit `-march=native`, preventing
an earlier workstation build from leaking CPU-specific instructions into the
archive. Archive inputs are an explicit allowlist, so stale files in an old
staging directory, `.env`, and Python bytecode caches are not packaged.

## External runtime dependencies

- FFmpeg must be discoverable through `PATH` for MP4 export. It is not bundled.
  The export panel checks this before opening the destination picker; encoder
  startup is still validated again when rendering begins.
- Python 3 and NumPy are needed for measured/assisted analysis, not playback or
  rendering.
- Whisper, Codex, and an OpenRouter credential are needed only by their explicit
  assistance modes. MiMo/OpenRouter remains an opt-in network action.
- Linux binaries are dynamically linked to the build system's libc even though
  raylib is linked statically. Release artifacts therefore still need testing on
  the oldest Linux distribution chosen as a compatibility baseline.

## Known product gaps

- The macOS `.app` is not signed, notarized, or placed in a DMG, and it does not
  register `.musi` Finder document handling. Analysis helpers are packaged, but
  their Python dependencies, optional local models, Codex login, and remote
  credential flow still require validation on a native macOS release machine.
  Icon generation also requires ImageMagick's `convert` and Apple's `iconutil`.
- Native MSVC distribution packaging is not implemented.
- The MinGW archive does not bundle FFmpeg or Python. Assisted analysis invokes
  the standard Windows launcher as `py -3`, so that launcher and compatible
  Python dependencies must be installed separately and still need native CI.
- Windows project writes use UTF-16 APIs, but FFmpeg launch/output publication
  still uses ANSI Win32 APIs. Audio or render paths outside the active Windows
  code page require a wide-process conversion before a Unicode-safe claim.
- OpenBSD and Windows recipes receive static syntax review on Linux, but require
  native/cross-toolchain CI before release claims.
- The Linux per-user launcher points into either its source checkout or an
  unpacked portable distribution. Moving that directory requires reinstalling
  the launcher. It installs a `.musi` MIME association and desktop file, but is
  not a native relocatable system package.
- GPU readback, FFmpeg pipe writes, and encoder finalization still run on the UI
  thread. Pipe back-pressure or a slow final MP4 relocation can temporarily
  prevent repaint/cancel input despite bounded child cleanup. A writer queue
  and asynchronous finalization are required before claiming long-export UI
  responsiveness.
- The Assist panel discovers its packaged helper, reports specific launch/runtime
  failures, distinguishes valid empty results from applyable candidates, and
  exposes persistent copy-result, copy-log, and copy-folder actions for the
  immutable job artifacts, but it does not duplicate the product
  doctor's full Python/model/credential preflight in-process. The helper also
  exposes elapsed time and a hard timeout, not structured per-stage progress or
  a trustworthy percentage. Run `tools/musializer_doctor.py` before production
  sessions that depend on Whisper, Codex, or OpenRouter.
- Export currently decodes the complete track and holds both Raylib's Wave and
  a float analysis copy. Normal songs are handled, but hour-long mixes can use
  gigabytes and need a chunked canonical PCM reader before large-media claims.
- Saved projects use a content-addressed sibling asset bundle for audio and
  optional ASCII source images. The `.musi` file and its matching
  `<stem>.assets/` directory must be moved together; a single-file archive or
  cloud sync rule that omits the sibling directory is incomplete.
- Caption text is strict UTF-8 and the bundled atlas covers accented Latin,
  Greek, Cyrillic, and common symbols. CJK font fallback, bidirectional text,
  and complex-script shaping are not implemented yet.
- Cadence currently estimates per-word windows proportionally inside each
  accepted line-level lyric cue. This is deterministic and editable at the line
  boundary, but it is not a claim of Whisper-derived word alignment. A future
  word-timing format needs an explicit project and analysis-contract migration.
- The live beat phase learns from conservative onset intervals and falls back to
  a deterministic 120 BPM clock before it has enough evidence. It resets on
  seeks instead of reconstructing tempo history before the destination, so the
  first beats after an arbitrary seek can differ from uninterrupted playback.
  Offline export from frame zero remains deterministic.
- The current editor intentionally rejects schema-valid project features it
  cannot preserve: imported audio, persisted partial ranges, fractional
  FPS/non-MP4 output, scene stacks/noncanonical layout, parameter cues, and
  arbitrary analysis-driven mappings. Canonical constant mappings used for the
  built-in scene-control inspector are validated and preserved. The CLI does
  support transient deterministic segment renders through
  `--render-window START DURATION`; that range is not yet stored in `.musi` or
  authored in the Export panel.
- There is no native system/package installer, auto-updater, code signing, or
  release upload workflow on any platform. Linux does provide a tested per-user
  XDG launcher installer for source and unpacked portable builds.

## Release checks

From a clean checkout on the target platform:

```console
cc -o nob nob.c
./nob test sanitize        # Linux/macOS with compatible sanitizer runtimes
./nob dist
```

Before invoking optional workflows, inspect or machine-gate their prerequisites:

```console
python3 tools/musializer_doctor.py
python3 tools/musializer_doctor.py --json --require export
python3 tools/musializer_doctor.py --json \
    --require local_lyrics --require remote_mimo
```

The doctor distinguishes preview, export, local Whisper/Codex lyrics, and
remote MiMo readiness. It only probes discovery and writability. It does not
invoke FFmpeg/models or contact OpenRouter, and delegates credential presence
to the orchestration layer's one-key dotenv parser without emitting the value.

On Linux, inspect the archive and baseline before publishing:

```console
tar -tzf musializer-linux-x86_64.tar.gz
readelf -n musializer-linux-x86_64/musializer
ldd musializer-linux-x86_64/musializer
```

The archive must not contain `.env`, `__pycache__`, or files absent from the
distribution allowlist. Run a short CLI export and use `ffprobe` to verify the
selected video profile (1920x1080, 30 fps by default), exact scheduled frame
count, audio/video/container duration matched within the muxer time base,
BT.709 metadata, and an audio
stream. The renderer stages Raylib's decoded PCM for FFmpeg so analysis and mux
input share one timeline; a final sub-frame tail is padded to the video-frame
boundary. The launcher integration test uses an isolated XDG home and a
checkout path containing spaces, quotes, ampersands, and the sed delimiter,
and verifies `.musi` MIME installation/removal.
