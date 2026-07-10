# Musializer

<p align=center>
  <img src="./resources/logo/logo-256.png">
</p>

> [!WARNING]
> This software is unfinished. Keep your expectations low.

The project aims to make a tool for creating beautiful music visualizations and rendering high quality videos of them.

*Please, read [CONTRIBUTING.md](CONTRIBUTING.md) before making a PR.*

## Demo

*Music by [@nu11](https://soundcloud.com/nu11-chiptune) from [https://soundcloud.com/nu11-chiptune/nu11-wip-works-2016-2022](https://soundcloud.com/nu11-chiptune/nu11-wip-works-2016-2022) at 20:38*

https://github.com/tsoding/musializer/assets/165283/8b9f9653-9b3d-4c04-9569-338fa19af071

## Supported Audio Formats

- wav
- ogg
- mp3
- qoa
- xm
- mod
- flac

## Download Binaries

- Windows: [musializer-alpha2-win64.zip](https://github.com/tsoding/musializer/releases/download/alpha2/musializer-alpha2-win64.zip)
- Linux: *in progress*

## Build from Source

External Dependencies:
- [ffmpeg](https://ffmpeg.org/) executable available in `PATH` environment variable. It is called as a child process during the rendering of the videos. So if you don't plan to render any videos it's completely **optional**.

We are using Custom Build System written entirely in C called `nob`. [nob.c](./nob.c) is the program that builds Musializer. For more info on this Build System see the [nob.h repo](https://github.com/tsoding/nob.h).

Before using `nob` you need to bootstrap it. Just compile it with the available C compiler. On Linux it's usually `$ cc -o nob nob.c` on Windows with MSVC from within `vcvarsall.bat` it's `$ cl.exe nob.c`. You only need to boostrap it once. After the bootstrap you can just keep running the same executable over and over again. It even tries to rebuild itself if you modify [nob.c](./nob.c) (which may fail sometimes, so in that case be ready to reboostrap it).

I really recommend to read [nob.c](./nob.c) and [nob.h](https://github.com/tsoding/nob.h) to get an idea of how it all actually works. The Build System is a work in progress, so if something breaks be ready to dive into it.

### Linux and OpenBSD

```console
$ cc -o nob nob.c # ONLY ONCE!!!
$ ./nob
$ ./build/musializer
```

Development profiles and the headless test suite are available after the same
bootstrap:

```console
$ ./nob build debug
$ ./nob build sanitize
$ ./nob build hotreload
$ ./nob test
$ ./nob test sanitize
```

Tracks may also be loaded from the command line. The optional scene selector is
useful for repeatable smoke tests:

```console
$ ./build/musializer --scene orbital path/to/track.mp3
$ ./build/musializer --scene atlas path/to/track.mp3
$ ./build/musializer --scene terrarium path/to/track.mp3
$ ./build/musializer --ascii-image path/to/image.png path/to/track.mp3
$ ./build/musializer --scene orbital path/to/track.wav --render output.mp4
```

Built-in scene names are `spectrum`, `pulse`, `orbital`, `ascii`, `atlas`, and
`terrarium`. While the app is running, <kbd>1</kbd> through <kbd>6</kbd> switch
between them. A
command-line render exits automatically after FFmpeg finishes, making it useful
for smoke tests and scripted renders.

For a repeatable legacy-scene export smoke, use any short WAV fixture and check
the resulting streams:

```console
$ ./build/musializer --scene spectrum fixture.wav --render /tmp/musializer-smoke.mp4
$ ffprobe -v error -show_entries stream=codec_type,width,height,r_frame_rate \
    -of compact /tmp/musializer-smoke.mp4
```

Optional offline analysis helpers for measured audio, Whisper timings, and
cached MiMo interpretations are documented in
[`tools/ANALYSIS_ADAPTERS.md`](tools/ANALYSIS_ADAPTERS.md) and
[`tools/MEASURED_ANALYSIS.md`](tools/MEASURED_ANALYSIS.md). They are separate
from the C renderer and never make network calls unless the MiMo helper is
explicitly invoked without `--dry-run`.

If the build fails because of missing header files, you may need to install the X11 dev packages.

On Debian, Ubuntu, etc, do this:

```console
$ sudo apt install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev
```

On other distro's, use the appropriate package manager.

### Windows MSVC

From within `vcvarsall.bat` do

```console
> cl.exe nob.c # ONLY ONCE!!!
> nob.exe
> build\musializer.exe
```

### Cross Compilation from Linux to Windows using MinGW-w64

Install [MinGW-w64](https://www.mingw-w64.org/) from your distro repository.

Edit `./build/config.h` and set `MUSIALIZER_TARGET_WIN64_MINGW` instead of `MUSIALIZER_TARGET_LINUX`.

```console
$ ./nob
$ wine ./build/musializer.exe
```

## Hot Reloading

Edit `./build/config.h` and enable `MUSIALIZER_HOTRELOAD`.

```console
$ ./nob
$ ./build/musializer
```

Keep the app running. Rebuild with `./nob`. Hot reload by focusing on the window of the app and pressing <kbd>h</kbd>.

The way it works is by putting the majority of the logic of the application into a `libplug` dynamic library and just reloading it when requested. The [rpath](https://en.wikipedia.org/wiki/Rpath) (aka hard-coded run-time search path) for that library is set to `.` and `./build/`. See [src_build/nob_linux.c](src_build/nob_linux.c) for more information on how everything is configured.
