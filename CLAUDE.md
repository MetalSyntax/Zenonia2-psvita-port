# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PS Vita native port (loader) of **Zenonia 2** (Xperia Play ARMv6 Android release, v1.0.5). It does not
reimplement the game — it loads the original `libzenonia2.so` from the legally-owned APK directly into
Vita memory and provides a minimal Android-like environment (fake JNI, libc/OpenGL ES shims, file I/O
redirection) so the unmodified game binary runs natively. Based on TheFloW's Android SO Loader
(gtasa_vita architecture) + FalsoJNI (borrowed from the Prince of Persia Vita port).

The game's own code is never recompiled; almost all work here is loader-side: relocating/resolving the
`.so`, satisfying its imports, and patching a couple of confirmed engine bugs in place (see
`apply_so_patches` in `loader/main.c`).

## Build

Requires a **softfp** vitasdk build (`vitasdk-softfp/vdpm`, not the standard vitasdk).

```bash
cmake -Bbuild .
cmake --build build
```

Convenience targets from vitasdk's `vita.cmake` (need a device/vitacompanion):
```bash
cmake --build build --target send   # build, upload eboot.bin, run
cmake --build build --target dump   # fetch latest coredump and parse
```

Preferred day-to-day build script (builds in `/tmp` to dodge spaces in the repo path, copies results
into `build/`, and can optionally push/install to Vita3K or a real console over FTP):
```bash
./build.sh            # normal build -> build/zenonia_2.vpk
./build.sh shader      # experimental postprocess build -> build/zenonia_2_shader.vpk (see below)
```

There is no unit test suite. "Testing" means running the produced `.vpk`/`.self` against real hardware
or Vita3K and reading the resulting log — see Debugging below.

### Two build variants

`ENABLE_POSTPROCESS_SHADER` (CMake option, off by default) toggles a sharpen post-process shader in
`loader/postprocess.c`/`postprocess.h` applied to the engine's software-composited 400x240 frame before
it's blitted to the screen. It's a separate, experimental VPK (`zenonia_2_shader.vpk`) kept isolated from
the confirmed-working `zenonia_2.vpk` so it can be tested without risk. `build.sh shader` builds it in a
separate temp/build directory. As of the last recorded attempt it produces a black screen from the menu
onward (see `port_progress.md` Fase 14.1) — do not assume it works without checking the log first.

## Deploying / testing

- `manage_vita.py` — interactive menu to FTP a built VPK to a real Vita (VitaShell FTP server must be
  running), to FTP just the splash/title/touch PNGs (`ux0_data/zenonia-2/drawable/`, see below) without
  re-copying the rest of the assets, and to pull back the latest crash dump (`psp2core*`) + log file for
  analysis. Hardcoded `VITA_IP`; edit before running on a different network.
