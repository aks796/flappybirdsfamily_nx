/* fbf_input.c -- Switch controllers and the touch screen as Android input.
 *
 * On Android (a Fire TV / Android TV box) every gamepad is an input device
 * with its own id, and GameActivity hands each key to the engine with that id
 * (keypressed / keyreleased(keycode, deviceId)); every frame the renderer
 * tells the engine which devices exist (setInputDevices(InputDevice.
 * getDeviceIds())). The engine does the rest itself: on the main menu the
 * first device to press a button becomes player 1 and the next other one
 * player 2 (MainScene / SelectorButton), and a player whose device leaves the
 * list is dropped (MainScene::update).
 *
 * TWO CONTROLLERS, NO MORE. The game has two birds. HID is told to accept
 * every controller in every slot -- nobody is disconnected, refused or asked
 * to re-pair -- but only two controllers become Android devices here: the
 * first two connected of
 *     the Joy-Cons on the console (handheld)   device 3
 *     the controller in slot 1                 device 1
 *     the controller in slot 2                 device 2
 * Docked, that is players 1 and 2; in handheld mode, the console and the
 * controller in slot 1 (a friend's Pro Controller, say). A pair of Joy-Cons
 * used together is one controller (one slot). A single Joy-Con held sideways
 * is one controller of its own (below). Everything else -- a third
 * controller, slots 3 to 8 -- stays connected and can use the HOME button,
 * but nothing it presses reaches the game, and the device list never names
 * it. Each controller keeps its device id for as long as it is connected, so
 * the engine's player assignment does not move when another controller comes
 * or goes.
 *
 * A SINGLE JOY-CON, held sideways (SL and SR on top) is turned a quarter
 * turn: the left one anticlockwise, the right one clockwise. HID reports its
 * stick and its four buttons as the Joy-Con sees them, upright, whatever the
 * hold type (hardware run 2026-09-25: with the hold type horizontal, a push
 * towards the top of the screen was a Right). So the turn is undone here, as
 * BombSquad's port does: the stick is rotated back, and the four buttons are
 * named by where they now sit -- the one on the right is A, the bottom one B,
 * the top one X, the left one Y, as on a Pro Controller. (A left Joy-Con's
 * four buttons are the D-pad bits, a right one's the lettered ones; a lone
 * right Joy-Con's stick is reported as the right stick.) config.ini [controls]
 * single_joycon_sideways = false: held upright, nothing turned.
 *
 * Keys (Android keycodes, what a TV gamepad sends):
 *   A B X Y L R ZL ZR, SL SR, stick clicks  BUTTON_* -- in the engine any key
 *                                           that is not the D-pad flaps and
 *                                           confirms
 *   D-pad                                   DPAD_UP/DOWN/LEFT/RIGHT (menus)
 *   the stick                               the same D-pad keys, as Android's
 *                                           ViewRootImpl makes them from an
 *                                           unhandled joystick (threshold 0.5);
 *                                           either stick (a right Joy-Con's is
 *                                           the right one)
 *   -                                       BACK
 *   +                                       the port's pause (fbf_game.c), or
 *                                           BUTTON_START with the pause off
 * A frame's key-ups go to the game before its key-downs: on Android they
 * arrive in time order, and the activity's one-key-per-device rule would
 * otherwise drop a press made as another key was let go.
 * Touch: GameView.onTouchEvent passes ACTION_DOWN and ACTION_UP of the first
 * finger only (touchPressed / touchReleased at the view's pixels). MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "fbf.h"
#include "util.h"

static const struct {
  u64 button;
  int keycode;
} k_keys[] = {
    {HidNpadButton_A, AK_BUTTON_A},
    {HidNpadButton_B, AK_BUTTON_B},
    {HidNpadButton_X, AK_BUTTON_X},
    {HidNpadButton_Y, AK_BUTTON_Y},
    {HidNpadButton_L, AK_BUTTON_L1},
    {HidNpadButton_R, AK_BUTTON_R1},
    {HidNpadButton_ZL, AK_BUTTON_L2},
    {HidNpadButton_ZR, AK_BUTTON_R2},
    {HidNpadButton_LeftSL, AK_BUTTON_L1},  /* a sideways Joy-Con's shoulders */
    {HidNpadButton_LeftSR, AK_BUTTON_R1},
    {HidNpadButton_RightSL, AK_BUTTON_L1},
    {HidNpadButton_RightSR, AK_BUTTON_R1},
    {HidNpadButton_StickL, AK_BUTTON_THUMBL},
    {HidNpadButton_StickR, AK_BUTTON_THUMBR},
    {HidNpadButton_Up, AK_DPAD_UP},
    {HidNpadButton_Down, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT},
    {HidNpadButton_Right, AK_DPAD_RIGHT},
    {HidNpadButton_Minus, AK_BACK},
    {HidNpadButton_Plus, AK_BUTTON_START}, /* only when + is not the pause */
};
#define NKEYS ((int)(sizeof k_keys / sizeof k_keys[0]))

