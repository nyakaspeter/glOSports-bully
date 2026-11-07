# glOSports-bully — Bully: Anniversary Edition, native, on the GA36-MB

Bully: Anniversary Edition through Rockstar's Android build (War Drum
Studios, 2016): a loader (`loader/`) maps the game's own `libBully.so`
(armeabi-v7a), resolves its imports to goodluckOS's glibc, ARM's Mali GLES 2.0
driver (from the folder's `glos/`, see `goodluckOS-mali-driver`), OpenAL Soft and
SDL's pad, answers its Java calls through a fake JNI, and runs it, the way
[bully_vita](https://github.com/TheOfficialFloW/bully_vita) (MIT) does on the
Vita. The loader core is the one in `glOSports-gtasa`, carried over from
`glOSports-maxpayne`. The console stays stock goodluckOS.

Status: runs on the GA36-MB, playable; tested with the game files from the Vita
release's `data/Bully` (libBully.so md5 e6e52fb0), its Vita-only `gxp/`
shaders and PlayStation prompt textures left out.

Fixes found on the first runs: `FindClass` must not return NULL
(`SystemServicesAND_JNI` spins for good, `b .`, on a NULL WarEngine class);
`NVThreadGetCurrentJNIEnv` stays the game's; the pause menu's map crashed
when its touch help button (`main.help`) isn't there (`game.c`: the visible
check is skipped then); `NvFEOF` / `OS_FileRead` read bionic's FILE flags
(as in GTA III); `OS_MovieStop` / `OS_MovieSetSkippable` are 4-byte branches,
so their targets are hooked; diagonals only walked until the left stick
was squared (`GAME_STICK_SQUARE`).

## Game files

From your own install of the Android game (package
`com.rockstargames.bully`), into `/home/player/ports/bully/game/`:

| From | What goes into `game/` |
|---|---|
| the `.apk` (version 1.0.0.19, the release whose OBBs are set 11) | `lib/armeabi-v7a/libBully.so`, and the contents of `assets/` |
| `main.11.com.rockstargames.bully.obb` | copied as is (not unpacked) to `game/Android/main.obb` |
| `patch.11.com.rockstargames.bully.obb` | copied as is to `game/Android/patch.obb` |

The OBBs are in `/sdcard/Android/obb/com.rockstargames.bully/` on the phone.
The game reads them as zips; on the first start it sorts their entries and
writes an index next to each (`main.obb.idx`, `patch.obb.idx`), which takes
a few minutes on the Vita; later starts read the index.

## The game folder on the console (`/home/player/ports/bully/`)

| | |
|---|---|
| `bully` | the loader (DT_RPATH `$ORIGIN/lib`) |
| `lib/` | OpenAL Soft 1.19 and its dependencies (sndio, bsd, md) |
| `alsoft.conf` | OpenAL reads it from the game folder by itself (ALSA) |
| `game/` | the game files above; saves land here too (and the OBB indexes) |
| `glos/` | the ARM driver set from `goodluckOS-mali-driver` |
| `start.sh` | pad mapping and settings, then `exec ./glos/glos-gpu run mali ./bully` |

Launcher entry: `bully.puppy`, its icon a 3:2 crop of a PS2 banner. Log: `/dev/shm/bully.log` (a crash prints the
PC and LR as `libBully.so+offset`).

## Settings (`start.sh`)

| Variable | Default | |
|---|---|---|
| `GAME_STICK_SCALE`, `GAME_STICK_DEADZONE` | 1.5, 0.12 | stick shaping (radial dead zone after scaling) |
| `GAME_STICK_SQUARE` | 0 (1 in `start.sh`) | left stick stretched from circle to square: a full diagonal is 1.0 on both axes, not 0.71 each (Bully runs only past a threshold on each axis, so diagonals only walked); the right stick stays round |
| `GAME_FPS_COUNTER` | 0 (1 in `start.sh`) | frames per second, top right |
| `GAME_LOG_FPS` | 0 | fps, swap time, swaps and flips into the log |
| `GAME_VSYNC` | 1 | swap interval and the wait for the previous flip (no tearing) |
| `GAME_AUDIO_RATE` | 44100 | OpenAL mixing rate (the game asks for 22050) |
| `GAME_LANGUAGE` | 0 | 0 English, 1 French, 2 German, 3 Italian, 4 Spanish, 5 Japanese |

## The videos

On Android a Java MediaPlayer shows the game's videos over it. Bully has
two, both in the patch OBB: `BullyOrig/Movies/Rockstar.m4v`, the logo at
startup, and `AttractMode.mp4`, the opening on New Game (Jimmy and his mother
on the way to Bullworth, under the credits). `tools/movies.sh` converts both
on the Mac (1280x720 -> 640x360 H.264 baseline, AAC 44.1 kHz stereo; 0.7 and
27 MB) into `gamedata/movies/`; they go loose into `game/BullyOrig/Movies/`.

The originals are 1280x720 H.264 Main. The logo (0.7 Mbit/s) plays as it is,
read in place from the OBB (stored, not compressed: `movie_archives` and
FFmpeg's `subfile` protocol), 371 frames, none dropped. The opening
(5.7 Mbit/s) is too heavy: 39 of 3,220 pictures in time; with the H.264
loop filter skipped and FFmpeg's fast flags, 234 dropped and visible colour
errors. Converted it plays in full. The player opens a loose file first and
stops a video that stays a second behind for two seconds (logged), so an
unconverted opening ends at once instead of freezing on a picture.

`GAME_MOVIE_TEST=<file>@<seconds>` (test runs) starts a video through the
game's `OS_MoviePlay` hook that many seconds in. In Bully it checks the
decoding only: at a menu the game draws on, and its frames and the video's
take turns on the screen; on New Game the game waits and only the video
draws.

The player (`loader/movie.c`, FFmpeg's MP4 demuxer and H.264 / AAC decoders
linked in statically) decodes on its own threads, draws with a GL context of
its own on the game's surface (none of the game's GL state is touched), and
plays the sound through the folder's OpenAL; A or Start skips. Bully asks for
the video on its game thread and then waits on `OS_MovieIsPlaying`; its
render thread gets no frames meanwhile and idles in
`GameRenderer::ConcurrentRenderThread`, holding the screen, so that loop's
`SleepThread` call (`+0x89417e`) goes through `render_idle`, which shows the
newest picture first. Measured: 30 fps, no frame dropped.

## From bully_vita, what carries over and what doesn't

- **Carried over:** the War Drum hooks (threads, `OS_ThreadWait`, screen,
  system init), no TouchSense, the OBB entry sort and index cache, Continue
  loads the newest save, Exit quits, the Java methods (window, storage,
  device, pad; sharing ignored).
- **Left out:** the Vita's rendering work (its precompiled shader
  replacements and `gxp/` shaders, vitaGL tuning): Mali-400 runs the game's
  GLSL ES shaders as Android does. The Vita's video player (`movie.c` plays the
  story video). The PlayStation button prompt textures of its `gamefiles.zip`.
