/* main.c -- boot sequence for the Flappy Birds Family wrapper (32-bit).
 *
 * The order here matters; each step says why it is where it is. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "dcr_apkfind.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_sched.h"
#include "dcr_time.h"
#include "error.h"
#include "fbf.h"
#include "nx_init.h"
#include "selfproc.h"
#include "util.h"

void dcr_setup_update_from_nro(void); /* dcr_setup.c */
void dcr_setup_from_apk(const char *apk);
void dcr_boost_launch_begin(void);    /* dcr_boost.c */

static char g_root[256] = "sdmc:" FBF_ROOT_PATH;
const char *dcr_game_root(void) { return g_root; }

extern volatile uint32_t __dcr_reloc_path __attribute__((visibility("hidden")));

static void report_boot(void) {
  const u64 MB = 1024 * 1024;
  debugPrintf("[boot] === fbf_nx: Flappy Birds Family (dotGears engine, armeabi-v7a) ===\n");
  static const char *const paths[] = {"none needed", "patched through a writable alias (hardware)",
                                      "direct writes (emulator: pseudo-handle refused)"};
  debugPrintf("[boot] text relocations: %s\n",
              __dcr_reloc_path < 3 ? paths[__dcr_reloc_path] : "?");
  debugPrintf("[heap] total %u MB, used %u MB at start, heap region %u MB, heap %u MB @ %p\n",
              (unsigned)(g_nxinit.total / MB), (unsigned)(g_nxinit.used / MB),
              (unsigned)(g_nxinit.heap_region / MB), (unsigned)(g_nxinit.heap / MB),
              (void *)g_nxinit.heap_base);
  debugPrintf("[svc] sm=%x applet=%x hid=%x time=%x fs=%x sdmc=%x\n", g_nxinit.rc_sm,
              g_nxinit.rc_applet, g_nxinit.rc_hid, g_nxinit.rc_time, g_nxinit.rc_fs,
              g_nxinit.rc_sdmc);
  if (R_FAILED(g_nxinit.rc_time))
    debugPrintf("[svc] time service unavailable: clocks fall back to the system tick\n");
}

int main(int argc, char *argv[]) {
  mkdir(g_root, 0777);
  log_init(g_root);
  log_console_open(); /* blank: text only when asked or for setup work */
  report_boot();

  if (chdir(g_root) != 0)
    debugPrintf("[boot] WARNING: chdir(%s) failed\n", g_root);
  /* The folder had another name up to build 202609260029: what is still
   * there (the APK, config.ini, the saves) comes over first (dcr_apkfind.c). */
  dcr_move_old_folder("sdmc:" FBF_OLD_ROOT_PATH, g_root);
  dcr_config_load(); /* config.ini: controls, resolution, volume */
  dcr_boost_launch_begin(); /* CPU at 1785 MHz until the first picture (dcr_boost.c) */
  if (dcr_config()->boot_log)
    log_console_show_text();
  dcr_time_init();
  dcr_path_prepare_dirs();

  /* A newer build of this program in the launcher NRO: install it and
   * restart into it before anything else happens (dcr_setup.c). */
  dcr_setup_update_from_nro();

  /* The player's own APK, whatever it is called: the one in the folder that
   * holds this game's engine and art (dcr_apkfind.c). */
  char apk[512], why[300];
  if (dcr_find_apk(g_root, FBF_LIB, apk, sizeof apk, why, sizeof why) != 0)
    fatal_error("%s\n\n"
                "Copy the APK of your own Flappy Birds Family (com.dotgears.flapfire,\n"
                "armeabi-v7a) into %s. Any file name ending in .apk\n"
                "works: the game's code, pictures and sounds are read from it.",
                why, g_root);
  dcr_set_apk_path(apk);
  if (dcr_manifest_load(apk) != 0)
    fatal_error("%s is unreadable.\n\n"
                "Copy the APK of your own Flappy Birds Family (com.dotgears.flapfire,\n"
                "armeabi-v7a) into %s again.",
                apk, g_root);
  debugPrintf("[boot] APK: %s: %s %s (version code %d)\n", apk, dcr_manifest_package(),
              dcr_manifest_version_name(), dcr_manifest_version_code());
  /* The engine checks it (getPackageName, in dot_JNILib.init) and draws
   * nothing for another package: say so now rather than show a black screen. */
  if (strcmp(dcr_manifest_package(), FBF_PACKAGE))
    fatal_error("%s is %s, not Flappy Birds Family (" FBF_PACKAGE ").\n\n"
                "Copy the APK of your own Flappy Birds Family into %s.",
                apk, dcr_manifest_package(), g_root);

  if (dcr_self_process() == INVALID_HANDLE)
    fatal_error("Could not obtain a handle to this process.\n"
                "The loader needs it to map the game's code.");

  /* libflapfire.so and classes.txt, from the APK when they are missing or
   * it has changed */
  dcr_setup_from_apk(apk);
  if (fbf_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" FBF_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n"
                FBF_LIB " and .setup there to unpack it again. See debug.log.",
                g_root);

  /* The main thread becomes a guest thread like the game's own would be:
   * priority 59 on cores 0-2, where the kernel time-slices (dcr_sched.c). */
  dcr_sched_init();
  {
    dcr_audio_selftest();
    void dcr_pthread_selftest(void);
    dcr_pthread_selftest();
    void dcr_io_selftest(void);
    dcr_io_selftest();
  }
#if DCR_GL_MESA
  if (dcr_is_emulator() || dcr_config()->gl_selftest) {
    int dcr_gl_selftest(void);
    dcr_gl_selftest();
  }
#endif

  /* System.loadLibrary("flapfire"): the library's constructors (gnustl's
   * locale and streams, the engine's statics). */
  fbf_run_constructors();

  fbf_game_run();
  debugPrintf("[boot] exiting\n");
  log_flush_ring();
  return 0;
}
