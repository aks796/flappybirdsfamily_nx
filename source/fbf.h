/* fbf.h -- the Flappy Birds Family side of the port: the pieces that stand in
 * for the game's Java (GameActivity, its GLSurfaceView and renderer, the
 * SoundPool, SharedPreferences), talking to each other. MIT. */
#ifndef FBF_H
#define FBF_H
#include <stddef.h>
#include <stdint.h>

#include "jni.h"

/* ------------------------------------------------------------ the engine */
/* The natives of com.dotgears.dot_JNILib (libflapfire.so exports them as
 * Java_com_dotgears_dot_1JNILib_*). NULL when the engine lacks one (1.0 has
 * no keyreleased). fbf_loader.c fills this in. */
typedef struct {
  void (*init)(void *env, void *cls, void *activity, jint w, jint h);
  void (*setHighScore)(void *env, void *cls, jint score);
  jint (*getHighScore)(void *env, void *cls);
  void (*setAtlas)(void *env, void *cls, jint tex, void *text, jint len);
  void (*step)(void *env, void *cls);
  void (*setInputDevices)(void *env, void *cls, void *ids);
  jint (*getOutputEventCount)(void *env, void *cls);
  void *(*getOutputEvents)(void *env, void *cls);
  void (*resetOutputEvent)(void *env, void *cls);
  void (*keypressed)(void *env, void *cls, jint key, jint device);
  void (*keyreleased)(void *env, void *cls, jint key, jint device);
  void (*touchPressed)(void *env, void *cls, jint x, jint y);
  void (*touchReleased)(void *env, void *cls, jint x, jint y);
  void (*pause)(void *env, void *cls);
  void (*resume)(void *env, void *cls, jint a, jint b);
  void (*stop)(void *env, void *cls);
  void (*exit)(void *env, void *cls);
  jint (*getSceneId)(void *env, void *cls);
} FbfNatives;

extern FbfNatives g_n;

int fbf_load_engine(void);      /* load, relocate, resolve, map; 0 on success */
void fbf_run_constructors(void);
const char *fbf_engine_build(void); /* "1.0.4", "1.0" or "unknown" (by its natives) */

/* ---------------------------------------------------------- the Java side */
extern JObj *g_activity;        /* com.dotgears.game.GameActivity */
extern void *g_jnilib;          /* the class object of com.dotgears.dot_JNILib */
void fbf_java_init(void);

/* Android key codes the game sees (android.view.KeyEvent). */
#define AK_BACK 4
#define AK_DPAD_UP 19
#define AK_DPAD_DOWN 20
#define AK_DPAD_LEFT 21
#define AK_DPAD_RIGHT 22
#define AK_BUTTON_A 96
#define AK_BUTTON_B 97
#define AK_BUTTON_X 99
#define AK_BUTTON_Y 100
#define AK_BUTTON_L1 102
#define AK_BUTTON_R1 103
#define AK_BUTTON_L2 104
#define AK_BUTTON_R2 105
#define AK_BUTTON_THUMBL 106
#define AK_BUTTON_THUMBR 107
#define AK_BUTTON_START 108

/* The controllers that can play, as Android input device ids (fbf_input.c):
 * two of them at most, the first two connected of handheld, slot 1, slot 2. */
#define FBF_DEV_P1 1       /* the controller in slot 1 */
#define FBF_DEV_P2 2       /* the controller in slot 2 */
#define FBF_DEV_HANDHELD 3 /* the Joy-Cons on the console */
#define FBF_PLAYERS 2

/* ---------------------------------------------------------- the activity */
/* fbf_game.c: GameActivity.onKeyDown / onKeyUp, GameView.onTouchEvent. */
void fbf_key(int down, int keycode, int device);
void fbf_touch(int down, int x, int y);
void fbf_pause_toggle(void);    /* + : the engine's pause, with the overlay */
int fbf_user_paused(void);
int fbf_scene(void);            /* dot_JNILib.getSceneId: 1 = the main menu */
int fbf_game_run(void);

/* fbf_window.c: the rendering size (config.ini [display] resolution). */
void dcr_window_size(int *w, int *h);
void dcr_window_set_size(int w, int h);
void dcr_window_prepare(void);

/* fbf_game.c: a key straight to the engine (press, and release when the
 * engine has keyreleased), bypassing the activity's key rule: the menu's
 * join of a waiting controller. */
