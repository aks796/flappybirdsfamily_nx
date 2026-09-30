/* fbf_menu.c -- the main menu, played with controllers.
 *
 * THE ENGINE'S OWN MENU (MainScene, from the disassembly; hardware run
 * 2026-09-25 confirmed it key for key). One row: the mode button (1P / PvP),
 * player 1's bird, player 2's bird, Play. Each player that has joined has a
 * cursor of its own (MainScene +0x88 / +0x8c) that Left / Right move over
 * three places -- the mode button, its own bird, Play -- and the key acts on
 * where the cursor is:
 *     on its bird   Up: next bird, Down: previous one (SelectorButton::
 *                   scrollUp / scrollDown), A (any other key): next bird
 *     on the mode   Up / Down / A: toggle 1P <-> PvP -- when it is enabled
 *     on Play       A: start
 * The mode is not a free choice: MainScene::update enables the button, and
 * sets it to PvP, exactly when BOTH players have joined; toggling it off
 * sends player 2 away. A player joins by pressing a key on a controller of
 * its own (MainScene::keyReleased: the first free SelectorButton takes the
 * device). So with one controller, Up / Down pressed on the mode button did
 * nothing at all, and "2P" could not be chosen.
 *
 * WHAT THIS PORT CHANGES, on the main menu only:
 *   - Up / Down change the player's own bird wherever its cursor is (the
 *     engine's own scrollUp / scrollDown, and the button sound it plays for
 *     them). On the mode button they no longer toggle it: a flick of the
 *     stick would send player 2 away. A still toggles it.
 *   - A (or any key but the D-pad) on the mode button while only one player
 *     has joined asks for 2P: another controller that plays (fbf_input.c) and
 *     has not joined joins as player 2, by the engine's own join (a Right and
 *     a Left from that device: joined, cursor back on its bird), and the
 *     engine turns the mode to PvP itself. With no other controller, the
 *     Switch's controller screen is shown (config.ini [controls]
 *     controller_screen_for_2p), and the controllers connected there join.
 * Everything else is the engine's.
 *
 * The objects are read where the engine keeps them, and only after checking
 * they are what this port knows: the Game's current scene is its main scene
 * (+0x104 == +0xf8, scene id +0xf4 == 1), a MainScene (its vtable), its two
 * SelectorButtons and its mode button, a dot_ToggleButton (theirs). The two
 * engine builds differ only inside the SelectorButtons (1.0.4: bird +0x64,
 * device +0x68; 1.0: 4 bytes less); the mode button's enabled byte is +0x50
 * in both. Anything else, and the engine's menu is left as it is.
 * tools/check_engine.py checks every one of these against the engine in the
 * user's APK. (Hardware run 2026-09-25: this port looked for an ActiveButton,
 * found none, and left the menu to the engine -- whose Up / Down on the mode
 * button kept sending player 2 away.) Keys are acted on at the edge the engine
 * acts on (1.0.4: the release; 1.0, which has no keyreleased: the press). MIT.
 */
#include <stdint.h>
#include <string.h>

#include "dcr_config.h"
#include "fbf.h"
#include "util.h"

#define G_SCENE_ID 0xf4
#define G_MAIN_SCENE 0xf8
#define G_CUR_SCENE 0x104
#define MS_SEL1 0x74
#define MS_SEL2 0x78
#define MS_MODE 0x80
#define MS_FOCUS1 0x88
#define MS_FOCUS2 0x8c
#define FL_COUNT 0x4
#define FL_CODES 0x8
#define FL_DATA 0x58

static const FbfMenuLayout k_layout_104 = {.sel_bird = 0x64, .sel_dev = 0x68, .mode_enabled = 0x50};
static const FbfMenuLayout k_layout_10 = {.sel_bird = 0x60, .sel_dev = 0x64, .mode_enabled = 0x50};

static FbfMenuEngine E;
static int g_ok, g_warned;
static int g_want_partner; /* A on the disabled mode button: 2P asked for */
static int g_join_frames;  /* after the controller screen: join what comes */

void fbf_menu_install(const FbfMenuEngine *e) {
  E = *e;
  g_ok = E.game_instance && E.scroll_up && E.scroll_down && E.vt_main && E.vt_sel && E.vt_toggle &&
         E.layout && E.key;
  g_want_partner = g_join_frames = 0;
}

const FbfMenuLayout *fbf_menu_layout(int build_has_keyreleased) {
  return build_has_keyreleased ? &k_layout_104 : &k_layout_10;
}

