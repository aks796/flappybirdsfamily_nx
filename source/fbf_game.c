/* fbf_game.c -- plays the part of the game's Java: GameActivity, its
 * GLSurfaceView (com.dotgears.game.f) and renderer (j).
 *
 * What the Java does, and what this file does for it:
 *
 *   SplashScreen            res/drawable/splash.png on black for 2 s (the
 *                           game's art and sounds are read meanwhile)
 *   GameActivity.onCreate   SoundPool with the six sounds (fbf_audio.c);
 *                           SharedPreferences "flapfire": score, playcount
 *                           (+1, saved at once), rated (fbf_prefs.c)
 *   GLSurfaceView           an EGL context: OpenGL ES 2, the window surface
 *                           (here: libnx's default window, through the
 *                           shared EGL layer, gl_mesa.c)
 *   renderer.onSurfaceChanged (the first time)
 *                           dot_JNILib.init(activity, w, h); setHighScore
 *                           (score); atlas.png as a GL texture (RGBA, LINEAR,
 *                           com.dotgears.a) and setAtlas(texture, atlas_text,
 *                           length) -- the engine is made there
 *   renderer.onDrawFrame    setInputDevices(InputDevice.getDeviceIds());
 *                           step(); GameActivity.b(): the engine's output
 *                           events (sounds, "save the high score")
 *   onKeyDown / onKeyUp     one key at a time per device (a SparseIntArray);
 *                           BACK on the main menu (scene 1) closes the game;
 *                           otherwise keypressed / keyreleased(code, device)
 *   GameView.onTouchEvent   touchPressed / touchReleased (x, y)
 *   onPause / onResume      pause() / resume(-1, -1): the engine stops its
 *                           clock and updates, and restarts them
 *
 * On Android the UI thread (input) and the GL thread (frames) run side by
 * side; here one thread does both, in the order a frame sees them: input,
 * the device list, step, events, present. The engine is never called from two
 * threads at once.
 *
 * The Switch side: + pauses (the engine's own pause, with the overlay drawn
 * by fbf_overlay.c) and resumes; HOME pauses the engine and the sound until
 * the game is in focus again; closing the game (HOME > Close, or Back on the
 * main menu) stops the engine and saves the preferences. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "dcr_time.h"
#include "error.h"
#include "fbf.h"
#include "fbf_gl.h"
#include "gl_layer.h"
#include "jni.h"
#include "util.h"

void dcr_watchdog_start(void);
void dcr_boost_launch_end(void);
void dcr_boost_report(void);
const char *dcr_game_root(void);

#define ENV g_jni_env
#define CLS g_jnilib

/* The shared EGL layer (gl_mesa.c / gl_null.c), as plain C: both define
 * these with the same register-level signatures. */
typedef int32_t fEGLint;
void *b_eglGetDisplay(void *native);
unsigned b_eglInitialize(void *d, fEGLint *maj, fEGLint *min);
unsigned b_eglChooseConfig(void *d, const fEGLint *attrs, void **cfgs, fEGLint cap, fEGLint *num);
void *b_eglCreateWindowSurface(void *d, void *cfg, void *win, const fEGLint *attrs);
void *b_eglCreateContext(void *d, void *cfg, void *share, const fEGLint *attrs);
unsigned b_eglMakeCurrent(void *d, void *draw, void *read, void *ctx);
unsigned b_eglSwapInterval(void *d, fEGLint interval);
unsigned b_eglSwapBuffers(void *d, void *s);
unsigned b_eglDestroySurface(void *d, void *s);
unsigned b_eglDestroyContext(void *d, void *c);
unsigned b_eglTerminate(void *d);
fEGLint b_eglGetError(void);

#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

static FbfPrefs g_prefs;
static FbfKeyGate g_gate;
static volatile int g_exit;
static volatile int g_focused = 1, g_focus_changed;
static int g_engine_up;     /* init + setAtlas done: the renderer's "j.a" */
static int g_user_paused;   /* + */
static int g_swallow_touch_up;
static float g_pause_t;
static int g_w, g_h;
static void *g_dpy, *g_surf, *g_ctx;

