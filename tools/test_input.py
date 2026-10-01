#!/usr/bin/env python3
"""Host test for source/fbf_input.c: which controllers play, and the order
and content of the key events the engine receives.

Builds fbf_input.c against tools/host/switch.h (a stand-in for libnx's pad
and touch calls whose controllers the test sets frame by frame) with the
sanitizers, and checks, as GameActivity would pass them on:

  * docked, slots 1 and 2 play (devices 1 and 2); a third controller in slot
    2 behind handheld + slot 1 does not, and plays once one of those leaves;
  * a frame's key-ups reach the game before its key-downs (a press made as
    another key of the same controller is let go is not lost);
  * the stick is the D-pad (either stick: a right Joy-Con's), with a key-up
    when it comes back; after the pause it must come back to the middle
    before it counts again;
  * a single Joy-Con held sideways: its stick turned back (left one
    anticlockwise, right one clockwise) and its four buttons named by where
    they sit (right A, bottom B, top X, left Y); a key-up names the key its
    key-down named; single_joycon_sideways = false turns nothing;
  * + pauses (nothing reaches the game), keys held into the pause give no
    key-up afterwards; a controller that leaves lets its keys go.

    python3 tools/test_input.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
SRC = os.path.join(TOP, 'source')
RT = os.path.join(TOP, 'runtime', 'source')  # the android32 runtime's headers and files

HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include "dcr_config.h"
#include "fbf.h"

HostPad host_pad_no1, host_pad_no2, host_pad_handheld;
HidTouchScreenState host_touch;
u64 host_tick;
static DcrConfig cfg = {.back_exits = 1, .stick_dpad = 1, .joycon_sideways = 1, .plus_pause = 1, .touch = 1,
                        .volume = 100, .res_w = 1280, .res_h = 720};
const DcrConfig *dcr_config(void) { return &cfg; }
void debugPrintf(const char *fmt, ...) { (void)fmt; }
void dcr_window_size(int *w, int *h) { *w = 1280; *h = 720; }

static char ev[2048];
static int paused;
static void add(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  size_t n = strlen(ev);
  if (n) ev[n++] = ' ', ev[n] = 0;
  vsnprintf(ev + n, sizeof ev - n, fmt, ap);
  va_end(ap);
}
void fbf_key(int down, int code, int dev) { add("%c%d@%d", down ? '+' : '-', code, dev); }
void fbf_touch(int down, int x, int y) { add("t%c%d,%d", down ? '+' : '-', x, y); }
Result rt_pad_setup(int max_players, int handheld) { (void)max_players, (void)handheld; return 0; }
void fbf_pause_toggle(void) { paused = !paused; add(paused ? "PAUSE" : "RESUME"); if (paused) fbf_input_swallow_held(); }
int fbf_user_paused(void) { return paused; }

static int fails;
static const char *frame(void) { ev[0] = 0; fbf_input_poll(); return ev; }
static void expect(const char *got, const char *want, int line) {
  if (strcmp(got, want)) { printf("FAIL line %d: got \"%s\", want \"%s\"\n", line, got, want); fails++; }
}
#define EXPECT(want) expect(frame(), want, __LINE__)
static void devices(const char *want, int line) {
  int ids[4], n = fbf_input_devices(ids, 4); char b[64] = "";
  for (int i = 0; i < n; i++) snprintf(b + strlen(b), sizeof b - strlen(b), "%s%d", i ? "," : "", ids[i]);
  expect(b, want, line);
}
#define DEVICES(want) devices(want, __LINE__)
#define PRO HidNpadStyleTag_NpadFullKey

int main(void) {
  fbf_input_init();
  /* docked: slots 1 and 2 */
  host_pad_no1 = (HostPad){1, PRO, 0, {{0, 0}, {0, 0}}};
  host_pad_no2 = (HostPad){1, PRO, 0, {{0, 0}, {0, 0}}};
  EXPECT("");
  DEVICES("1,2");

  /* a press as another key of the same controller is let go: up first */
  host_pad_no1.buttons = HidNpadButton_B;
  EXPECT("+97@1");
  host_pad_no1.buttons = HidNpadButton_A;              /* B up, A down, same frame */
  EXPECT("-97@1 +96@1");
  host_pad_no1.buttons = 0;
  EXPECT("-96@1");

  /* - is Back, the D-pad the D-pad, player 2 its own device */
  host_pad_no2.buttons = HidNpadButton_Minus | HidNpadButton_Up;
  EXPECT("+19@2 +4@2");
  host_pad_no2.buttons = 0;
  EXPECT("-19@2 -4@2");

  /* the stick as the D-pad; a right Joy-Con's stick is the right one */
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, -30000};
  EXPECT("+20@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, -14000};   /* inside the hysteresis: held */
  EXPECT("");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  EXPECT("-20@1");
  host_pad_no2 = (HostPad){1, HidNpadStyleTag_NpadJoyRight, 0, {{0, 0}, {0, 30000}}}; /* its up: right */
  EXPECT("+22@2");
  host_pad_no2.sticks[1] = (HidAnalogStickState){0, 0};
  EXPECT("-22@2");

  /* + pauses: nothing reaches the game; held keys give no key-up after it */
  host_pad_no1.buttons = HidNpadButton_A;
  host_pad_no1.sticks[0] = (HidAnalogStickState){-30000, 0};
  EXPECT("+96@1 +21@1");
  host_pad_no1.buttons = HidNpadButton_A | HidNpadButton_Plus;
  EXPECT("PAUSE");
  host_pad_no1.buttons = HidNpadButton_A | HidNpadButton_X;
  EXPECT("");
  host_pad_no1.buttons = HidNpadButton_A;
  EXPECT("");
  host_pad_no1.buttons = HidNpadButton_A | HidNpadButton_Plus;
  EXPECT("RESUME");
  host_pad_no1.buttons = 0;                                    /* A (held into the pause) up */
  EXPECT("");                                                  /* stick still left: latched */
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  EXPECT("");
  host_pad_no1.sticks[0] = (HidAnalogStickState){-30000, 0};
  EXPECT("+21@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  EXPECT("-21@1");

  /* single Joy-Cons, sideways: the stick and the four buttons turned back */
  host_pad_no1 = (HostPad){1, HidNpadStyleTag_NpadJoyLeft, 0, {{0, 0}, {0, 0}}};
  host_pad_no2 = (HostPad){1, HidNpadStyleTag_NpadJoyRight, 0, {{0, 0}, {0, 0}}};
  EXPECT("");
  host_pad_no1.sticks[0] = (HidAnalogStickState){30000, 0};    /* its right now points up */
  EXPECT("+19@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 30000};    /* its up now points left */
  EXPECT("-19@1 +21@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  EXPECT("-21@1");
  host_pad_no2.sticks[1] = (HidAnalogStickState){30000, 0};    /* its right now points down */
  EXPECT("+20@2");
  host_pad_no2.sticks[1] = (HidAnalogStickState){0, 30000};    /* its up now points right */
  EXPECT("-20@2 +22@2");
  host_pad_no2.sticks[1] = (HidAnalogStickState){0, 0};
  EXPECT("-22@2");
  host_pad_no1.buttons = HidNpadButton_Down;                   /* now on the right: A */
  EXPECT("+96@1");
  host_pad_no1.buttons = HidNpadButton_Left;                   /* at the bottom: B */
  EXPECT("-96@1 +97@1");
  host_pad_no1.buttons = HidNpadButton_Up | HidNpadButton_Right; /* on the left: Y; on top: X */
  EXPECT("-97@1 +100@1 +99@1");
  host_pad_no1.buttons = 0;
  EXPECT("-100@1 -99@1");
  host_pad_no2.buttons = HidNpadButton_X;                      /* now on the right: A */
  EXPECT("+96@2");
  host_pad_no2.buttons = HidNpadButton_A;                      /* at the bottom: B */
  EXPECT("-96@2 +97@2");
  host_pad_no2.buttons = HidNpadButton_B | HidNpadButton_Y;    /* on the left: Y; on top: X */
  EXPECT("-97@2 +100@2 +99@2");
  host_pad_no2.buttons = 0;
  EXPECT("-100@2 -99@2");
  /* a key-up names its key-down's key, though the Joy-Con was joined to
   * another in between */
  host_pad_no1.buttons = HidNpadButton_Down;
  EXPECT("+96@1");
  host_pad_no1.style = HidNpadStyleTag_NpadJoyDual;
  host_pad_no1.buttons = 0;
  EXPECT("-96@1");
  host_pad_no1.buttons = HidNpadButton_Down;                   /* a pair's D-pad is the D-pad */
  EXPECT("+20@1");
  host_pad_no1.buttons = 0;
  EXPECT("-20@1");
  /* upright (single_joycon_sideways = false): nothing turned */
  cfg.joycon_sideways = 0;
  host_pad_no1.style = HidNpadStyleTag_NpadJoyLeft;
  host_pad_no1.buttons = HidNpadButton_Down;
  EXPECT("+20@1");
  host_pad_no1.buttons = 0;
  EXPECT("-20@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){30000, 0};
  EXPECT("+22@1");
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  EXPECT("-22@1");
  cfg.joycon_sideways = 1;
  /* [debug] log_input: the raw report changes nothing */
  cfg.log_input = 1;
  host_tick = 100000000;
  host_pad_no1.sticks[0] = (HidAnalogStickState){30000, 0};
  host_pad_no1.buttons = HidNpadButton_L;
  EXPECT("+102@1 +19@1");
  host_tick += 1000;
  host_pad_no1.sticks[0] = (HidAnalogStickState){0, 0};
  host_pad_no1.buttons = 0;
  EXPECT("-102@1 -19@1");
  cfg.log_input = 0;
  host_pad_no1 = (HostPad){1, PRO, 0, {{0, 0}, {0, 0}}};
  host_pad_no2 = (HostPad){1, PRO, 0, {{0, 0}, {0, 0}}};
  EXPECT("");

  /* handheld + slot 1 play; slot 2 is a third controller */
  host_pad_no2 = (HostPad){1, PRO, 0, {{0, 0}, {0, 0}}};
  host_pad_handheld = (HostPad){1, HidNpadStyleTag_NpadHandheld, 0, {{0, 0}, {0, 0}}};
  EXPECT("");
  DEVICES("3,1");
  host_pad_no2.buttons = HidNpadButton_A;                      /* ignored */
  host_pad_handheld.buttons = HidNpadButton_ZR;
  EXPECT("+105@3");
  host_pad_no1.buttons = HidNpadButton_Y;
  EXPECT("+100@1");
  host_pad_no1.connected = 0;                                  /* slot 1 leaves with Y held */
  EXPECT("-100@1");                                            /* ... and slot 2 now plays */
  DEVICES("3,2");
  host_pad_no2.buttons = 0;                                    /* its A went down before: not ours */
  EXPECT("");
  host_pad_no2.buttons = HidNpadButton_B;
  EXPECT("+97@2");

  /* touch: down when a finger lands, up (where it was) when the last lifts */
  host_touch.count = 1; host_touch.touches[0] = (HidTouchState){0, 640, 360};
  EXPECT("t+640,360");
  host_touch.touches[0] = (HidTouchState){0, 700, 400};
  EXPECT("");
  host_touch.count = 0;
  EXPECT("t-700,400");

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
                               '-I', os.path.join(HERE, 'host'), '-I', SRC, '-I', RT, '-DPORT_PAYLOAD_NAME="fbf_nx"', src,
                               os.path.join(SRC, 'fbf_input.c'), '-o', exe])
        r = subprocess.run([exe], capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip())
            sys.exit(1)
        print('OK: controller choice, event order, stick, single Joy-Cons, pause and touch as specified')


if __name__ == '__main__':
    main()
