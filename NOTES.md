# Notes: flappybirdsfamily_nx

Technical notes from porting Flappy Birds Family (armeabi-v7a) to the Switch as
a 32-bit (AArch32) process. They cover what the 32-bit libraries needed,
what was specific to this game, and what may help anyone porting another
32-bit Android game.

**The android32 runtime.** Since build 202610010027 the shared files -- the
loader, bionic shims, JNI core, GL glue, audout, clocks, paths, setup, the
config engine, crash handler, watchdog, `main()` and the launcher -- come from
android32 (`runtime/`, commit `81b772b`), shared by all the 32-bit ports. This
repository keeps Flappy Birds Family's own code (`source/fbf_*.c`, its
`port_config.h`, `dcr_config.c` option table and `fbf_main.c`). File names
below that are not `fbf_*` now refer to `runtime/source/`.

Toolchain used:

* **AArch32 wrapper**: devkitARM from the vita2hos container
  (`ghcr.io/vita2hos/devcontainer/vita2hos:latest`), with libnx32 replaced by
  the patched build, [libnx32](https://github.com/aks796/libnx32) (branch
  `master` at `41b61f92`, version 4.12.0: vita2hos/libnx `721c977` with
  switchbrew/libnx master merged in `9b4f3b29`, plus the 32-bit fixes
  `cb01ef9f` and `c6c53d20`).