/* The engine is 32-bit: a pointer it keeps is 4 bytes, read as such and
 * turned into ours (the same on the Switch; a test arena on a PC). */
typedef uint32_t eaddr;
static uint8_t *at(eaddr a) { return a ? (uint8_t *)(E.mem_base + a) : NULL; }
static eaddr addr_of(const void *p) { return p ? (eaddr)((uintptr_t)p - E.mem_base) : 0; }
static uint32_t rd32(const uint8_t *obj, int off) {
  uint32_t v;
  memcpy(&v, obj + off, sizeof v);
  return v;
}
static int32_t field(const uint8_t *obj, int off) { return (int32_t)rd32(obj, off); }
static int is_obj(eaddr a, uintptr_t vt) { return a && rd32(at(a), 0) == (uint32_t)vt; }

/* The main menu, if it is the scene on screen and laid out as this port
 * knows; NULL otherwise. */
static uint8_t *main_menu(void) {
  if (!g_ok)
    return NULL;
  uint8_t *game = E.game_instance();
  if (!game || field(game, G_SCENE_ID) != 1)
    return NULL;
  const eaddr ms = rd32(game, G_MAIN_SCENE);
  if (!ms || rd32(game, G_CUR_SCENE) != ms)
    return NULL;
  uint8_t *m = at(ms);
  if (!is_obj(ms, E.vt_main) || !is_obj(rd32(m, MS_SEL1), E.vt_sel) || !is_obj(rd32(m, MS_SEL2), E.vt_sel) ||
      !is_obj(rd32(m, MS_MODE), E.vt_toggle)) {
    if (!g_warned++)
      debugPrintf("[menu] the main menu is not laid out as this port knows: the engine's own "
                  "controls only\n");
    return NULL;
  }
  return m;
}

static uint8_t *sel_of(uint8_t *ms, int p) { return at(rd32(ms, p ? MS_SEL2 : MS_SEL1)); }
static int dev_of(uint8_t *ms, int p) { return field(sel_of(ms, p), E.layout->sel_dev); }

static int is_dpad(int k) { return k >= AK_DPAD_UP && k <= AK_DPAD_RIGHT; }

int fbf_menu_key(int keycode, int device) {
  uint8_t *ms = main_menu();
  if (!ms)
    return 0;
  const int p = device == dev_of(ms, 0) ? 0 : device == dev_of(ms, 1) ? 1 : -1;
  if (p < 0)
    return 0; /* not joined yet: the engine lets it join */
  uint8_t *mine = sel_of(ms, p);
  const eaddr cursor = rd32(ms, p ? MS_FOCUS2 : MS_FOCUS1);
  const eaddr mode = rd32(ms, MS_MODE);

  if (keycode == AK_DPAD_UP || keycode == AK_DPAD_DOWN) {
    if (cursor == addr_of(mine))
      return 0; /* the engine changes the bird itself */
    const int before = field(mine, E.layout->sel_bird);
    (keycode == AK_DPAD_UP ? E.scroll_up : E.scroll_down)(mine);
    fbf_audio_play(FBF_SFX_BUTTON, 1.0f); /* the engine's own change plays it (event 14) */
    debugPrintf("[menu] player %d (device %d): bird %d -> %d (%s, cursor elsewhere)\n", p + 1, device,
                before, field(mine, E.layout->sel_bird), keycode == AK_DPAD_UP ? "Up" : "Down");
    return 1;
  }
  if (!is_dpad(keycode) && keycode != AK_BACK && cursor == mode && !at(mode)[E.layout->mode_enabled]) {
    g_want_partner = device; /* after this frame's input (fbf_menu_frame) */
    return 0;                /* a disabled button: the engine does nothing with it */
  }
  return 0;
}

/* Playing controllers that have not joined join, while a place is free. */
static int join_waiting(uint8_t *ms) {
  int ids[FBF_PLAYERS], joined = 0;
  const int n = fbf_input_devices(ids, FBF_PLAYERS);
  for (int i = 0; i < n; i++) {
    const int d1 = dev_of(ms, 0), d2 = dev_of(ms, 1);
    if (ids[i] == d1 || ids[i] == d2 || (d1 >= 0 && d2 >= 0))
      continue;
    /* the engine's join: Right and Left from that device (it takes the free
     * place, and its cursor ends on its bird) */
    E.key(AK_DPAD_RIGHT, ids[i]);
    E.key(AK_DPAD_LEFT, ids[i]);
    debugPrintf("[menu] device %d joins (player %d)\n", ids[i], dev_of(ms, 0) == ids[i] ? 1 : 2);
    joined++;
  }
  if (joined)
    fbf_audio_play(FBF_SFX_BUTTON, 1.0f);
  return joined;
}