/* For the watchdog: frames presented, and whether frames are expected. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }
int dcr_boot_in_focus(void) { return g_focused && g_engine_up; }

int fbf_user_paused(void) { return g_user_paused; }

int fbf_scene(void) { return g_engine_up ? g_n.getSceneId(ENV, CLS) : 0; }

/* ------------------------------------------------------------- input */
void fbf_key(int down, int code, int dev) {
  if (!g_engine_up)
    return;
  if (dcr_config()->log_input)
    debugPrintf("[input] key %s %d from device %d (scene %d)\n", down ? "down" : "up", code, dev,
                fbf_scene());
  if (fbf_ui_active()) {
    fbf_ui_key(down, code, dev); /* the name entry or the leaderboard has them (fbf_ui.c) */
    return;
  }
  /* the edge the engine acts on: the release (1.0.4), the press (1.0) */
  const int acts = g_n.keyreleased ? !down : down;
  if (down) {
    if (!fbf_gate_down(&g_gate, dev))
      return; /* another key of this device is down: GameActivity drops it */
    if (code == AK_BACK && fbf_scene() == 1) {
      if (dcr_config()->back_exits) {
        debugPrintf("[game] Back on the main menu: closing the game (as on Android)\n");
        if (g_n.exit)
          g_n.exit(ENV, CLS);
        g_exit = 1;
      }
      return;
    }
    if (acts && fbf_menu_key(code, dev))
      return; /* the main menu's Up / Down (fbf_menu.c) */
    g_n.keypressed(ENV, CLS, code, dev);
  } else {
    fbf_gate_up(&g_gate, dev);
    if (code == AK_BACK && fbf_scene() == 1)
      return;
    if (acts && fbf_menu_key(code, dev))
      return;
    if (g_n.keyreleased)
      g_n.keyreleased(ENV, CLS, code, dev);
  }
}

void fbf_engine_key(int code, int dev) {
  if (!g_engine_up)
    return;
  g_n.keypressed(ENV, CLS, code, dev);
  if (g_n.keyreleased)
    g_n.keyreleased(ENV, CLS, code, dev);
}

void fbf_touch(int down, int x, int y) {
  if (!g_engine_up)
    return;
  if (!down && g_swallow_touch_up) {
    g_swallow_touch_up = 0; /* the lift of the tap that ended the pause */
    return;
  }
  if (g_user_paused) {
    if (down) { /* a tap on the paused screen resumes */
      g_swallow_touch_up = 1;
      fbf_pause_toggle();
    }
    return;
  }
  if (dcr_config()->log_input)
    debugPrintf("[input] touch %s at %d,%d\n", down ? "down" : "up", x, y);
  if (fbf_ui_active()) { /* in the 768x432 picture's pixels, fitted to the view as the engine's */
    const float sc = fminf((float)g_w / FBF_UI_W, (float)g_h / FBF_UI_H);
    fbf_ui_touch(down, (int)(((float)x - ((float)g_w - FBF_UI_W * sc) / 2) / sc),
                 (int)(((float)y - ((float)g_h - FBF_UI_H * sc) / 2) / sc));
    return;
  }
  if (down)
    g_n.touchPressed(ENV, CLS, x, y);
  else if (g_n.touchReleased)
    g_n.touchReleased(ENV, CLS, x, y);
}

void fbf_pause_toggle(void) {
  if (!g_engine_up)
    return;
  if (!g_user_paused) {
    g_user_paused = 1;
    g_pause_t = 0.0f;
    fbf_input_swallow_held();
    memset(&g_gate, 0, sizeof g_gate);
    g_n.pause(ENV, CLS);
    debugPrintf("[game] paused (+)\n");
  } else {
    g_user_paused = 0;
    g_n.resume(ENV, CLS, -1, -1);
    debugPrintf("[game] resumed\n");
  }
}

/* -------------------------------------------------- GameActivity.b() */
/* getOutputEvents: the engine's event -> the Java's code, -1 none (its
 * slot in the array is left as it was: noise). tools/check_engine.py
 * checks this against the engine. */
