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
#include "config.h"
#include "error.h"
#include "fbf.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

so_module g_mod_game;
FbfNatives g_n;

/* ------------------------------------------ code writes: none in this game
 * The shared memory shims (bionic_mem.c) ask codespace.h first; the PvZ port
 * answers there for its mod's run-time hooks. This engine mmaps no code and
 * mprotects none, so every question gets "not mine". */
volatile int g_cs_armed;
void *cs_mmap(size_t len, int prot, const void *caller) { return NULL; }
int cs_munmap(void *addr, size_t len) { return 0; }
int cs_mprotect(void *addr, size_t len, int prot, const void *caller) { return 0; }
int cs_write(void *dst, const void *src, size_t n, int c, int kind) { return 0; }

/* libgcc's __sync_* on ARM Linux call the kernel's user helpers through
 * literal pools (kuser.S). This build of the engine has none (its atomics are
 * inline LDREX/STREX), but another build of it might: point any such literal
 * at ours, as the PvZ loader does, and report helpers not provided. */
void dcr_kuser_cmpxchg(void);
void dcr_kuser_memory_barrier(void);

static void fix_kuser_helpers(so_module *m) {
  int cmpxchg = 0, barrier = 0, other = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
      continue;
    uint32_t *w = (uint32_t *)((uintptr_t)((uint8_t *)m->load_base + ph->p_vaddr + 3) & ~3u);
    size_t nw = ph->p_filesz / 4;
    for (size_t k = 0; k < nw; k++) {
      if ((w[k] & 0xfffff000u) != 0xffff0000u || (w[k] & 0xfff) < 0xf60)
        continue;
      if (w[k] == 0xffff0fc0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_cmpxchg;
        cmpxchg++;
      } else if (w[k] == 0xffff0fa0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_memory_barrier;
        barrier++;
      } else if (w[k] == 0xffff0f60u || w[k] == 0xffff0fe0u || w[k] == 0xffff0ffcu) {
        if (other++ < 4)
          debugPrintf("[boot] %s+0x%x: kernel helper 0x%08x not provided\n", m->base_name,
                      (unsigned)((uintptr_t)&w[k] - (uintptr_t)m->load_base), (unsigned)w[k]);
      }
    }
  }
  if (cmpxchg || barrier || other)
    debugPrintf("[boot] %s: libgcc atomics -> kuser.S (%d cmpxchg, %d barrier%s)\n", m->base_name,
                cmpxchg, barrier, other ? ", others NOT handled" : "");
}

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
  int rc = so_load(&g_mod_game, path, NULL, SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  fix_kuser_helpers(&g_mod_game);
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
