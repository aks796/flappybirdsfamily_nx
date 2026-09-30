#!/usr/bin/env python3
"""Host test for source/fbf_menu.c, the main menu played with controllers.

Builds fbf_menu.c natively against a model of the engine's main menu written
from the disassembly of libflapfire.so 1.0.4 (MainScene::keyReleased /
update, SelectorButton::select / scrollUp / scrollDown; the objects at the
offsets fbf_menu.c reads, with the vtables it checks), and plays it:

  * Up / Down change the player's own bird with the cursor on the mode button
    or Play (the wrapper), and are left to the engine on the bird itself;
  * a controller that has not joined is the engine's (it joins);
  * A on the mode button with one player: a second playing controller joins
    as player 2 through the engine's own join, its cursor on its bird, and
    the mode becomes 2P; with none, the controller screen, then what was
    connected joins;
  * with two players, Up / Down on the mode button no longer send player 2
    away (the engine's toggle), A still toggles;
  * nothing happens outside the main menu or on objects that are not what
    the port knows; the 1.0 layout (4 bytes less inside the buttons).

    python3 tools/test_menu.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
SRC = os.path.join(TOP, 'source')

HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "dcr_config.h"
#include "fbf.h"

static DcrConfig cfg = {.controller_screen = 1, .stick_dpad = 1, .plus_pause = 1, .volume = 100};
const DcrConfig *dcr_config(void) { return &cfg; }
void debugPrintf(const char *fmt, ...) { (void)fmt; }
static int sounds;
void fbf_audio_play(int id, float v) { (void)v; if (id == FBF_SFX_BUTTON) sounds++; }
static int playing[4], nplaying;
int fbf_input_devices(int *ids, int cap) { int n = 0; for (int i = 0; i < nplaying && n < cap; i++) ids[n++] = playing[i]; return n; }

/* ---- the engine's main menu, modelled on the disassembly ----
 * Its objects live in an arena addressed with 32-bit offsets, as the
 * engine's own 4-byte pointers are (fbf_menu.c's mem_base = the arena). */
static const FbfMenuLayout *L;
static uint8_t mem[0x1000] __attribute__((aligned(16)));
enum { VT_MAIN = 0x10, VT_SEL = 0x20, VT_TOGGLE = 0x30, VT_OTHER = 0x40,
       GAME = 0x100, MS = 0x300, SEL0 = 0x400, SEL1 = 0x500, MODE = 0x600, PLAY = 0x700 };
static uint32_t rd(uint32_t a) { uint32_t v; memcpy(&v, mem + a, 4); return v; }
static void wr(uint32_t a, uint32_t v) { memcpy(mem + a, &v, 4); }
static uint32_t sel(int p) { return p ? SEL1 : SEL0; }
#define BIRD(p) ((int32_t)rd(sel(p) + L->sel_bird))
#define DEV(p) ((int32_t)rd(sel(p) + L->sel_dev))
static uint32_t focus(int p) { return rd(MS + (p ? 0x8c : 0x88)); }
static void set_focus(int p, uint32_t o) { wr(MS + (p ? 0x8c : 0x88), o); }
static void *self_game(void) { return mem + GAME; }
static void scroll_up(void *s) { uint32_t o = (uint32_t)((uint8_t *)s - mem); wr(o + L->sel_bird, (uint32_t)(((int32_t)rd(o + L->sel_bird) + 1) % 3)); }
static void scroll_down(void *s) { uint32_t o = (uint32_t)((uint8_t *)s - mem); int32_t b = (int32_t)rd(o + L->sel_bird) - 1; wr(o + L->sel_bird, (uint32_t)(b < 0 ? 2 : b)); }
static void sel_select(uint32_t s, int d) { wr(s + L->sel_dev, (uint32_t)d); wr(s + L->sel_bird, 0); }
static void sel_reject(uint32_t s) { wr(s + L->sel_dev, (uint32_t)-1); wr(s + L->sel_bird, (uint32_t)-1); }
static int mode_on(void) { return mem[MODE + L->mode_enabled]; }
static int started, toggles, engine_calls;
static void engine_update(void) { /* MainScene::update: the mode is on when both have birds */
  mem[MODE + L->mode_enabled] = BIRD(0) >= 0 && BIRD(1) >= 0;
}
static void engine_release(int key, int dev) { /* MainScene::keyReleased */
  engine_calls++;
  int d1 = DEV(0), d2 = DEV(1);
  if (d1 == -1) { if (d2 != dev) { sel_select(SEL0, dev); set_focus(0, SEL0); } }
  else if (d2 == -1 && d1 != dev) { sel_select(SEL1, dev); set_focus(1, SEL1); }
  int p = DEV(0) == dev ? 0 : DEV(1) == dev ? 1 : -1;
  if (p < 0) return;
  uint32_t cur = focus(p), mine = sel(p);
  if (key == AK_DPAD_LEFT) set_focus(p, cur == PLAY ? mine : cur == mine ? MODE : PLAY);
  else if (key == AK_DPAD_RIGHT) set_focus(p, cur == PLAY ? MODE : cur == MODE ? mine : PLAY);
  else if (key == AK_DPAD_UP || key == AK_DPAD_DOWN) {
    if (cur == mine) { if (key == AK_DPAD_UP) scroll_up(mem + mine); else scroll_down(mem + mine); }
    else if (cur == MODE && mode_on()) { toggles++; sel_reject(SEL1); }
  } else {
    if (cur == MODE && mode_on()) { toggles++; sel_reject(SEL1); }
    else if (cur == PLAY) started++;
    else if (cur == mine) scroll_up(mem + mine);
  }
}
/* the activity: the acting edge (release) goes to the menu first */
static void key(int k, int dev) { if (!fbf_menu_key(k, dev)) engine_release(k, dev); engine_update(); }
static void engine_key(int k, int dev) { engine_release(k, dev); }
static int screens;
static int screen(void) { screens++; return 0; }

static void reset(const FbfMenuLayout *layout) {
  L = layout;
  memset(mem, 0, sizeof mem);
  wr(GAME + 0xf4, 1);                              /* scene: the main menu */
  wr(GAME + 0xf8, MS); wr(GAME + 0x104, MS);
  wr(MS, VT_MAIN);
  wr(SEL0, VT_SEL); wr(SEL1, VT_SEL); sel_reject(SEL0); sel_reject(SEL1);
  wr(MODE, VT_TOGGLE); wr(PLAY, VT_OTHER);
  wr(MS + 0x74, SEL0); wr(MS + 0x78, SEL1); wr(MS + 0x80, MODE); wr(MS + 0x84, PLAY);
  set_focus(0, SEL0); set_focus(1, SEL1);
  engine_update();
  FbfMenuEngine e = {self_game, scroll_up, scroll_down, VT_MAIN, VT_SEL, VT_TOGGLE, layout, engine_key, screen,
                     (uintptr_t)mem, 0, 0};
  fbf_menu_install(&e);
  sounds = started = toggles = screens = engine_calls = 0;
  nplaying = 0;
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void run(const FbfMenuLayout *layout, const char *name) {
  /* one player (the handheld, device 3) joins with Right: cursor on Play */
  reset(layout);
  playing[nplaying++] = 3;
  key(AK_DPAD_RIGHT, 3);
  CHECK(DEV(0) == 3 && focus(0) == PLAY && BIRD(0) == 0);
  /* Up / Down on Play: the bird changes (the engine would do nothing) */
  key(AK_DPAD_UP, 3);   CHECK(BIRD(0) == 1 && sounds == 1);
  key(AK_DPAD_DOWN, 3); CHECK(BIRD(0) == 0);
  key(AK_DPAD_DOWN, 3); CHECK(BIRD(0) == 2);
  /* to the mode button (Right from Play): Up still changes the bird */
  key(AK_DPAD_RIGHT, 3); CHECK(focus(0) == MODE);
  key(AK_DPAD_UP, 3);    CHECK(BIRD(0) == 0 && toggles == 0);
  /* on the bird itself the engine does it */
  key(AK_DPAD_RIGHT, 3); CHECK(focus(0) == SEL0);
  engine_calls = 0;
  key(AK_DPAD_UP, 3);    CHECK(BIRD(0) == 1 && engine_calls == 1);

  /* A on the mode button with one player: another playing controller joins */
  key(AK_DPAD_LEFT, 3);  CHECK(focus(0) == MODE);
  CHECK(!mode_on());
  playing[nplaying++] = 1;                     /* a Pro Controller in slot 1, not joined */
  key(AK_BUTTON_A, 3);   CHECK(DEV(1) == -1);  /* the engine: a disabled button */
  fbf_menu_frame(); engine_update();
  CHECK(DEV(1) == 1 && BIRD(1) == 0 && focus(1) == SEL1);
  CHECK(mode_on());                            /* 2P */
  CHECK(DEV(0) == 3 && BIRD(0) == 1);          /* player 1 untouched */
  /* two players: Down on the mode button changes the bird, player 2 stays */
  key(AK_DPAD_DOWN, 3);  CHECK(BIRD(0) == 0 && DEV(1) == 1 && toggles == 0);
  /* A on it still toggles (2P -> 1P: player 2 leaves) */
  key(AK_BUTTON_A, 3);   CHECK(toggles == 1 && DEV(1) == -1);
  /* player 2's own keys: its cursor, its bird */
  key(AK_BUTTON_B, 1); CHECK(DEV(1) == 1);     /* joins again (the engine) */

  /* one player, nobody else: the controller screen, then who came joins */
  reset(layout);
  playing[nplaying++] = 3;
  key(AK_DPAD_LEFT, 3);  CHECK(DEV(0) == 3 && focus(0) == MODE);
  key(AK_BUTTON_A, 3);
  fbf_menu_frame(); engine_update();
  CHECK(screens == 1 && DEV(1) == -1);
  fbf_menu_frame(); engine_update(); CHECK(DEV(1) == -1);  /* nothing yet */
  playing[nplaying++] = 2;                                 /* connected on the screen */
  fbf_menu_frame(); engine_update();
  CHECK(DEV(1) == 2 && mode_on());
  /* with the screen off: nothing opens */
  reset(layout); cfg.controller_screen = 0;
  playing[nplaying++] = 3;
  key(AK_DPAD_LEFT, 3); key(AK_BUTTON_A, 3); fbf_menu_frame();
  CHECK(screens == 0 && DEV(1) == -1);
  cfg.controller_screen = 1;

  /* a controller that has not joined is the engine's */
  reset(layout);
  playing[nplaying++] = 3;
  key(AK_DPAD_UP, 3);    CHECK(DEV(0) == 3 && BIRD(0) == 1 && engine_calls == 1); /* joined, then scrolled */

  /* not the main menu: nothing */
  reset(layout);
  playing[nplaying++] = 3;
  key(AK_DPAD_RIGHT, 3);
  wr(GAME + 0xf4, 2); wr(GAME + 0x104, PLAY);
  CHECK(fbf_menu_key(AK_DPAD_UP, 3) == 0 && BIRD(0) == 0);
  /* not what the port knows (a vtable): nothing */
  wr(GAME + 0xf4, 1); wr(GAME + 0x104, MS);
  wr(SEL1, VT_OTHER);
  CHECK(fbf_menu_key(AK_DPAD_UP, 3) == 0 && BIRD(0) == 0);
  printf("%s: done\n", name);
}

/* The event queue: FlapListener::sharedInstance -> the queue (its vtable,
 * count +4, codes +8, data +0x58). */
enum { VT_FLAP = 0x50, FL_PTR = 0x800, FL = 0x900 };
static void events(void) {
  reset(fbf_menu_layout(1));
  FbfMenuEngine e = {self_game, scroll_up, scroll_down, VT_MAIN, VT_SEL, VT_TOGGLE, fbf_menu_layout(1), engine_key,
                     screen, (uintptr_t)mem, FL_PTR, VT_FLAP};
  fbf_menu_install(&e);
  int codes[FBF_EVENTS_MAX], data[FBF_EVENTS_MAX];
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == -1); /* not made yet */
  wr(FL_PTR, FL);
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == -1); /* not a FlapListener */
  wr(FL, VT_FLAP);
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == 0);
  wr(FL + 4, 3);
  wr(FL + 8, 13); wr(FL + 0x58, 0);    /* the wing sound */
  wr(FL + 12, 3); wr(FL + 0x5c, 0);    /* no Java code */
  wr(FL + 16, 0); wr(FL + 0x60, 27);   /* a round of 27 */
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == 3);
  CHECK(codes[0] == 13 && codes[1] == 3 && codes[2] == 0 && data[2] == 27);
  wr(FL + 4, 21);                      /* more than it holds: not as known */
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == -1);
  FbfMenuEngine none = {0};
  fbf_menu_install(&none);
  CHECK(fbf_engine_events(codes, data, FBF_EVENTS_MAX) == -1);
}

int main(void) {
  run(fbf_menu_layout(1), "1.0.4 layout");
  run(fbf_menu_layout(0), "1.0 layout");
  events();
  printf("%d failure(s)\n", fails);
  return fails != 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(t, 'h.c')
        open(src, 'w').write(HARNESS)
        exe = os.path.join(t, 'h')
        subprocess.check_call(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                               '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                               '-I', os.path.join(HERE, 'host'), '-I', SRC, src,
                               os.path.join(SRC, 'fbf_menu.c'), '-o', exe])
        r = subprocess.run([exe], capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip())
            sys.exit(1)
        print('OK: main menu -- Up / Down, 2P, the controller screen, both layouts; the event queue')


if __name__ == '__main__':
    main()