static const signed char k_java_code[] = {9, 10, 11, -1, -1, -1, 1, 2, 3, 4, 5, 6, 7, 8, 12};
/* how many of them the engine's switch covers: 1.0 (no keyreleased) has no
 * button sound, event 14 */
#define JAVA_CODES_104 15
#define JAVA_CODES_10 14

/* The UI opened or closed: keys held now end there (no key-up to the game,
 * which acts on releases), and the one-key rule starts afresh. */
static void ui_handover(void) {
  if (fbf_ui_take_swallow()) {
    fbf_input_swallow_held();
    memset(&g_gate, 0, sizeof g_gate);
  }
}

static void output_events(void) {
  const int count = g_n.getOutputEventCount(ENV, CLS);
  if (count > 0) {
    /* the engine's queue first (fbf_menu.c): which of the array's places
     * are noise, and a finished round's score */
    int codes[FBF_EVENTS_MAX], data[FBF_EVENTS_MAX];
    const int queued = fbf_engine_events(codes, data, FBF_EVENTS_MAX);
    const int exact = queued == count;
    static int told;
    if (!told) {
      told = 1;
      debugPrintf("[game] the engine's event queue: %s\n",
                  exact ? "read (noise in the Java's array skipped, scores seen)"
                        : "not as this port knows -- the Java's array alone, no name entry");
    }
    JObj *arr = g_n.getOutputEvents(ENV, CLS);
    const jint *ev = arr && arr->kind == JK_ARRAY ? (const jint *)arr->a.data : NULL;
    const int n = ev ? (count < arr->a.len ? count : arr->a.len) : 0;
    for (int i = 0; i < n; i++) {
      if (exact) {
        const int known = g_n.keyreleased ? JAVA_CODES_104 : JAVA_CODES_10;
        const int java = codes[i] >= 0 && codes[i] < known ? k_java_code[codes[i]] : -1;
        if (codes[i] == 0)
          fbf_ui_round_over(data[i]); /* "a new score": the round's, once its panel counted up */
        if (java < 0)
          continue; /* the engine gave the Java nothing for it */
        static int differ;
        if (java != ev[i] && !differ++)
          debugPrintf("[game] engine event %d came as Java %d, not %d\n", codes[i], (int)ev[i], java);
      }
      switch (ev[i]) {
      case 4: /* the point sound, at a third of the volume */
        fbf_audio_play(FBF_SFX_POINT, 1.0f / 3.0f);
        break;
      case 5: fbf_audio_play(FBF_SFX_SWOOSHING, 1.0f); break;
      case 6: fbf_audio_play(FBF_SFX_DIE, 1.0f); break;
      case 7: fbf_audio_play(FBF_SFX_HIT, 1.0f); break;
      case 8: fbf_audio_play(FBF_SFX_WING, 1.0f); break;
      case 12: fbf_audio_play(FBF_SFX_BUTTON, 1.0f); break;
      case 9: { /* a new high score: saved when it beats the saved one */
        const int hs = g_n.getHighScore(ENV, CLS);
        if (hs > g_prefs.score) {
          debugPrintf("[game] new high score %d (was %d): saved\n", hs, g_prefs.score);
          g_prefs.score = hs;
          fbf_prefs_save(&g_prefs);
        }
        break;
      }
      case 10: /* the Amazon GameCircle leaderboard: the port's own (fbf_ui.c) */
        fbf_ui_show_board();
        break;
      case 0:                 /* nothing (an engine event with no Java code) */
      case 1: case 2: case 3: /* show / hide the ad banner: none here */
      case 11:                /* "rate": the store page, none here */
        break;
      default: {
        static int logged;
        if (logged++ < 8)
          debugPrintf("[game] output event %d: unknown\n", (int)ev[i]);
        break;
      }
      }
    }
    jni_release(arr); /* the local reference the native returned */
  }
  g_n.resetOutputEvent(ENV, CLS);
  ui_handover();
}

/* setInputDevices(InputDevice.getDeviceIds()): the (at most two) playing
 * controllers' devices. One array object per length, reused. */