void fbf_engine_key(int keycode, int device);
/* fbf_game.c: the Switch's controller screen (the engine and its sound held
 * meanwhile); 0 when it was shown and closed normally. */
int fbf_controller_screen(void);

/* ---------------------------------------------------------- main menu */
/* fbf_menu.c: Up / Down change a player's bird wherever its cursor is, and
 * A on the mode button with one player asks for 2P. */
typedef struct {
  int sel_bird, sel_dev; /* SelectorButton: bird id, device id */
  int mode_enabled;      /* dot_ToggleButton (the mode button): enabled flag (byte) */
} FbfMenuLayout;
typedef struct {
  void *(*game_instance)(void);    /* Game::Instance() */
  void (*scroll_up)(void *sel);    /* SelectorButton::scrollUp() */
  void (*scroll_down)(void *sel);  /* SelectorButton::scrollDown() */
  uintptr_t vt_main, vt_sel, vt_toggle; /* MainScene, SelectorButton, dot_ToggleButton vtables (+8),
                                           as the engine's 32-bit addresses */
  const FbfMenuLayout *layout;
  void (*key)(int keycode, int device);  /* fbf_engine_key */
  int (*controller_screen)(void);        /* fbf_controller_screen */
  uintptr_t mem_base; /* engine address 0 in ours: 0 on the Switch (tools/test_menu.py: its arena) */
  uintptr_t flap_instance; /* FlapListener::sharedInstance, the event queue's pointer (its address) */
  uintptr_t vt_flap;       /* the queue's vtable (+8) */
} FbfMenuEngine;
void fbf_menu_init(void);                          /* from the loaded engine (Switch) */
/* The engine's event queue as it stands (before getOutputEvents turns it
 * into Java codes): its codes and their data; the count, or -1 when the
 * queue is not as this port knows. */
#define FBF_EVENTS_MAX 20
int fbf_engine_events(int *codes, int *data, int cap);
void fbf_menu_install(const FbfMenuEngine *e);     /* the same, given (tools/test_menu.py) */
const FbfMenuLayout *fbf_menu_layout(int build_has_keyreleased);
/* A key at the edge the engine acts on; 1: handled here, the engine must not
 * see it. */
int fbf_menu_key(int keycode, int device);
void fbf_menu_frame(void); /* after the frame's input, before step() */

/* ------------------------------------------------------------- input */
void fbf_input_init(void);
void fbf_input_poll(void);                   /* once per frame, before step() */
int fbf_input_devices(int *ids, int cap);    /* connected player devices */
void fbf_input_reset(void);                  /* held keys let go, with key-ups (focus lost) */
void fbf_input_swallow_held(void);           /* held keys forgotten, no key-ups (the pause) */

/* The per-device key rule of GameActivity (pure logic, host-tested):
 * a key-down while the same device already holds a key is dropped; any
 * key-up of the device releases it. Returns 1 if the event goes on. */
typedef struct {
  int held[8]; /* by device id; 0 = nothing held */
} FbfKeyGate;
int fbf_gate_down(FbfKeyGate *g, int device);
void fbf_gate_up(FbfKeyGate *g, int device);

/* ------------------------------------------------------------- sound */
/* The SoundPool's sounds (GameActivity's k.a .. k.f), in res/raw. */
enum { FBF_SFX_DIE, FBF_SFX_HIT, FBF_SFX_POINT, FBF_SFX_SWOOSHING, FBF_SFX_WING, FBF_SFX_BUTTON,
       FBF_SFX_COUNT };
extern const char *const fbf_sfx_names[FBF_SFX_COUNT]; /* "sfx_die" ... */

int fbf_audio_init(void);
int fbf_audio_set_sound(int id, int16_t *pcm, int frames, int channels, int rate); /* takes pcm */
void fbf_audio_play(int id, float volume);
void fbf_audio_pause(int paused); /* HOME: voices hold, nothing is mixed */
void fbf_audio_shutdown(void);
uint32_t dcr_audio_blocks(void);  /* blocks mixed so far (the watchdog) */
unsigned long dcr_audio_underruns(void); /* times audout ran dry (after HOME: once, expected) */

/* ------------------------------------------------------------ assets */
typedef struct {
  uint8_t *rgba;       /* atlas.png as Java uploads it: RGBA8, not premultiplied */
  int w, h;
  char *atlas_text;    /* atlas_text.txt, NUL-terminated */
  size_t atlas_len;
  struct {
    int16_t *pcm;      /* interleaved */
    int frames, channels, rate;
  } sfx[FBF_SFX_COUNT];
} FbfAssets;