void fbf_menu_frame(void) {
  if (!g_want_partner && !g_join_frames)
    return;
  uint8_t *ms = main_menu();
  if (!ms) {
    g_want_partner = g_join_frames = 0;
    return;
  }
  if (g_want_partner) {
    const int asker = g_want_partner;
    g_want_partner = 0;
    if (join_waiting(ms)) {
      debugPrintf("[menu] 2P asked for by device %d: the other controller joined\n", asker);
      return;
    }
    if (!dcr_config()->controller_screen) {
      debugPrintf("[menu] 2P asked for by device %d, but no other controller plays (press a button on "
                  "a second one to join)\n", asker);
      return;
    }
    debugPrintf("[menu] 2P asked for by device %d with one controller: the controller screen\n", asker);
    if (E.controller_screen && E.controller_screen() == 0)
      g_join_frames = 90; /* what was connected joins over the next frames */
    return;
  }
  /* after the controller screen: the controllers settle into their slots
   * over a few frames, and a joined one that left is dropped by the engine */
  g_join_frames--;
  join_waiting(ms);
  if (dev_of(ms, 0) >= 0 && dev_of(ms, 1) >= 0)
    g_join_frames = 0;
}

/* THE EVENT QUEUE. The engine queues what the Java should do (sounds, the
 * high score, the leaderboard) in its FlapListener: a count (+4), up to 20
 * codes (+8) and their data (+0x58); getOutputEvents turns the codes into
 * the Java's, and resetOutputEvent empties it. Read here first, because
 * getOutputEvents gives the Java nothing for codes it has no Java code for
 * (3, 4, 5): their places in its array are whatever was on its stack, and
 * could pass for a sound. And the data is what the Java never saw: with
 * event 0 (the Java's 9, "a new score"), the round's score. */
int fbf_engine_events(int *codes, int *data, int cap) {
  if (!E.flap_instance || !E.vt_flap)
    return -1;
  const eaddr q = rd32(at((eaddr)E.flap_instance), 0);
  if (!is_obj(q, E.vt_flap))
    return -1; /* not made yet (the first event makes it), or not as known */
  uint8_t *fl = at(q);
  const int n = field(fl, FL_COUNT);
  if (n < 0 || n > FBF_EVENTS_MAX)
    return -1;
  for (int i = 0; i < n && i < cap; i++) {
    codes[i] = field(fl, FL_CODES + 4 * i);
    data[i] = field(fl, FL_DATA + 4 * i);
  }
  return n;
}

#ifdef __SWITCH__
#include "so_util.h"
extern so_module g_mod_game; /* fbf_loader.c */

static uintptr_t vtable(const char *sym) {
  uintptr_t a = so_try_find_addr_rx(&g_mod_game, sym);
  return a ? a + 8 : 0; /* objects point past offset-to-top and the typeinfo */
}

void fbf_menu_init(void) {
  FbfMenuEngine e = {
      .game_instance = (void *(*)(void))so_try_find_addr_rx(&g_mod_game, "_ZN4Game8InstanceEv"),
      .scroll_up = (void (*)(void *))so_try_find_addr_rx(&g_mod_game, "_ZN14SelectorButton8scrollUpEv"),
      .scroll_down = (void (*)(void *))so_try_find_addr_rx(&g_mod_game, "_ZN14SelectorButton10scrollDownEv"),
      .vt_main = vtable("_ZTV9MainScene"),
      .vt_sel = vtable("_ZTV14SelectorButton"),
      .vt_toggle = vtable("_ZTV16dot_ToggleButton"),
      .layout = fbf_menu_layout(g_n.keyreleased != NULL),
      .key = fbf_engine_key,
      .controller_screen = fbf_controller_screen,
      .flap_instance = so_try_find_addr_rx(&g_mod_game, "_ZN12FlapListener14sharedInstanceE"),
      .vt_flap = vtable("_ZTV12FlapListener"),
  };
  fbf_menu_install(&e);
  debugPrintf("[menu] main menu for controllers: %s\n",
              g_ok ? "Up / Down change your bird anywhere, A on the mode button asks for 2P"
                   : "engine symbols missing -- the engine's own controls only");
}
#endif