static void input_devices(void) {
  static JObj *arrays[FBF_PLAYERS + 1];
  int ids[FBF_PLAYERS];
  const int n = fbf_input_devices(ids, FBF_PLAYERS);
  if (!arrays[n]) {
    arrays[n] = jni_array('I', n);
    arrays[n]->immortal = 1;
  }
  memcpy(arrays[n]->a.data, ids, sizeof(jint) * (size_t)n);
  g_n.setInputDevices(ENV, CLS, arrays[n]);
  static int last = -1;
  const int key = n ? ids[0] * 16 + (n > 1 ? ids[1] : 0) : 0;
  if (key != last) {
    if (!n)
      debugPrintf("[input] devices the game sees: none (no controller)\n");
    else if (n == 1)
      debugPrintf("[input] devices the game sees: %d\n", ids[0]);
    else
      debugPrintf("[input] devices the game sees: %d and %d\n", ids[0], ids[1]);
    last = key;
  }
}

/* ------------------------------------------------------------ the splash */
/* SplashScreen: res/drawable/splash.png (the dotGears logo) in the middle
 * of a black screen for 2 s (its Handler.postDelayed(2000)), then the game.
 * The ImageView shows it at the TV's density: 1.33x on a 720p screen, 2x on
 * 1080p, i.e. height/540. The game's art and sounds are read while it shows. */
static unsigned g_splash_tex;
static int g_splash_w, g_splash_h;
static u64 g_splash_t0;

static void splash_draw(void) {
  const float sc = (float)g_h / 540.0f;
  const float iw = floorf((float)g_splash_w * sc + 0.5f), ih = floorf((float)g_splash_h * sc + 0.5f);
  fbf_overlay_picture(g_splash_tex, g_w, g_h, floorf(((float)g_w - iw) / 2), floorf(((float)g_h - ih) / 2),
                      iw, ih);
  fbf_overlay_opaque(g_w, g_h);
  b_eglSwapBuffers(g_dpy, g_surf);
}

static void splash_begin(const char *apk) {
  const FbfGl *gl = fbf_gl();
  uint8_t *px = gl ? fbf_assets_splash(apk, &g_splash_w, &g_splash_h) : NULL;
  if (!px) {
    debugPrintf("[game] no splash picture in the APK\n");
    return;
  }
  fGLuint tex = 0;
  gl->glGenTextures(1, &tex);
  gl->glBindTexture(F_GL_TEXTURE_2D, tex);
  gl->glPixelStorei(F_GL_UNPACK_ALIGNMENT, 4);
  gl->glTexImage2D(F_GL_TEXTURE_2D, 0, F_GL_RGBA, g_splash_w, g_splash_h, 0, F_GL_RGBA,
                   F_GL_UNSIGNED_BYTE, px);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MIN_FILTER, F_GL_LINEAR);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MAG_FILTER, F_GL_LINEAR);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_WRAP_S, F_GL_CLAMP_TO_EDGE);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_WRAP_T, F_GL_CLAMP_TO_EDGE);
  gl->glBindTexture(F_GL_TEXTURE_2D, 0);
  fbf_assets_free_pixels(px);
  g_splash_tex = tex;
  splash_draw();
  g_splash_t0 = armGetSystemTick();
  debugPrintf("[game] splash %dx%d\n", g_splash_w, g_splash_h);
}

/* The rest of the 2 s; 0 when the system asked the game to close meanwhile. */
static int splash_end(void) {
  if (!g_splash_tex)
    return appletMainLoop();
  int open = 1;
  while (armTicksToNs(armGetSystemTick() - g_splash_t0) < 2000000000ull) {
    if (!(open = appletMainLoop()))
      break;
    splash_draw();
  }
  const FbfGl *gl = fbf_gl();
  const fGLuint tex = g_splash_tex;
  gl->glDeleteTextures(1, &tex);
  g_splash_tex = 0;
  return open;
}

/* --------------------------------------------------------- lifecycle */
static AppletHookCookie g_hook;