/* The controllers that can play, in the order they are chosen. */
typedef struct {
  PadState pad;
  int dev;            /* its Android device id */
  const char *name;
  int connected, playing;
  u64 sent_down;      /* buttons whose key-down went out */
  int sent_code[NKEYS]; /* ... with this keycode (its key-up says the same) */
  int stick_held;     /* the D-pad key the stick holds down, 0 none */
  int stick_latched;  /* the stick must come back to the middle first */
  u32 style_seen;
  u64 raw_logged;     /* [debug] log_input: the buttons last written */
  u64 stick_log_tick;
} Source;

enum { SRC_HANDHELD, SRC_NO1, SRC_NO2, NSRC };
static Source g_src[NSRC] = {
    [SRC_HANDHELD] = {.dev = FBF_DEV_HANDHELD, .name = "the Joy-Cons on the console", .style_seen = 0xffffffffu},
    [SRC_NO1] = {.dev = FBF_DEV_P1, .name = "the controller in slot 1", .style_seen = 0xffffffffu},
    [SRC_NO2] = {.dev = FBF_DEV_P2, .name = "the controller in slot 2", .style_seen = 0xffffffffu},
};

/* --------------------------------------------------- a single Joy-Con */
enum { JOY_NONE, JOY_LEFT, JOY_RIGHT };
static int sideways_joycon(const Source *s) {
  if (!dcr_config()->joycon_sideways)
    return JOY_NONE;
  const u32 style = padGetStyleSet(&s->pad);
  return (style & HidNpadStyleTag_NpadJoyLeft) ? JOY_LEFT : (style & HidNpadStyleTag_NpadJoyRight) ? JOY_RIGHT : JOY_NONE;
}

/* Its four buttons by where they sit once turned, as the letters a Pro
 * Controller has there. */
static const struct {
  u64 button;
  int keycode;
} k_joy_left[] = {
    {HidNpadButton_Up, AK_BUTTON_Y},    /* now on the left */
    {HidNpadButton_Right, AK_BUTTON_X}, /* on top */
    {HidNpadButton_Down, AK_BUTTON_A},  /* on the right */
    {HidNpadButton_Left, AK_BUTTON_B},  /* at the bottom */
}, k_joy_right[] = {
    {HidNpadButton_X, AK_BUTTON_A}, /* now on the right */
    {HidNpadButton_A, AK_BUTTON_B}, /* at the bottom */
    {HidNpadButton_B, AK_BUTTON_Y}, /* on the left */
    {HidNpadButton_Y, AK_BUTTON_X}, /* on top */
};

static int keycode_of(int joy, int i) {
  const u64 b = k_keys[i].button;
  for (int j = 0; j < 4; j++) {
    if (joy == JOY_LEFT && k_joy_left[j].button == b)
      return k_joy_left[j].keycode;
    if (joy == JOY_RIGHT && k_joy_right[j].button == b)
      return k_joy_right[j].keycode;
  }
  return k_keys[i].keycode;
}

/* ---------------------------------------------------------------- setup */
static void log_controller(Source *s) {
  const u32 style = s->connected ? padGetStyleSet(&s->pad) : 0;
  if (style == s->style_seen)
    return;
  s->style_seen = style;
  if (!style) {
    debugPrintf("[input] %s: gone\n", s->name);
    return;
  }
  char kinds[96] = "";
  static const struct {
    u32 tag;
    const char *name;
  } k[] = {
      {HidNpadStyleTag_NpadFullKey, "Pro Controller"},  {HidNpadStyleTag_NpadHandheld, "handheld Joy-Cons"},
      {HidNpadStyleTag_NpadJoyDual, "two Joy-Cons"},    {HidNpadStyleTag_NpadJoyLeft, "left Joy-Con"},
      {HidNpadStyleTag_NpadJoyRight, "right Joy-Con"},  {HidNpadStyleTag_NpadGc, "GameCube controller"},
  };
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (style & k[i].tag)
      snprintf(kinds + strlen(kinds), sizeof kinds - strlen(kinds), "%s%s", kinds[0] ? " + " : "",
               k[i].name);
  debugPrintf("[input] %s: %s (style 0x%x)\n", s->name, kinds[0] ? kinds : "a controller",
              (unsigned)style);
}