- **Different:** threads are real pthreads; EGL is ARM's fbdev EGL with the
  tear-free flip wait from `glOSports-maxpayne`.
- **Watch:** bully_vita notes that the game never frees unused textures (made
  for phones with more memory), so long sessions crash on the Vita's 512 MB;
  the GA36-MB has 494 MB.

## The loader (`loader/`)

| File | |
|---|---|
| `so_util.c` | maps the .so, relocates, finds symbols through its hash table, patches code (`hook_addr`: Thumb or ARM by the address's low bit) |
| `imports.c` | bionic → glibc (softfp wrappers for every float import, bionic-sized pthread / sem / stat / FILE / jmp_buf); every `gl*` / `egl*` resolves to ARM's driver, through wrappers where floats pass by value |
| `jni.c`, `jni_names.inc` | the fake Java VM and JNIEnv: all 233 slots, unknown ones log their name |
| `android.c` | War Drum's threads and screen, and the Java methods the game calls (window, storage, device, pad) |
| `egl.c` | GLES 2.0 on EGL on `/dev/fb0`, tear-free flips, FPS counter |
| `openal.c` | every `al*` / `alc*` entry point of the game's own OpenAL Soft jumps to the folder's (found by walking the .so's symbols) |
| `pad.c` | SDL2 game controller, stick shaping |
| `movie.c` | the story video: FFmpeg decoding, its own GL context, OpenAL sound |
| `game.c` | what is Bully: Anniversary Edition's: hooks |

## Build

```
./build.sh      # loader/ -> build/bully (Docker, Debian bookworm armhf)
./package.sh    # -> out/ports/bully/ and out/.local/share/applications/bully.puppy
```

`build.sh` runs `tools/build-ffmpeg.sh` first when `build/ffmpeg/` is
missing: a static FFmpeg 6.1.2 (MP4 demuxer, H.264 and AAC decoders, NEON),
from ffmpeg.org's `ffmpeg-6.1.2.tar.xz` (sha256 `3b624649…`) unpacked in
`upstream/ffmpeg-6.1.2`.

Test tools are shared: `../goodluckOS-tools/testing-sync.sh bully` puts them
on the console (`glos/testing/`), `testing-clean.sh bully` takes them off,
`shot.sh bully file.png` takes a screenshot (see `goodluckOS-tools`).

## Reference loader (`upstream/`, not committed)

| Folder | Source | Commit |
|---|---|---|
| `upstream/bully_vita` | [TheOfficialFloW/bully_vita](https://github.com/TheOfficialFloW/bully_vita) (MIT) | `f0b069f` |

## Third-party components

Code carried over from the Vita loaders it names keeps their MIT notices;
those, FFmpeg (LGPL-2.1, linked statically for the videos), OpenAL Soft and
the other bundled libraries, and ARM's Mali driver are listed with their
licences in `licenses/`, which `package.sh` copies into the game folder. The
game itself is not included.