/* Everything the Java loads from res/raw, out of the APK. 0 on success; on
 * failure a reason in err. */
int fbf_assets_load(const char *apk, FbfAssets *a, char *err, size_t errcap);
void fbf_assets_free(FbfAssets *a);
/* res/drawable/splash.png (SplashScreen's ImageView) as RGBA, or NULL. */
uint8_t *fbf_assets_splash(const char *apk, int *w, int *h);
void fbf_assets_free_pixels(uint8_t *rgba);
/* BitmapFactory decodes to premultiplied pixels and Bitmap.getPixels hands
 * them back unpremultiplied: fully transparent texels come out black, and
 * partly transparent ones lose a little precision. Done in place. */
void fbf_android_bitmap_roundtrip(uint8_t *rgba, size_t pixels);

/* ------------------------------------------------------ preferences */
/* SharedPreferences "flapfire" (GameActivity): score, playcount, rated. */
typedef struct {
  int score, playcount, rated;
} FbfPrefs;
void fbf_prefs_path(char *out, size_t cap);
int fbf_prefs_parse(const char *xml, FbfPrefs *p); /* number of keys found */
int fbf_prefs_format(const FbfPrefs *p, char *out, size_t cap);
void fbf_prefs_load(FbfPrefs *p);
int fbf_prefs_save(const FbfPrefs *p);

/* ------------------------------------------------------------ overlay */
/* Drawn between the engine's frame and the present (fbf_overlay.c). */
int fbf_overlay_init(void);
void fbf_overlay_pause(int w, int h, float t); /* dimmed screen + a pause sign */
void fbf_overlay_opaque(int w, int h);         /* alpha to 1 (the window is RGBA) */
/* A texture over the whole window's black, at x, y, iw, ih (pixels, from the
 * top left), filtered: the splash screen. */
void fbf_overlay_picture(unsigned tex, int w, int h, float x, float y, float iw, float ih);

/* ------------------------------------------------------- leaderboard */
/* fbf_board.c: the five best 1P rounds and their players' names. */
#define FBF_BOARD_SIZE 5
typedef struct {
  char name[4]; /* A-Z and spaces, 1 to 3 */
  int score;
} FbfBoardEntry;
typedef struct {
  FbfBoardEntry e[FBF_BOARD_SIZE];
  int n;
  char last[4]; /* the last name entered */
} FbfBoard;
int fbf_board_place(const FbfBoard *b, int score, int min_score); /* 0-4, or -1: not on it */
int fbf_board_insert(FbfBoard *b, const char *name, int score);   /* its place, or -1 */
int fbf_board_parse(FbfBoard *b, const char *text);               /* places read */
int fbf_board_format(const FbfBoard *b, char *out, size_t cap);   /* length, or -1 */
void fbf_board_load(FbfBoard *b);       /* <game folder>/leaderboard.txt (Switch) */
int fbf_board_save(const FbfBoard *b);

/* fbf_ui.c: the name entry and the leaderboard, drawn in the game's art.
 * The engine's frame is 768x432; so is the UI's picture (FBF_UI_W/H). */
#define FBF_UI_W 768
#define FBF_UI_H 432
struct FbfCanvas;
/* The sprites it uses, copied out of the atlas (before the atlas is freed).
 * 0 when all were found. */
int fbf_ui_init(const uint8_t *atlas_rgba, int aw, int ah, const char *atlas_text);
void fbf_ui_set_board(const FbfBoard *b, int enabled, int min_score);
void fbf_ui_round_over(int score); /* a 1P round's score (the engine's event 0) */
void fbf_ui_show_board(void);      /* the game's leaderboard button */
int fbf_ui_active(void);           /* while it is up, input is its own */
void fbf_ui_key(int down, int keycode, int device);
void fbf_ui_touch(int down, int x, int y); /* in the 768x432 picture's pixels */
void fbf_ui_frame(float dt);
/* 1 (once) when the UI opened or closed: keys held now must give the game
 * no key-up (fbf_game.c forgets them). */
int fbf_ui_take_swallow(void);
/* Its picture, or NULL when nothing shows; *changed: since the last call.
 * *alpha: the fade in. */
const struct FbfCanvas *fbf_ui_picture(int *changed, float *alpha);
/* Draws it over the engine's frame (fbf_overlay.c). */
void fbf_overlay_ui(const struct FbfCanvas *c, int changed, float alpha, int w, int h);

#endif