- Game data assets (from the extracted APK) live under `ux0_data/zenonia-2/` in this repo and must be
  copied to `ux0:data/zenonia-2/` on the target (real Vita or Vita3K's virtual `ux0`) separately from
  installing the VPK — see `INSTALL.md` for exact paths for both targets.
- On-console logs are written per-run to `ux0:data/zenonia-2/logs/log_<timestamp>.txt` (see `init_log()`
  in `loader/main.c`); crash dumps are `psp2core*.psp2dmp` files in `ux0:data`. Local copies of past
  sessions live in `logs/`.
- `clean_macos.sh` deletes macOS `._*` AppleDouble sidecar files, which VitaShell/vita_create_vpk
  can otherwise choke on or misinterpret as extra content.

## Architecture

### Loader pipeline (`loader/`)

1. **`so_util.c`/`.h`** — ELF32 loader: maps `libzenonia2.so` into memory (`so_file_load`), relocates it
   (`so_relocate`), resolves imports against the fake libc/OpenGL/Android tables (`so_resolve`), applies
   `so_hook`-style inline patches, then flushes I-cache (`so_flush_caches`) before running `.init_array`.
2. **`dynlib.c`** — the `so_default_dynlib default_dynlib[]` import table: every libc/pthread/Android/
   OpenGL ES symbol the `.so` expects to find, each backed by a real syscall, a vitaGL call, or a small
   compat shim (e.g. ABI differences between bionic and newlib `struct stat` — see `port_progress.md`
   §12.4 for a bug this caused). File path hooks (`fopen_hook`/`stat_hook`/`access_hook`) here redirect
   Android asset paths to `ux0:data/zenonia-2/assets/...`.
3. **`main.c`** — orchestrates load → relocate → resolve → patch → init, then sets up vitaGL, audio, and
   FalsoJNI, resolves the game's JNI entry points by name (`Java_com_gamevil_nexus2_Natives_*`), and runs
   the main loop: poll pad/touch, translate to the game's `MH_*`/HAL-keycode input protocol (each event is
   delivered twice, mirroring the original Java `setInputEvent` + `handleCletEvent` pattern — see the
   comment block above `queue_input_event`), call `NativeRender`, then swap buffers.
   - `apply_so_patches()` is where confirmed engine-side bugs get binary-patched directly in the loaded
     `.so` (currently one: a signed-pointer comparison in `CMvLayerData::PreLoad` that assumes Android's
     low, positive heap addresses — Vita's newlib heap lives at `0x81xxxxxx`, which reads as negative).
     Each patch checks the original bytes before touching them and no-ops with a log warning if they don't
     match, since these are hardcoded to one specific `.so` build (md5 noted in the comment).
4. **`java.c`** — the "Java-side" native callback handlers the engine calls back into via FalsoJNI
   (registered in `nameToMethodId[]`/`methodsObject[]`/etc., following FalsoJNI's method-table
   convention). This is where `readAssets`/`isAssetExist` (asset file I/O), UI status callbacks, and sound
   dispatch live. Any JNI method not registered here is treated by FalsoJNI as "not found" (logged,
   non-fatal) rather than needing a hardcoded stub table.
5. **`audio.c`/`.h`** — reimplements the APK's sound pipeline (`NexusSound.java`/`SoundMgr.java`) natively:
   a single `sceAudioOut` port + mixer thread, decoding `.ogg` assets with Tremor (fixed-point Vorbis,
   `vitasdk`'s `vorbisidec`) since the Vita has no hardware OGG decode. BGM/stream/SFX are all voices on
   the same mixer; which voice gets replaced vs. stacked on a new `OnSoundPlay` follows the original
   Android SoundPool/MediaPlayer semantics.
6. **`postprocess.c`/`.h`** — optional post-process shader path, see "Two build variants" above. Behind
   `#ifdef POSTPROCESS_SHADER`; the non-shader build compiles this file down to no-ops.
7. **`image_load.c`/`.h`** — PNG decode + cover-fit resize for the splash/title/touch overlays, see
   "Supporting pieces" below.

### Supporting pieces

- **`lib/falso_jni/`** — FalsoJNI: a fake Dalvik JNI environment. The game calls JNI functions expecting a
  real Android VM; FalsoJNI provides `JNIEnv`/`JavaVM` structures backed by simple lookup tables so those
  calls resolve to the C handlers in `java.c` instead. Java object/array layouts it returns (e.g. the
  `jbyteArray` from `readAssets`) must match what the game's *specific, pre-ART NDK build* expects — some
  code paths reach directly into Dalvik's internal `ArrayObject` layout rather than going through
  accessor functions, so FalsoJNI's own array helpers can't always be used as-is (see the comment above
  `Zenonia_readAssets` in `loader/java.c`).
- **`common/`** — `debugScreen.c`/`.h`: bitmap-font framebuffer console used only for fatal-error display
  (`fatal_error()` in `main.c`); not used during normal boot since vitaGL owns the framebuffer once
  active.
- **`sce_sys/`** — LiveArea assets (icons, background, bubble metadata) bundled into the VPK by
  `vita_create_vpk` in `CMakeLists.txt`.
- **`loader/image_load.c`/`.h`** — decodes `logo.png`/`title.png`/`touch.png` (the APK's own
  logo/title/touch-prompt drawables, read at runtime from `ux0:data/zenonia-2/drawable/`, *not* bundled
  in the VPK — see `ux0_data/zenonia-2/` below) with `stb_image` (vendored in `lib/stb/`, PNG-only) and
  resamples them with a bilinear "cover" fit (scale to fill the destination box, crop the overflowing
  axis) to the exact frames `main.c`'s `splash_load()`/`splash_draw()` draw over the engine's blank output
  during its Java-UI-only logo/title states (`g_ui_status` 0/1) — those two screens were originally drawn
  by Android Java UI, which doesn't exist in this environment, so the engine renders nothing without
  them. This replaced an earlier approach of shipping pre-baked `splash.rgba`/`title.rgba`/`touch.rgba`
  raw frames inside the VPK.
- **`apk_extract/` / `apk_decompiled/`** — the original APK's raw contents and a `jadx` decompilation of
  its Java layer. This is the ground truth for JNI method signatures, resource IDs (e.g. sound ID → file
  mapping), and UI/input behavior being replicated natively — consult these before guessing at engine
  behavior. `output/out_ghidra.c`/`.h` is a Ghidra decompilation of `libzenonia2.so` itself, used the same
  way for native-side behavior (e.g. locating the `CMvLayerData::PreLoad` patch site).
- **`ux0_data/zenonia-2/`** — the runtime asset tree deployed separately from the VPK (see Deploying
  above); not compiled into the app. Includes `assets/` and `sound/` (the game's own data) plus
  `drawable/` (the three splash-screen PNGs `image_load.c` reads, copied from `apk_extract/res/drawable/`
  — see above).

## Project history / where to look first

`port_progress.md` is a chronological, detailed dev log (Fases 1–14+) of every bug hit and fixed on real
hardware, including root causes confirmed via `vita-parse-core` crash dumps — it is the authoritative
record of *why* a given workaround exists, more detailed than the inline comments. `plan_zenonia_port.md`
is the original high-level task checklist (mostly superseded by `port_progress.md`'s later phases). Check
`port_progress.md`'s most recent phase before starting new debugging — it usually states the exact next
step and open questions. A "Backlog" section at the end lists deferred ideas (e.g. the postprocess
shader) explicitly marked not to implement until current bugs are confirmed resolved.

The `.claude/skills/psvita-porting/` skill captures generalized lessons from this port (hardware
debugging without network plugins, `.apk`/`.obb` asset fallback behavior, JNI stub gotchas, input
handling pitfalls, VPK packaging/installation errors, toolchain gotchas) — consult it for patterns likely
to recur in this or similar Android→Vita ports.

## Known issues (current)

- Logo/title screens are covered by splash images decoded from PNG at runtime (see `image_load.c` note
  above) rather than fixed at the engine level, since those were originally Java UI screens. If
  `ux0:data/zenonia-2/drawable/` wasn't deployed, these screens fall back to the engine's blank output
  (logged as "no encontrado", non-fatal).
- The experimental postprocess shader build (`zenonia_2_shader.vpk`) currently produces a black screen
  from the menu onward — do not treat it as a working alternative to the standard build.