static void on_applet(AppletHookType type, void *param) {
  (void)param;
  if (type == AppletHookType_OnExitRequest)
    debugPrintf("[applet] the system asked the game to close\n");
  if (type == AppletHookType_OnFocusState || type == AppletHookType_OnOperationMode) {
    int focused = appletGetFocusState() == AppletFocusState_InFocus;
    if (focused != g_focused) {
      g_focused = focused;
      g_focus_changed = 1;
    }
  }
}

static void apply_focus(void) {
  if (!g_focus_changed)
    return;
  g_focus_changed = 0;
  if (!g_focused) {
    debugPrintf("[game] focus lost: onPause%s\n", g_user_paused ? " (already paused)" : "");
    fbf_input_reset();
    memset(&g_gate, 0, sizeof g_gate);
    if (g_engine_up && !g_user_paused)
      g_n.pause(ENV, CLS);
    fbf_audio_pause(1);
    log_flush_ring();
    dcr_time_suspend();
  } else {
    dcr_time_resume();
    fbf_audio_pause(0);
    if (g_engine_up && !g_user_paused)
      g_n.resume(ENV, CLS, -1, -1);
    debugPrintf("[game] focus regained: onResume\n");
  }
}

/* ------------------------------------------------ the controller screen */
/* The Switch's own screen for connecting controllers (the one games show
 * for a 2-player mode): at least two controllers, taking over the ones
 * already connected, up to the system's maximum -- so nobody is sent away
 * (fbf_input.c still lets only two play). A pair of Joy-Cons may be one
 * controller. The engine and its sound are held while it shows, as when the
 * game loses focus. */
int fbf_controller_screen(void) {
  HidLaControllerSupportArg arg;
  hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = 2;
  arg.hdr.player_count_max = 8; /* 4 before 8.0.0 (libnx) */
  arg.hdr.enable_take_over_connection = 1;
  arg.hdr.enable_permit_joy_dual = 1;
  const int was_paused = g_user_paused;
  if (!was_paused)
    g_n.pause(ENV, CLS);
  fbf_input_swallow_held(); /* what is held now ends on the other screen */
  memset(&g_gate, 0, sizeof g_gate);
  fbf_audio_pause(1);
  HidLaControllerSupportResultInfo info;
  memset(&info, 0, sizeof info);
  Result rc = hidLaShowControllerSupport(&info, &arg);
  fbf_audio_pause(0);
  if (!was_paused)
    g_n.resume(ENV, CLS, -1, -1);
  debugPrintf("[menu] controller screen: rc 0x%x, %d controller(s)\n", (unsigned)rc, (int)info.player_count);
  return R_SUCCEEDED(rc) ? 0 : -1;
}

/* ---------------------------------------------------------------- EGL */
static void egl_up(void) {
  dcr_window_size(&g_w, &g_h);
  g_dpy = b_eglGetDisplay(NULL);
  fEGLint maj = 0, min = 0, n = 0;
  if (!g_dpy || !b_eglInitialize(g_dpy, &maj, &min))
    fatal_error("The graphics driver did not start (eglInitialize 0x%x).", (unsigned)b_eglGetError());
  unsigned (*bind_api)(unsigned) = (unsigned (*)(unsigned))dcr_gl_lookup("eglBindAPI");
  if (bind_api)
    bind_api(EGL_OPENGL_ES_API);
  /* GLSurfaceView's chooser (h): ES 2, RGB; the RGBA8888 Mesa offers, no depth */
  static const fEGLint cfg_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                      EGL_BLUE_SIZE, 8, EGL_NONE};
  void *cfg = NULL;
  if (!b_eglChooseConfig(g_dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
    fatal_error("No OpenGL ES 2 window configuration (0x%x).", (unsigned)b_eglGetError());
  g_surf = b_eglCreateWindowSurface(g_dpy, cfg, nwindowGetDefault(), NULL);
  static const fEGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE}; /* the factory i */
  g_ctx = b_eglCreateContext(g_dpy, cfg, NULL, ctx_attrs);
  if (!g_surf || !g_ctx || !b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx))
    fatal_error("Could not create the OpenGL ES 2 context (surface %p, context %p, 0x%x).", g_surf,
                g_ctx, (unsigned)b_eglGetError());
  b_eglSwapInterval(g_dpy, 1);
  debugPrintf("[game] EGL %d.%d: OpenGL ES 2 on the window, %dx%d\n", (int)maj, (int)min, g_w, g_h);
}

