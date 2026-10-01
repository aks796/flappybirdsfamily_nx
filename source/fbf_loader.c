/* fbf_loader.c -- loading Flappy Birds Family's one module, libflapfire.so.
 *
 * The APK's lib/armeabi-v7a holds a single library, the whole game: dotGears'
 * engine (dot_Engine, the scenes, dot_GL) with gnustl linked in, Thumb-2 and
 * GLES 2. Its DT_NEEDED are system libraries only (liblog, libandroid, libEGL,
 * libGLESv2, libstdc++, libm, libc, libdl), all served by the shims: 150
 * imports, gl* through the GL layer, the rest from the import table
 * (tools/gen_imports.py). Nothing in it writes code at run time, so the module
 * is mapped the plain way: staged, relocated, resolved, then sealed as code
 * (RX text, RW data) before anything in it runs.
 *
 * Its natives are exported by name (Java_com_dotgears_dot_1JNILib_*: the
 * Java loads the library with System.loadLibrary and binds them lazily; there
 * is no JNI_OnLoad or RegisterNatives), and are looked up once into g_n. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "codespace.h"
#include "dcr_path.h"
#include "config.h"
#include "error.h"
#include "fbf.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"


so_module g_mod_game;
FbfNatives g_n;

/* Code writes: none in this game. The runtime's codespace defaults
 * (so_util.c) answer the shared memory shims for a loaded module's pages.
 *
 * libgcc's __sync_* on ARM Linux call the kernel's user helpers through
 * literal pools. This build of the engine has none (its atomics are inline
 * LDREX/STREX), but another build of it might: so_fix_kuser_helpers points
 * any such literal at the runtime's kuser.S and reports helpers it lacks. */

/* ---------------------------------------------------------------- natives */
#define JNI_ "Java_com_dotgears_dot_1JNILib_"

static const struct {
  const char *name;
  size_t off;
  int required;
} k_natives[] = {
#define NAT(n, req) {#n, offsetof(FbfNatives, n), req}
    NAT(init, 1),           NAT(setHighScore, 1),     NAT(getHighScore, 1),
    NAT(setAtlas, 1),       NAT(step, 1),             NAT(setInputDevices, 1),
    NAT(getOutputEventCount, 1), NAT(getOutputEvents, 1), NAT(resetOutputEvent, 1),
    NAT(keypressed, 1),     NAT(keyreleased, 0),      NAT(touchPressed, 1),
    NAT(touchReleased, 0),  NAT(pause, 1),            NAT(resume, 1),
    NAT(stop, 0),           NAT(exit, 0),             NAT(getSceneId, 1),
#undef NAT
};

static const char *g_build = "unknown";
const char *fbf_engine_build(void) { return g_build; }

static int bind_natives(void) {
  int missing = 0;
  char sym[96];
  for (unsigned i = 0; i < sizeof k_natives / sizeof k_natives[0]; i++) {
    snprintf(sym, sizeof sym, JNI_ "%s", k_natives[i].name);
    uintptr_t a = so_try_find_addr_rx(&g_mod_game, sym);
    memcpy((uint8_t *)&g_n + k_natives[i].off, &a, sizeof a); /* a function pointer's slot */
    if (!a) {
      debugPrintf("[boot] %s native %s%s\n", k_natives[i].required ? "MISSING" : "no", sym,
                  k_natives[i].required ? "" : " (optional)");
      missing += k_natives[i].required;
    }
  }
  /* 1.0.4 added keyreleased (menus act on the release) and the package check;
   * 1.0 has neither. Both run here. */
  g_build = g_n.keyreleased ? "1.0.4" : "1.0";
  return missing;
}

/* -------------------------------------------------------------- loading */
int fbf_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), FBF_LIB);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  so_fix_kuser_helpers(&g_mod_game);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  if (bind_natives()) {
    debugPrintf("[boot] %s is not the Flappy Birds Family engine this port knows\n", FBF_LIB);
    return -2;
  }
  debugPrintf("[boot] engine: Flappy Birds Family %s (by its natives), mapped at %p\n", g_build,
              g_mod_game.load_virtbase);
  return 0;
}

/* Android runs a library's constructors inside System.loadLibrary, which
 * dot_JNILib's static initialiser calls before the first native: gnustl's
 * locale and iostream set-up, the engine's statics. */
void fbf_run_constructors(void) {
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done\n", FBF_LIB);
}