void fbf_input_init(void) {
  /* Every slot and style accepted: extra controllers stay connected. */
  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  Result rc = hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
  if (R_FAILED(rc))
    debugPrintf("[input] hidSetNpadJoyHoldType: 0x%x\n", rc);
  padInitialize(&g_src[SRC_HANDHELD].pad, HidNpadIdType_Handheld);
  padInitialize(&g_src[SRC_NO1].pad, HidNpadIdType_No1);
  padInitialize(&g_src[SRC_NO2].pad, HidNpadIdType_No2);
  hidInitializeTouchScreen();
  debugPrintf("[input] two controllers play: the first two of handheld, slot 1, slot 2 "
              "(devices 3, 1, 2); the others stay connected but do not reach the game\n");
}

int fbf_input_devices(int *ids, int cap) {
  int n = 0;
  for (int i = 0; i < NSRC && n < cap; i++)
    if (g_src[i].playing)
      ids[n++] = g_src[i].dev;
  return n;
}

/* ------------------------------------------------------------ the stick */
/* ViewRootImpl.SyntheticJoystickHandler: an axis past 0.5 is a D-pad key
 * down, back under it the key up; one direction at a time here (the
 * activity's rule lets only one key of a device through anyway). A little
 * hysteresis keeps a stick resting near the threshold from chattering. */
static int stick_key(HidAnalogStickState s, int held) {
  const float x = (float)s.x / 32767.0f, y = (float)s.y / 32767.0f;
  const float on = 0.5f, off = 0.4f;
  const float ax = fabsf(x), ay = fabsf(y);
  if (held) {
    const float along = (held == AK_DPAD_LEFT || held == AK_DPAD_RIGHT) ? x : y;
    const int sign = (held == AK_DPAD_RIGHT || held == AK_DPAD_UP) ? 1 : -1;
    if (along * (float)sign > off)
      return held;
  }
  if (ax < on && ay < on)
    return 0;
  if (ax >= ay)
    return x > 0 ? AK_DPAD_RIGHT : AK_DPAD_LEFT;
  return y > 0 ? AK_DPAD_UP : AK_DPAD_DOWN; /* HID: +y is up */
}

/* The stick that is pushed further: a controller's left one, or a single
 * right Joy-Con's (its only stick is reported as the right). A sideways
 * Joy-Con's is turned back: the left one was turned anticlockwise (its
 * right points up), the right one clockwise (its left points up). */
static HidAnalogStickState stick_of(Source *s) {
  const int joy = sideways_joycon(s);
  if (joy == JOY_LEFT) {
    const HidAnalogStickState r = padGetStickPos(&s->pad, 0);
    return (HidAnalogStickState){-r.y, r.x};
  }
  if (joy == JOY_RIGHT) {
    const HidAnalogStickState r = padGetStickPos(&s->pad, 1);
    return (HidAnalogStickState){r.y, -r.x};
  }
  const HidAnalogStickState l = padGetStickPos(&s->pad, 0), r = padGetStickPos(&s->pad, 1);
  const long ml = (long)l.x * l.x + (long)l.y * l.y, mr = (long)r.x * r.x + (long)r.y * r.y;
  return mr > ml ? r : l;
}

/* [debug] log_input: what HID reports, before any of the above -- the
 * buttons when they change, both sticks four times a second while one is
 * pushed (for a report of a controller that comes out wrong). */
static void log_raw(Source *s) {
  const u64 b = padGetButtons(&s->pad) & 0xffffffffull;
  const HidAnalogStickState r0 = padGetStickPos(&s->pad, 0), r1 = padGetStickPos(&s->pad, 1);
  if (b != s->raw_logged) {
    s->raw_logged = b;
    debugPrintf("[input] %s: buttons 0x%08llx\n", s->name, (unsigned long long)b);
  }
  const int pushed = abs(r0.x) > 12000 || abs(r0.y) > 12000 || abs(r1.x) > 12000 || abs(r1.y) > 12000;
  const u64 now = armGetSystemTick();
  if (pushed && now - s->stick_log_tick > armGetSystemTickFreq() / 4) {
    s->stick_log_tick = now;
    const HidAnalogStickState t = stick_of(s);
    debugPrintf("[input] %s: sticks [0] %6d,%6d [1] %6d,%6d -> %6d,%6d%s\n", s->name, (int)r0.x, (int)r0.y,
                (int)r1.x, (int)r1.y, (int)t.x, (int)t.y, sideways_joycon(s) ? " (Joy-Con turned back)" : "");
  }
}

/* ---------------------------------------------------------------- poll */
/* Every key of s that went down, let go: with key-ups sent (send), or
 * silently (the pause swallows them: a release after it must not reach a
 * menu, which acts on releases). A stick that was held must come back to
 * the middle before it is a key again. */