static void egl_down(void) {
  if (!g_dpy)
    return;
  b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  if (g_ctx)
    b_eglDestroyContext(g_dpy, g_ctx);
  if (g_surf)
    b_eglDestroySurface(g_dpy, g_surf);
  b_eglTerminate(g_dpy);
  g_dpy = g_surf = g_ctx = NULL;
}

/* com.dotgears.a: the atlas as a texture, filtered LINEAR both ways. */
static fGLuint upload_atlas(const FbfAssets *a) {
  const FbfGl *gl = fbf_gl();
  if (!gl)
    fatal_error("The GL functions the port needs are missing (see debug.log).");
  fGLuint tex = 0;
  gl->glGenTextures(1, &tex);
  gl->glBindTexture(F_GL_TEXTURE_2D, tex);
  gl->glPixelStorei(F_GL_UNPACK_ALIGNMENT, 4);
  gl->glTexImage2D(F_GL_TEXTURE_2D, 0, F_GL_RGBA, a->w, a->h, 0, F_GL_RGBA, F_GL_UNSIGNED_BYTE, a->rgba);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MIN_FILTER, F_GL_LINEAR);
  gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MAG_FILTER, F_GL_LINEAR);
  fGLenum err = gl->glGetError();
  debugPrintf("[game] atlas: %dx%d texture %u%s\n", a->w, a->h, (unsigned)tex,
              err ? " -- GL ERROR" : "");
  if (err)
    debugPrintf("[game] glTexImage2D left GL error 0x%x\n", (unsigned)err);
  return tex;
}

/* ---------------------------------------------------------------- report */
static void report(void) {
  static u64 last_tick;
  static unsigned long last_frames;
  const u64 tick = armGetSystemTick();
  const unsigned long frames = (unsigned long)dcr_gl_frames();
  const double fps = last_tick ? (double)(frames - last_frames) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  last_tick = tick;
  last_frames = frames;
  char rate[24] = "";
  if (fps > 0.0)
    snprintf(rate, sizeof rate, " (%.1f fps)", fps);
  debugPrintf("[game] %lu frames%s, scene %d%s, %lu audio blocks (%lu underruns), %d Java objects%s\n",
              frames, rate, fbf_scene(), g_user_paused ? " (paused)" : "", (unsigned long)dcr_audio_blocks(),
              dcr_audio_underruns(), jni_live_objects(), fbf_ui_active() ? ", leaderboard up" : "");
  dcr_boost_report();
}