* **Graphics**: [mesa32](https://github.com/aks796/mesa32): libdrm_nouveau
  1.0.1 and Mesa 20.1.0-rc3, devkitPro's `switch-20.1.0-rc3` branch with the
  fixes below on top, cross-built for AArch32 and copied into
  `portlibs32/`.
* **Launcher NRO**: 64-bit, devkitA64 and upstream libnx (`devkitpro/devkita64`).

Build 202609300923 and later link against that libnx32 and mesa32.

---

## 1. The 32-bit libraries

What this port needed from libnx32, newlib and Mesa, and where each item
stands. Items fixed upstream keep the port's own version: each was proven on
hardware, and the library's fix has not been run on a console with this game
yet. Nothing else in the new libraries clashes with them (the link map shows
the port's `__libnx_initheap`, `__appInit`, `nwindowGetDefault`, virtmem,
exception entry and relocator in use).

### Summary

| Item | Status | This port |
| --- | --- | --- |
| `svcSetThreadCoreMask` mask was `u32` | fixed, libnx32 `c6c53d20` | own SVC, `dcr_sched.c` |
| `svcGetThreadCoreMask` stack | fixed, `c6c53d20` | own SVC, `dcr_sched.c` |
| No `svcWaitForAddress` / `svcSignalToAddress` stubs | fixed, `c6c53d20` | own SVCs + boot self-test, `bionic_pthread.c` |
| `AudioOutBuffer` layout | fixed, `c6c53d20` | own IPC, `fbf_audio.c` |
| `virtmem.c` 32-bit bounds and search region | fixed, `c6c53d20` | `nx32_virtmem.c` |
| `__libnx_initheap` asks for more than the 1 GiB heap region | fixed, `c6c53d20` | `nx_init.c` (also keeps 16 MB out for the GPU driver) |
| `__libnx_exception_entry` was a stub | fixed, `c6c53d20` | `exc32.S` |
| `armICacheInvalidate` was a no-op | fixed, `c6c53d20` | `code_flush.c` |
| No real own-process handle | fixed, `c6c53d20`: `envAcquireOwnProcessHandle()` | `selfproc.c` |
| `switch32.ld` without `.rel.dyn` | fixed, `c6c53d20` | `dcr32.ld` (also needs its page-0 layout) |
| `timespec_get` missing | fixed, `c6c53d20` (weak) | `host_compat.c` |
| Enums in system data were short | fixed, `cb01ef9f` | links with `--no-enum-size-warning` |
| Text relocations in devkitARM's non-PIC libraries | **open** | `crt0_reloc.c`, `dcr32.ld`, `dcr32.specs` |
| `__appInit` aborts on any service failure | **open** | `nx_init.c` |
| newlib soft-float `setjmp` and libm | **open** | `bionic_setjmp.S`, `bionic_math.c` |
| newlib `access()` over fsdev | **open** | `stat()` in `bionic_io.c` |

A side effect of the `svcSetThreadCoreMask` fix: newlib's `pthread_create`
(libsysbase over libnx) used to fail on 32-bit, and now works.
* In this port the only linked code that would start a thread through it is
  Mesa's `util_queue`, pulled in by glthread. glthread only runs after
  `switch_egl_start_glthread`, which this port does not call, so no new
  threads start here.
* The game's own threads go through `bionic_pthread.c`, which uses libnx
  threads directly (priority 59, cores 0–2).

### Fixed in libnx32 (`c6c53d20`)

**`svcSetThreadCoreMask`** (`svc.h`, `svc32.s`): the mask was declared `u32`
while the kernel reads a 64-bit mask from `r2:r3`. `r3` held whatever the
caller left there, the kernel returned InvalidCoreId, and every thread stayed
on its creation core. The port issues the SVC itself with the high word zeroed
(`source/dcr_sched.c`).

**`svcGetThreadCoreMask`** (`svc32.s`): the stub pushed 12 bytes and removed
8, restoring `r4` from the wrong slot. The port reads the mask with its own
SVC.

**`svcWaitForAddress` / `svcSignalToAddress`**: there were no AArch32 stubs.
The layouts that work on Atmosphere (firmware 21.x) and Ryujinx 1.1.1098:
* `svcWaitForAddress` (0x34): `r0` address, `r1` type, `r2` value (32-bit),
  `r3:r4` timeout. The layout with a 64-bit value (`r2:r3`, then `r4:r5`)
  timed out wrongly on both.
* `svcSignalToAddress` (0x35): `r0` address, `r1` type, `r2` value, `r3`
  count.

libnx32's new stubs use the same layout. The port keeps its own
(`arb_wait_if_equal`, `arb_signal` in `source/bionic_pthread.c`) and
`dcr_pthread_selftest()`, which times a wait at boot and switches layouts if
needed.

**`AudioOutBuffer`** (`audout.h`): pointers were 4 bytes while audout reads the
64-bit descriptor. libnx32 now sends the 0x28-byte layout and u64 tags. The
port sends append and get-released itself (`source/fbf_audio.c`, checked by a
boot self-test).

**`virtmem.c`**:
* Region ends wrapped at `0x1_0000_0000`.
* The search looked through the whole ASLR region, while a 32-bit process
  may only map shared, transfer and code memory in the code region
  `[0x200000, 0x40000000)`. Anything else failed, seen as `MapSharedMemory` =
  InvalidCurrentMemory in `hidInitialize`.

The port's `source/nx32_virtmem.c` replaces the whole object.

**`__libnx_initheap`** (`init.c`): it asked for total memory minus used (about
3 GB on hardware) while a 32-bit heap region is 1 GiB
(`0x80000000`–`0xC0000000`). libnx32 now clamps and retries. The port's
`source/nx_init.c` also leaves 16 MB outside the heap for the GPU driver's
kernel-side allocations.

**`__libnx_exception_entry`** (`exception32.s`): was a `TODO` stub. libnx32
now saves `r8`–`r12`, `d0`–`d31` and FPSCR on its own stack and calls an
optional `__libnx_exception_handler32`. The port's `source/exc32.S` does the
same and `source/exc_handler.c` writes `crash.log`.

**`armICacheInvalidate`** (`cache.h`): was `(void)0`. libnx32 now cleans the
data cache and flips a spare code page's permission, which makes the kernel
invalidate every core's instruction cache. The port's `source/code_flush.c`
does the same. This game writes no code at run time; the mechanism is kept
for loader patches.

**Own process handle**: `svcMapProcessCodeMemory` and `svcMapProcessMemory`
reject `CUR_PROCESS_HANDLE`, and an ExeFS override gets no handle from a
loader. libnx32 now has `envAcquireOwnProcessHandle()`. The port's
`source/selfproc.c` sends `CUR_PROCESS_HANDLE` as a copy handle over a session
to itself, the same technique.

**`switch32.ld`**: now places `.rel.dyn`/`.rel.plt`. The port keeps `dcr32.ld`,
which it needs for its page-0 layout anyway (below).

**`timespec_get`**: newlib declared it without implementing it, and Mesa's
threads need it. libnx32 now has a weak one; the port's
`source/host_compat.c` is used when anything references it.

**Short enums** (`cb01ef9f`): devkitARM builds with `-fshort-enums`, so any
enum in data the system reads had the wrong size. `tools32/
check_short_enums.sh` lists the structs whose layout depends on enum size. Any
new struct the system reads must use `u32` for enum fields.

### Still open

**Text relocations** (`runtime/dynamic.c`, devkitARM's target libraries):
* newlib, libsysbase and libstdc++ for devkitARM are not built `-fPIC`, so a
  PIE link has `R_ARM_RELATIVE` relocations in `.text` and `.rodata`.
  `__nx_dynamic` writes to those pages directly, which faults.
* Making a code page writable is no way out on Mesosphere: it becomes
  CodeData and can never be executable again (`svcBreak` 0xDC03).
* The port's `source/crt0_reloc.c` maps each kernel memory block of `.text`
  and `.rodata` through a writable alias (`svcMapProcessMemory`), patches
  through it, and unmaps it. It has to be one block per call: `.text` and
  `.rodata` together fail with InvalidCurrentMemory (0xD401).
* `dcr32.ld` keeps page 0 for crt0 and the relocator, and `dcr32.specs`
  links with `-z notext`.
* Fix: build the target libraries `-fPIC`, or move this relocator into
  libnx32's crt0.

**`__appInit`**: aborts on any service failure. Under Ryujinx the time
service's shared memory fails to map for 32-bit processes while everything
else works. The port's `nx_init.c` records failures and logs them.

**newlib, soft float**:
* `setjmp` does not save `d8`–`d15`, which AAPCS makes callee-saved for VFP
  code. The port uses `source/bionic_setjmp.S`.
* libm does double arithmetic through libgcc calls: correct, but slow. The
  port's `source/bionic_math.c` does sqrt, abs, rounding and min/max in VFP.

**newlib `access()`**: unreliable over fsdev. The port's `source/bionic_io.c`
uses `stat()`.

**ABI differences from bionic** (not bugs, but any shim layer has to convert
them): `off_t` is 32-bit in bionic; `timespec`/`timeval`; `mbstate_t` is 8
bytes in newlib and 4 in bionic; errno values above 34 differ. See
`source/bionic.h`.

### mesa32 (Mesa 20.1 for AArch32)

**Fixes** (commits in [mesa32](https://github.com/aks796/mesa32) on top of
devkitPro's branch):
* `099a02a3` (don't depend on int-sized enums): `mesa_format` was a 16-bit
  enum, and `st_choose_matching_format` crashed on the first
  RGBA/UNSIGNED_BYTE texture upload (hardware). It also fixes an enum
  bit-field in `tgsi_info.h`.
* From the other ports:
  * `24aa14fe`: `thrd_success` in `u_thread.h`;
  * `4e41d89f`: `eglQuerySurface` sizes;
  * `2c27955c`: ETC2/ASTC on chipset 0x120;
  * `dddc69a4`: render-to-texture without storage;
  * `972de9c1`: glthread, opt-in through `switch_egl_start_glthread` (this
    port does not use it).

**Handled in this port:**
* **Window configs.** The Switch EGL platform offers RGBA8888 only, without
  MSAA; a failed config request is retried without multisampling
  (`source/gl_mesa.c`).
* **Alpha.** The compositor uses the window's alpha, and a game that asked
  for an opaque RGB565 window can leave any alpha in the frame. The port
  forces alpha to 1 before each present (`fbf_overlay_opaque`).

**Good to know:**
* GPU memory comes from the heap: libdrm_nouveau memaligns its buffers and
  hands them to nvmap.
* Every `gl*` name resolves through Mesa's `eglGetProcAddress`
  (EGL_KHR_get_all_proc_addresses).

### Kernel behaviour a 32-bit port has to plan for

These are not library bugs.

* **Address space:** code region `[0x200000, 0x40000000)`, alias region at
  `0x40000000` (1 GiB), heap region at `0x80000000` (1 GiB).
* **Scheduling:** Horizon only time-slices at priority 59 on cores 0–2 (63 on
  core 3), rotating that queue every 10 ms. At other priorities a runnable
  thread keeps its core until it blocks. Guest threads that spin-wait
  therefore need priority 59 and a core mask of 0–2 (`source/dcr_sched.c`).
* **Faults:** on a fault the kernel re-enters the program at its entry point
  with a stack of under 448 bytes, and restores only what it saved.
* **HOME and sleep** freeze the whole process.
  * With libnx's default focus mode (`SuspendHomeSleep`) no focus message
    arrives for them, so a focus hook never runs. This port's hardware log
    showed a 45 s HOME visit and no "focus lost" line.
  * The system tick keeps counting through the freeze, so the first frame
    after it sees the whole gap.
  * This port sets `AppletFocusHandlingMode_SuspendHomeSleepNotify`, and also
    takes any frame longer than 2 s for a freeze. It then does what Android
    did around HOME: held keys are let go, and the engine is paused and
    resumed (`fbf_game.c`).
  * The watchdog does not report a freeze as a hang (`watchdog.c`).
  * The clocks are not shifted: the engine steps from `gettimeofday` but
    never by more than 25 ms a frame (`dot_Engine::enterFrame`), so a freeze
    costs one 25 ms step.
  * A game that steps from real time without such a cap needs the gap
    removed from its clock (the Disney Crossy Road: SEA and Asphalt 8 ports
    do this).

---

## 2. What was specific to this game

**The engine.** `lib/armeabi-v7a/libflapfire.so` is dotGears' own C++ engine:
Thumb-2, gnustl, GLES 2, symbols intact. It has no audio, no threads and no
file I/O of its own.
* The Java side owns the GL context, uploads the texture atlas, plays the
  sounds and keeps the high score. The wrapper reimplements `GameActivity`,
  the `GLSurfaceView` and its renderer, `SoundPool` and `SharedPreferences`
  in C (`source/fbf_*.c`).
* Everything runs on one thread in the Android per-frame order: input,
  `setInputDevices`, `step`, output events, present.

**Loader.**
* 93 imports: 61 bionic shims and 32 passed straight to newlib, with none
  missing (`tools/gen_imports.py --check`). The 37 `gl*` imports go to Mesa.
* The engine is mapped into an 8 MB code region (0.6 MB used).
* `fix_kuser_helpers` (for old-libgcc `__sync` code that calls the Linux
  kuser page at `0xffff0fc0`) is kept from the PvZ port, but this engine has
  no such calls.

**JNI.**
* No `JNI_OnLoad` and no `RegisterNatives`: the natives are resolved by name.
* The engine's only callback is `getPackageName()`. It must answer
  `com.dotgears.flapfire`, or the engine never draws; the answer comes from
  the APK's own manifest.
* Engine 1.0 has no `keyreleased` and no package check, so those natives are
  optional.
* `FindClass` answers from the class list of the APK's dex files
  (`classes.txt`, made at setup).

**The atlas** (`res/raw/atlas.png`) is decoded and put through Android's
premultiply/unpremultiply round trip, so the texture is byte-for-byte what
`Bitmap.getPixels` gave the Java. Fully transparent texels come out black.

**Output events.**
* `getOutputEvents` leaves the array slots of engine events that have no Java
  code (3, 4, 5, and 14 on 1.0) holding uninitialised stack data, which can
  look like a sound code.
* The port reads the engine's queue (`FlapListener::sharedInstance`: count
  +4, codes +8, data +0x58) to skip those slots. The same queue carries a
  finished 1P round's score with event 0.
* `getOutputEvents` also raises the engine's high score when it handles
  event 0.

**Input.**
* GameActivity lets one key per device through at a time: while a key is
  held, that device's other key-downs are dropped.
* 1.0.4 acts on key release, 1.0 on press.
* Players join by device id on the main menu, and `setInputDevices` is sent
  every frame. Only two devices are ever listed, so any other controller
  stays connected but plays no part.
* The stick becomes D-pad keys at 0.5, as Android's
  SyntheticJoystickHandler does.
* A single Joy-Con held sideways reports its stick and buttons as if held
  upright, whatever the hold type. The port rotates the stick (left Joy-Con
  anticlockwise, right one clockwise) and maps the four buttons by position.

**Engine objects.**
* The main-menu fixes read `MainScene`, its `SelectorButton`s and its
  `dot_ToggleButton` (the mode button) by offset, after checking each vtable.
* A wrong class guess (ActiveButton) turned the feature off on hardware.
  `tools/check_engine.py` now re-derives every offset, class and event code
  from the APK's own code for both engine builds, and compares them with the
  C source.

**Rendering.** The engine draws into its own 768x432 framebuffer and scales it
to the window (1280x720 or 1920x1080).

**Sound.** The six Ogg Vorbis sounds are decoded once (stb_vorbis). A
10-voice mixer resamples 44.1 kHz to 48 kHz and feeds audout 512-frame
blocks, three in flight. Measured on hardware: 93.75 blocks/s, matching
48000 / 512.

**Leaderboard** (added by the port). It is drawn from the user's atlas at
run time, using the score panel, menu squares, arrows, buttons and digits.
Its lettering follows rules measured from the game's art: redrawing GAME OVER
and GET READY by those rules matches except for a few hand-drawn pixels, and
the score digits and the panel's BEST label match exactly
(`tools/test_ui.py`).

**Launch.**
* A 32-bit program cannot be an NRO, because hbloader is 64-bit. The 64-bit
  launcher NRO carries the wrapper (`fbf_nx.nsp`) in its romfs.
* Started from a sphaira forwarder (title id `05…`), the launcher writes
  `/atmosphere/contents/<title id>/exefs.nsp`, with the NPDM's program id
  retargeted in ACI0 and ACID, and restarts the title.
* Later, the wrapper updates itself from any newer NRO in its folder.
* The romfs name `fbf_nx.nsp` must stay, because older builds look for it
  there.

**Folder and APK.**
* The game folder is `/switch/flappybirdsfamily_nx/`. Builds up to
  202609260029 used `/switch/flappybirdsfamily/`; the wrapper moves what is
  there (all but `.nro` files) on its first start (`source/dcr_apkfind.c`).
* The APK may have any name. The one that contains
  `lib/armeabi-v7a/libflapfire.so` and `res/raw/atlas.png` is used; with
  several, the highest version code wins.

**Setup screen.**
* The first launch, a new APK or a new build shows the PvZ Touch port's green
  progress bar on the boot console: the game's name, what is being done, and
  a bar (`log_console_progress` in `source/util.c`, staged in
  `source/dcr_setup.c`).
* The steps are "Unpacking the game's engine" (by bytes written), "Reading
  the game's Java classes" and "Starting the game". An update from a newer
  NRO shows "Updating to the new build, then restarting".
* A start with nothing to set up shows nothing. The log goes to `debug.log`,
  or scrolls on screen instead with `boot_log_on_screen`.
* The console is closed for good before EGL takes the window: Mesa registers
  3 buffer slots and the console 2, so a console frame after Mesa fails with
  0x2B59.

---

## 3. For anyone porting another 32-bit game

* **Start from the APK.**
  * List `lib/armeabi-v7a/` and every library's imports; a script like
    `tools/gen_imports.py` tells you which shims are needed before anything
    runs.
  * Read the Java (smali) for what it does around the natives: many engines
    leave the GL context, sound and saves to Java.
* **Match the ABI.** armeabi-v7a is softfp: VFP instructions, floats in core
  registers. Building the host with `-mfloat-abi=softfp` makes shims and
  callbacks agree without per-function attributes.
* **Check anything that crosses to the system for enum size** (IPC, applet
  storage, shared memory); devkitARM makes enums small.
* **Do not make code pages writable.** Patch through an alias mapping, one
  memory block at a time.
* **Get a real process handle through self-IPC** before mapping code memory.
* **Put spin-waiting guest threads at priority 59 on cores 0–2**, and set
  core masks with a correct 64-bit SVC.
* **Bionic sync objects are one 32-bit word each** on armeabi-v7a. Static
  initialisers (0, 0x4000, 0x8000 for mutexes) have to be decoded in place.
* **Verify engine offsets against the disassembly with a script**, for every
  engine build you support. A host test built on your own reading of the code
  will also pass when that reading is wrong.
* **Test on hardware for the kernel side.** Ryujinx (1.1.1098):
  * refuses the pseudo-handle in `svcSetProcessMemoryPermission`;
  * does not enforce page permissions;
  * fails the time service's shared memory for 32-bit processes;
  * uses the older `svcWaitForAddress` layout;
  * cannot translate `MRC p15, c14` (CNTFRQ), which libnx's
    `armGetSystemTickFreq()` reads on AArch32. Use `armNsToTicks()` (a fixed
    19.2 MHz conversion) instead; this port does;
  * cannot execute the A32 fixed-point `VCVT` that nouveau's
    `nv50_sampler_state_create` uses. Up to build 202609301843 the game
    stopped at its splash picture there.
  * Since the android32 runtime (build 202610010027), its emulator fix-ups
    (`RT_EMU_FIXUPS`, the default 1) rewrite those instructions under an
    emulator only; on hardware nothing is rewritten. The game then gets past
    its splash, through the JNI, preferences, audio, input and the first
    frames, and Ryujinx stops later, on a memory exception inside the engine
    (a sign-extended address, `libflapfire.so+0x264a2`).
  * The GL self-test's triangle reads black (000000) under those fix-ups, an
    emulator-only result.
  * The setting stays 1, because reaching the game helps testing more than a
    passing self-test does; `#define RT_EMU_FIXUPS 0` in `port_config.h`
    gives the old behaviour.
* **Log a lot, but not from the frame loop.** This port writes the log to a
  RAM ring during play and flushes it every 10 s and on a crash.
