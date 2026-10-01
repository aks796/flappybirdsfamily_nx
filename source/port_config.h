/* port_config.h -- Flappy Birds Family's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Flappy Birds Family"
#define PORT_NAME     "flappybirdsfamily_nx"
#define PORT_PACKAGE  "com.dotgears.flapfire"
#define PORT_BANNER   "fbf_nx: Flappy Birds Family (dotGears engine, armeabi-v7a)"
/* up to build 202609260029 the folder was /switch/flappybirdsfamily */
#define PORT_OLD_ROOT_PATHS "/switch/flappybirdsfamily"
/* libflapfire.so is 0x9196c bytes (~0.6 MB) mapped; a larger module is
 * refused by so_load (-3) */
#define PORT_SO_REGION_BYTES (8u * 1024 * 1024)

/* The APK, by what is in it -- the game's engine and its art -- whatever it
 * is called; with several, the highest version code. */
#define PORT_APK_DESC "Flappy Birds Family (com.dotgears.flapfire, armeabi-v7a)"
#define PORT_APK_ROLES                                                                        \
  {.what = "the game",                                                                        \
   .need = (const char *const[]){"lib/armeabi-v7a/libflapfire.so", "res/raw/atlas.png", NULL}, \
   .flags = RT_APK_HIGHEST_VERSION}
/* its engine draws nothing for another package: refuse it at boot */
#define RT_PACKAGE_MISMATCH_FATAL 1

/* ------------------------------------------------------------------ launcher */
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's engine from the APK)"
#define PORT_LAUNCHER_BYLINE     "by aks796 (the Switch port); the game by .GEARS"

/* ------------------------------------------------------------------ sound, input, frames */
#define RT_AUDOUT_FRAMES      512 /* the SoundPool mixes 512-frame blocks (fbf_audio.c) */
#define RT_BOOST_WATCH_THREAD 1   /* boost the long (loading) frames: the loop never polled it */
#define RT_PAD_MAX_PLAYERS    8   /* any controller connects; two of them play (fbf_input.c) */

#endif
