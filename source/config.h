/* config.h -- build-wide constants for the Flappy Birds Family Switch wrapper.
 *
 * Flappy Birds Family (com.dotgears.flapfire 1.0.4, dotGears), armeabi-v7a:
 * one native module, libflapfire.so (dotGears' own engine, GLES 2). AArch32
 * host process built against libnx32 (see the Makefile). MIT.
 */
#ifndef DCR_CONFIG_H
#define DCR_CONFIG_H

/* Where on the SD card the game files live (named after the NRO,
 * flappybirdsfamily_nx.nro), where builds up to 202609260029 kept them
 * (moved from there on a start: dcr_apkfind.c), and the module name. The
 * player's own APK may have any name (dcr_apkfind.c finds it). */
#define FBF_ROOT_PATH  "/switch/flappybirdsfamily_nx"
#define FBF_OLD_ROOT_PATH "/switch/flappybirdsfamily"
#define DCR_ROOT_PATH  FBF_ROOT_PATH
#define FBF_LIB        "libflapfire.so"
#define FBF_PACKAGE    "com.dotgears.flapfire"
#define FBF_TITLE      "Flappy Birds Family"

/* The reserved region the game module is mapped into. libflapfire.so is
 * 0x9196c bytes (~0.6 MB) mapped; a module larger than this is refused by
 * so_load (-3). */
#define SO_REGION_BYTES (8u * 1024 * 1024)

/* Left outside the heap for kernel-side allocations. GPU buffers come from
 * the heap (libdrm_nouveau memaligns them and hands them to nvmap). */
#define GFX_RESERVE_MB  16u

/* The default window size until config.ini sets the rendering resolution
 * (the compositor scales it to the screen). */
#define DCR_FORCE_SCREEN_W 1280
#define DCR_FORCE_SCREEN_H 720

#define DEBUG_LOG 1

/* The renderer: 1 = mesa/nouveau (gl_mesa.c, portlibs32/ from
 * mesa32), 0 = null GL (gl_null.c: runs the game, draws
 * nothing). Set by the Makefile. */
#ifndef DCR_GL_MESA
#define DCR_GL_MESA 0
#endif

#endif /* DCR_CONFIG_H */