/* ----------------------------------------------------------------- run */
int fbf_game_run(void) {
  char apk[512], err[256];
  snprintf(apk, sizeof apk, "%s", dcr_apk_path()); /* found at boot, whatever its name */

  /* ---- SplashScreen (the window and its GL context are the port's from
   * here on: the GLSurfaceView's, later) ---- */
  egl_up();
  fbf_overlay_init();
  splash_begin(apk);

  /* ---- GameActivity.onCreate ---- */
  fbf_java_init();
  fbf_prefs_load(&g_prefs);
  g_prefs.playcount++;
  fbf_prefs_save(&g_prefs);

  FbfAssets assets;
  if (fbf_assets_load(apk, &assets, err, sizeof err) != 0)
    fatal_error("%s", err);
  int sounds = 0;
  if (fbf_audio_init() == 0) {
    for (int i = 0; i < FBF_SFX_COUNT; i++)
      if (assets.sfx[i].pcm &&
          fbf_audio_set_sound(i, assets.sfx[i].pcm, assets.sfx[i].frames, assets.sfx[i].channels,
                              assets.sfx[i].rate) == 0) {
        assets.sfx[i].pcm = NULL; /* the SoundPool has it now */
        sounds++;
      }
  }
  debugPrintf("[game] res/raw: atlas %dx%d, sprite table %u bytes, %d of %d sounds\n", assets.w,
              assets.h, (unsigned)assets.atlas_len, sounds, FBF_SFX_COUNT);
  fbf_input_init();
  if (!splash_end()) { /* closed from the HOME menu during the splash */
    fbf_assets_free(&assets);
    fbf_audio_shutdown();
    egl_down();
    debugPrintf("[game] closed during the splash\n");
    log_flush_ring();
    return 0;
  }
  appletHook(&g_hook, on_applet, NULL);
  dcr_watchdog_start();

  /* ---- renderer.onSurfaceChanged, the first time ---- */
  debugPrintf("[game] dot_JNILib.init(%dx%d), setHighScore(%d)\n", g_w, g_h, g_prefs.score);
  g_n.init(ENV, CLS, g_activity, g_w, g_h);
  g_n.setHighScore(ENV, CLS, g_prefs.score);
  const fGLuint tex = upload_atlas(&assets);
  /* The engine keeps what GetStringUTFChars gave it: the string stays. */
  JObj *text = jni_str(assets.atlas_text);
  text->immortal = 1;
  g_n.setAtlas(ENV, CLS, (jint)tex, text, (jint)assets.atlas_len);
  /* the leaderboard's sprites, out of the atlas before it goes */
  if (fbf_ui_init(assets.rgba, assets.w, assets.h, assets.atlas_text) == 0) {
    FbfBoard board;
    fbf_board_load(&board);
    fbf_ui_set_board(&board, dcr_config()->board, dcr_config()->board_min);
    debugPrintf("[board] leaderboard %s (min_score %d)\n", dcr_config()->board ? "on" : "off",
                dcr_config()->board_min);
  }
  fbf_assets_free(&assets); /* the bitmap is recycled, as the Java does */
  g_engine_up = 1;
  debugPrintf("[game] setAtlas done: the engine is up (scene %d)\n", fbf_scene());
  fbf_menu_init();
  log_flush_ring();

  /* ---- onDrawFrame, and the UI thread's input, in turn ---- */
  u64 last_report = armGetSystemTick(), last_frame = last_report;
  int first = 1;
  unsigned long quiet_at = 0;
  while (!g_exit && appletMainLoop()) {
    apply_focus();
    if (!g_focused) {
      svcSleepThread(50000000ll);
      continue;
    }
    fbf_input_poll();
    if (g_exit)
      break;
    ui_handover();
    fbf_menu_frame(); /* 2P asked for on the main menu (fbf_menu.c) */
    input_devices();
    g_n.step(ENV, CLS);
    output_events();

    const u64 now = armGetSystemTick();
    if (!g_user_paused)
      fbf_ui_frame((float)armTicksToNs(now - last_frame) / 1e9f);
    int ui_changed;
    float ui_alpha;
    const struct FbfCanvas *ui = fbf_ui_picture(&ui_changed, &ui_alpha);
    if (ui)
      fbf_overlay_ui(ui, ui_changed, ui_alpha, g_w, g_h);
    if (g_user_paused) {
      g_pause_t += (float)armTicksToNs(now - last_frame) / 1e9f;
      fbf_overlay_pause(g_w, g_h, g_pause_t);
    }
    last_frame = now;
    fbf_overlay_opaque(g_w, g_h);
    b_eglSwapBuffers(g_dpy, g_surf);

    const unsigned long frames = (unsigned long)dcr_gl_frames();
    if (first) {
      first = 0;
      dcr_boost_launch_end();
      debugPrintf("[game] first frame presented\n");
      quiet_at = frames + 180;
    }
    /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
     * written out every 10 s and by the watchdog. */
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
    }
  }

  /* ---- onPause, onStop, onDestroy ---- */
  debugPrintf("[game] leaving (%s)\n", g_exit ? "Back on the main menu" : "closed from the system");
  log_set_quiet(0);
  appletUnhook(&g_hook);
  if (g_engine_up && g_focused && !g_user_paused)
    g_n.pause(ENV, CLS);
  if (g_n.stop)
    g_n.stop(ENV, CLS);
  fbf_prefs_save(&g_prefs);
  fbf_audio_shutdown();
  egl_down();
  debugPrintf("[game] closed; high score %d\n", g_prefs.score);
  log_flush_ring();
  return 0;
}