static void release_all(Source *s, int send) {
  for (int i = 0; i < NKEYS; i++)
    if (s->sent_down & k_keys[i].button) {
      s->sent_down &= ~k_keys[i].button;
      if (send)
        fbf_key(0, s->sent_code[i], s->dev);
    }
  if (s->stick_held) {
    if (send)
      fbf_key(0, s->stick_held, s->dev);
    s->stick_held = 0;
    s->stick_latched = 1;
  }
}

static void pad_keys(Source *s) {
  const u64 down = padGetButtonsDown(&s->pad), up = padGetButtonsUp(&s->pad);
  const int plus_pauses = dcr_config()->plus_pause;
  if (dcr_config()->log_input)
    log_raw(s);

  if ((down & HidNpadButton_Plus) && plus_pauses) {
    fbf_pause_toggle();
    return; /* the rest of this frame's presses belong to the pause */
  }
  if (fbf_user_paused())
    return; /* nothing reaches the game while it is paused */

  int want = s->stick_held;
  if (dcr_config()->stick_dpad) {
    const HidAnalogStickState st = stick_of(s);
    want = stick_key(st, s->stick_held);
    if (s->stick_latched) {
      if (fabsf((float)st.x) < 0.4f * 32767.0f && fabsf((float)st.y) < 0.4f * 32767.0f)
        s->stick_latched = 0;
      want = 0;
    }
  }

  /* the frame's key-ups first ... */
  for (int i = 0; i < NKEYS; i++) {
    const u64 b = k_keys[i].button;
    if ((up & b) && (s->sent_down & b)) {
      s->sent_down &= ~b;
      fbf_key(0, s->sent_code[i], s->dev);
    }
  }
  if (s->stick_held && want != s->stick_held) {
    fbf_key(0, s->stick_held, s->dev);
    s->stick_held = 0;
  }
  /* ... then its key-downs */
  const int joy = sideways_joycon(s);
  for (int i = 0; i < NKEYS; i++) {
    const u64 b = k_keys[i].button;
    if (b == HidNpadButton_Plus && plus_pauses)
      continue;
    if ((down & b) && !(s->sent_down & b)) {
      s->sent_down |= b;
      s->sent_code[i] = keycode_of(joy, i);
      fbf_key(1, s->sent_code[i], s->dev);
    }
  }
  if (want && want != s->stick_held) {
    s->stick_held = want;
    fbf_key(1, want, s->dev);
  }
}

/* GameView.onTouchEvent: ACTION_DOWN when the first finger lands,
 * ACTION_UP when the last one lifts (at its position); moves and further
 * fingers are not passed on. Panel pixels (1280x720) -> the view's. */
static void touch_poll(void) {
  static int touching;
  static int last_x, last_y;
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1))
    return;
  int vw, vh;
  dcr_window_size(&vw, &vh);
  if (ts.count > 0) {
    last_x = (int)((float)ts.touches[0].x * (float)vw / 1280.0f);
    last_y = (int)((float)ts.touches[0].y * (float)vh / 720.0f);
    if (!touching) {
      touching = 1;
      if (dcr_config()->touch)
        fbf_touch(1, last_x, last_y);
    }
  } else if (touching) {
    touching = 0;
    if (dcr_config()->touch)
      fbf_touch(0, last_x, last_y);
  }
}

void fbf_input_poll(void) {
  touch_poll();
  for (int i = 0; i < NSRC; i++) {
    Source *s = &g_src[i];
    padUpdate(&s->pad);
    s->connected = padIsConnected(&s->pad);
    log_controller(s);
  }
  /* the first two connected play */
  int n = 0;
  for (int i = 0; i < NSRC; i++) {
    Source *s = &g_src[i];
    const int playing = s->connected && n < FBF_PLAYERS;
    n += playing;
    if (playing != s->playing) {
      if (!playing)
        release_all(s, 1); /* a key held as it stopped playing must not stay down */
      s->playing = playing;
      debugPrintf("[input] %s %s (device %d)\n", s->name,
                  playing ? "plays" : s->connected ? "does not play: two others do" : "left",
                  s->dev);
    }
  }
  for (int i = 0; i < NSRC; i++)
    if (g_src[i].playing)
      pad_keys(&g_src[i]);
}

/* Focus lost: whatever is held now is let go (Android sends the key-ups
 * when a window loses focus, as cancelled keys). */
void fbf_input_reset(void) {
  for (int i = 0; i < NSRC; i++)
    release_all(&g_src[i], 1);
}

/* The pause begins: held keys are forgotten without key-ups. */
void fbf_input_swallow_held(void) {
  for (int i = 0; i < NSRC; i++)
    release_all(&g_src[i], 0);
}
