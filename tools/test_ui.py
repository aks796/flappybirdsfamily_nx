#!/usr/bin/env python3
"""Host test for the leaderboard: source/fbf_board.c (the five places and
their file), source/fbf_ui.c (the name entry and the board: what each key
and tap does, when input is the UI's) and source/fbf_font.c (lettering in
the game's styles).

    python3 tools/test_ui.py [the game's APK] [--preview DIR]

Without an APK the UI runs on a made-up atlas (the logic only). With one:
  * the fonts are checked against the game's own art: GAME OVER and GET
    READY redrawn from their letters' shapes, the ten score digits, the
    score panel's BEST label -- the edges, depth and shadow must come out as
    the game's (a few pixels are the artist's own touches);
  * --preview DIR writes the screens as PNGs over a mock of the game over
    screen, to look at (needs Pillow).
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
SRC = os.path.join(TOP, 'source')
RT = os.path.join(TOP, 'runtime', 'source')  # the android32 runtime's headers and files

HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fbf.h"
#include "fbf_font.h"

void debugPrintf(const char *fmt, ...) {
  if (getenv("UI_LOG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
}
static char sounds[512];
void fbf_audio_play(int id, float v) {
  (void)v;
  static const char *n[] = {"die", "hit", "point", "swoosh", "wing", "button"};
  snprintf(sounds + strlen(sounds), sizeof sounds - strlen(sounds), "%s%s", sounds[0] ? " " : "", n[id]);
}
static int saves;
static FbfBoard saved;
int fbf_board_save(const FbfBoard *b) { saves++; saved = *b; return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECK_STR(got, want) do { if (strcmp((got), (want))) { printf("FAIL line %d: got \"%s\", want \"%s\"\n", __LINE__, (got), (want)); fails++; } } while (0)

static void key(int code) { fbf_ui_key(1, code, 1); fbf_ui_key(0, code, 1); }
static void tap(int x, int y) { fbf_ui_touch(1, x, y); fbf_ui_touch(0, x, y); }
static void frames(float s) { for (float t = 0; t < s - 1e-4f; t += 1.0f / 60) fbf_ui_frame(1.0f / 60); }
static const char *shown(void) { int ch; float a; return fbf_ui_picture(&ch, &a) ? "picture" : "none"; }

static void dump(const char *dir, const char *name) {
  int ch; float a;
  const FbfCanvas *c = fbf_ui_picture(&ch, &a);
  if (!c || !dir) return;
  char path[512]; snprintf(path, sizeof path, "%s/%s.raw", dir, name);
  FILE *o = fopen(path, "wb");
  fwrite(&c->w, 4, 1, o); fwrite(&c->h, 4, 1, o); fwrite(c->px, 4, (size_t)c->w * c->h, o); fclose(o);
}
static void font_dump(const char *dir, const char *name, FbfFont f, const char *t) {
  FbfCanvas c; c.w = (fbf_font_width(f, t) + 12) * 2; c.h = (fbf_font_height(f) + 14) * 2;
  c.px = calloc((size_t)c.w * c.h, 4);
  fbf_font_draw(&c, f, t, 12, 12);
  char path[512]; snprintf(path, sizeof path, "%s/%s.raw", dir, name);
  FILE *o = fopen(path, "wb");
  fwrite(&c.w, 4, 1, o); fwrite(&c.h, 4, 1, o); fwrite(c.px, 4, (size_t)c.w * c.h, o); fclose(o);
  free(c.px);
}

/* A made-up atlas: every sprite the UI wants, in a row. */
static const char *k_fake[] = {"score_panel 238 126", "button_enable 64 68", "play 64 68", "selector_p1 24 100",
  "indicator_p1 14 10", "button_exit_selected 74 58", "new 32 14", NULL};
static uint8_t *fake_atlas(char *text, size_t cap) {
  uint8_t *px = calloc(2048 * 2048, 4);
  int x = 0; text[0] = 0;
  for (int i = 0; i < 37; i++) {
    char line[80]; int w, h; char name[40];
    if (i < 7) sscanf(k_fake[i], "%39s %d %d", name, &w, &h);
    else if (i < 17) snprintf(name, sizeof name, "number_score_%02d", i - 7), w = 16, h = 20;
    else if (i < 27) snprintf(name, sizeof name, "number_context_%02d", i - 17), w = 12, h = 14;
    else snprintf(name, sizeof name, "font_%03d", 48 + i - 27), w = 24, h = 44;
    for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) { uint8_t *p = px + ((size_t)yy * 2048 + x + xx) * 4; p[0] = (uint8_t)i; p[3] = 255; }
    snprintf(line, sizeof line, "%s %d %d %.8f 0.0 0.1 0.1\n", name, w, h, x / 2048.0);
    strncat(text, line, cap - strlen(text) - 1);
    x += w + 2;
  }
  return px;
}

int main(int argc, char **argv) {
  const char *dir = argc > 3 ? argv[3] : NULL;
  /* ---------------------------------------------------------- the board */
  FbfBoard b = {0};
  CHECK(fbf_board_place(&b, 9, 10) == -1);
  CHECK(fbf_board_place(&b, 10, 10) == 0);
  const int scores[] = {30, 50, 15, 40, 20};
  const char *names[] = {"ccc", "A", "e e ", "D", "Ee!"};
  for (int i = 0; i < 5; i++) fbf_board_insert(&b, names[i], scores[i]);
  CHECK(b.n == 5);
  CHECK(b.e[0].score == 50 && b.e[4].score == 15);
  CHECK_STR(b.e[0].name, "A");
  CHECK_STR(b.e[2].name, "CCC");
  CHECK_STR(b.e[4].name, "E E");
  CHECK_STR(b.last, "EE"); /* the last one entered, cleaned: "Ee!" -> EE */
  CHECK(fbf_board_place(&b, 15, 10) == -1);  /* ties the fifth: stays off */
  CHECK(fbf_board_place(&b, 16, 10) == 4);
  CHECK(fbf_board_place(&b, 40, 10) == 2);   /* ties go below */
  CHECK(fbf_board_insert(&b, "NEW", 40) == 2);
  CHECK(b.n == 5 && b.e[1].score == 40 && !strcmp(b.e[1].name, "D") && !strcmp(b.e[2].name, "NEW"));
  CHECK(b.e[4].score == 20); /* 15 fell off */
  char text[1024];
  CHECK(fbf_board_format(&b, text, sizeof text) > 0);
  FbfBoard r;
  CHECK(fbf_board_parse(&r, text) == 5);
  CHECK(r.n == b.n && !strcmp(r.last, b.last));
  for (int i = 0; i < b.n; i++)
    CHECK(r.e[i].score == b.e[i].score && !strcmp(r.e[i].name, b.e[i].name));
  /* a hand-edited file: comments, junk, too many, unsorted, CRLF */
  CHECK(fbf_board_parse(&r, "# x\r\n7 LOW\r\nfoo\n12\n-3 NEG\n99 TOP\n12 twelve\n12 SEC\n40 MID\n33 abc\n50 FIF\nlast XY\n") == 5);
  CHECK(r.e[0].score == 99 && r.e[1].score == 50 && r.e[4].score == 12);
  CHECK_STR(r.e[4].name, "TWE"); /* equal scores keep the file's order */
  CHECK_STR(r.last, "XY");
  CHECK(fbf_board_parse(&r, "") == 0 && r.last[0] == 0);

  /* ---------------------------------------------------------- the UI */
  uint8_t *atlas; int aw = 2048, ah = 2048; char atext[8192];
  static char real_text[65536];
  if (argc > 2) {
    FILE *f = fopen(argv[1], "rb"); fread(&aw, 4, 1, f); fread(&ah, 4, 1, f);
    atlas = malloc((size_t)aw * ah * 4); fread(atlas, 4, (size_t)aw * ah, f); fclose(f);
    f = fopen(argv[2], "rb"); real_text[fread(real_text, 1, sizeof real_text - 1, f)] = 0; fclose(f);
    CHECK(fbf_ui_init(atlas, aw, ah, real_text) == 0);
  } else {
    atlas = fake_atlas(atext, sizeof atext);
    CHECK(fbf_ui_init(atlas, aw, ah, atext) == 0);
  }
  free(atlas); /* the UI keeps its own copies */

  FbfBoard empty = {0};
  fbf_ui_set_board(&empty, 1, 10);
  fbf_ui_round_over(9);
  CHECK(!fbf_ui_active());                 /* under min_score */
  fbf_ui_round_over(12);
  CHECK(fbf_ui_active());                  /* input is the UI's from now */
  CHECK(fbf_ui_take_swallow() == 1);
  CHECK(fbf_ui_take_swallow() == 0);
  frames(0.5f);
  CHECK_STR(shown(), "none");              /* the panel's medal shows first */
  key(AK_BUTTON_A);                         /* nothing: not up yet */
  frames(0.5f);
  CHECK_STR(shown(), "picture");
  CHECK(fbf_ui_take_swallow() == 1);
  CHECK_STR(sounds, "swoosh");
  sounds[0] = 0;
  key(AK_DPAD_UP);                          /* too soon after it opened */
  frames(0.3f);
  if (dir) dump(dir, "entry_aaa");
  key(AK_DPAD_UP);                          /* A -> B */
  key(AK_DPAD_DOWN); key(AK_DPAD_DOWN);     /* -> Z (the first letter is never empty) */
  key(AK_DPAD_RIGHT);
  key(AK_DPAD_DOWN);                        /* the second may be: A -> empty */
  key(AK_DPAD_RIGHT);
  key(AK_DPAD_DOWN); key(AK_DPAD_DOWN);     /* third: A -> empty -> Z */
  key(AK_DPAD_RIGHT);                       /* the play button */
  if (dir) dump(dir, "entry_play_focused");
  key(AK_DPAD_UP);                          /* nothing there */
  key(AK_BUTTON_B);                         /* back to the third */
  key(AK_DPAD_UP);                          /* Z -> empty */
  CHECK(saves == 0);
  key(AK_BUTTON_A);                         /* on the third: done */
  CHECK(saves == 1);
  CHECK_STR(saved.e[0].name, "Z");          /* "Z  " */
  CHECK(saved.e[0].score == 12);
  CHECK_STR(saved.last, "Z");
  CHECK(fbf_ui_take_swallow() == 1);        /* the A that closed it is held */
  CHECK_STR(shown(), "picture");            /* the board, the new place marked */
  key(AK_BUTTON_A);                         /* too soon */
  frames(0.3f);
  key(AK_DPAD_DOWN);                        /* the D-pad does not close it */
  CHECK(fbf_ui_active());
  key(AK_BUTTON_B);
  CHECK(!fbf_ui_active());
  CHECK(fbf_ui_take_swallow() == 1);
  CHECK_STR(shown(), "none");

  /* the next entry starts from the last name; + is done */
  fbf_ui_set_board(&saved, 1, 10);
  fbf_ui_round_over(11);
  frames(1.3f);
  fbf_ui_take_swallow();
  key(AK_BUTTON_START);
  CHECK(saves == 2 && saved.n == 2 && !strcmp(saved.e[1].name, "Z") && saved.e[1].score == 11);
  frames(0.3f);
  key(AK_BUTTON_A);
  CHECK(!fbf_ui_active());

  /* held Up runs on: at once, then after 0.4 s every 0.09 s */
  FbfBoard named = {0};
  snprintf(named.last, sizeof named.last, "AKS");
  fbf_ui_set_board(&named, 1, 10);
  fbf_ui_round_over(20);
  frames(1.2f);
  fbf_ui_key(1, AK_DPAD_UP, 1);             /* A -> B */
  frames(0.35f);                            /* not yet */
  frames(0.1f);                             /* C */
  frames(0.09f);                            /* D */
  fbf_ui_key(0, AK_DPAD_UP, 1);
  frames(0.5f);
  key(AK_BUTTON_START);
  CHECK_STR(saved.e[0].name, "DKS");
  frames(0.3f);
  key(AK_BUTTON_A);
  CHECK(!fbf_ui_active());

  /* touch: a letter focuses; the focused one turns; its arrows turn it;
   * the play button is done. Lifts count, where the finger went down. */
  snprintf(named.last, sizeof named.last, "AKS");
  fbf_ui_set_board(&named, 1, 10);
  fbf_ui_round_over(33);
  frames(1.3f);
  const int sx0 = (768 - (4 * 64 + 3 * 12)) / 2;
  tap(sx0 + 76 + 32, 200 + 34);             /* the second letter: focused */
  tap(sx0 + 76 + 32, 200 + 34);             /* again: K -> L */
  tap(sx0 + 76 + 32, 200 - 20);             /* its up arrow: M */
  tap(sx0 + 76 + 32, 200 + 68 + 20);        /* its down arrow: L */
  fbf_ui_touch(1, sx0 + 3 * 76 + 32, 234);  /* the play button, held ... */
  CHECK(saves == 3);
  fbf_ui_touch(0, 10, 10);                  /* ... lifted elsewhere: counts where it went down */
  CHECK(saves == 4);
  CHECK_STR(saved.e[0].name, "ALS");
  frames(0.3f);
  if (dir) dump(dir, "board_marked");
  tap(5, 5);                                 /* anywhere closes the board */
  CHECK(!fbf_ui_active());

  /* the leaderboard button; not while disabled */
  fbf_ui_set_board(&b, 0, 10);
  fbf_ui_show_board();
  CHECK(!fbf_ui_active());
  fbf_ui_round_over(100);
  CHECK(!fbf_ui_active());
  fbf_ui_set_board(&b, 1, 10);
  fbf_ui_show_board();
  CHECK(fbf_ui_active());
  frames(0.3f);
  if (dir) dump(dir, "board_full");
  key(AK_BUTTON_A);
  fbf_ui_set_board(&empty, 1, 10);
  fbf_ui_show_board();
  frames(0.3f);
  if (dir) dump(dir, "board_empty");
  key(AK_BUTTON_A);
  /* a big score, and a name of M and W (the widest letters) */
  FbfBoard wide = {0};
  fbf_board_insert(&wide, "MWM", 98765);
  fbf_board_insert(&wide, "WWW", 1234);
  fbf_board_insert(&wide, "I", 7);
  snprintf(wide.last, sizeof wide.last, "MWM");
  fbf_ui_set_board(&wide, 1, 1);
  fbf_ui_round_over(4321);
  frames(1.3f);
  key(AK_DPAD_RIGHT);
  if (dir) dump(dir, "entry_mwm");
  key(AK_BUTTON_START);
  frames(0.3f);
  if (dir) dump(dir, "board_wide");
  key(AK_BUTTON_A);

  if (dir) {
    font_dump(dir, "font_gameover", FBF_FONT_TITLE, "GAME OVER");
    font_dump(dir, "font_ready", FBF_FONT_TITLE, "GET READY");
    for (int d = 0; d < 10; d++) {
      char t[2] = {(char)('0' + d), 0}, n[16];
      snprintf(n, sizeof n, "font_digit%d", d);
      font_dump(dir, n, FBF_FONT_SMALL, t);
    }
    font_dump(dir, "font_best", FBF_FONT_LABEL, "BEST");
    font_dump(dir, "font_medal", FBF_FONT_LABEL, "MEDAL");
  }
  printf("%d failure(s)\n", fails);
  return fails != 0;
}
'''


def atlas_of(apk):
    from PIL import Image
    z = zipfile.ZipFile(apk)
    png = z.read('res/raw/atlas.png')
    text = z.read('res/raw/atlas_text.txt')
    im = Image.open(io.BytesIO(png)).convert('RGBA')
    # as the port uploads it: Android's decode leaves fully transparent texels black
    px = bytearray(im.tobytes())
    for i in range(0, len(px), 4):
        if px[i + 3] == 0:
            px[i] = px[i + 1] = px[i + 2] = 0
    return im.size, bytes(px), text


def load_raw(path):
    from PIL import Image
    b = open(path, 'rb').read()
    w, h = struct.unpack('<ii', b[:8])
    return Image.frombytes('RGBA', (w, h), b[8:])


class Atlas:
    def __init__(self, size, px, text):
        from PIL import Image
        self.im = Image.frombytes('RGBA', size, px)
        self.t = {}
        for line in text.decode().splitlines():
            p = line.split()
            if len(p) == 7:
                self.t[p[0]] = (int(p[1]), int(p[2]), round(float(p[3]) * size[0]), round(float(p[4]) * size[1]))

    def sprite(self, n):
        w, h, x, y = self.t[n]
        return self.im.crop((x, y, x + w, y + h))


def font_checks(atlas, d):
    """The port's lettering against the game's: count pixels that differ."""
    def bbox(im, pred):
        pts = [(x, y) for y in range(im.height) for x in range(im.width) if pred(im.getpixel((x, y)))]
        return min(p[0] for p in pts), min(p[1] for p in pts)

    def differing(sprite, ours, pred, keep=lambda p: True):
        sx, sy = bbox(sprite, pred)
        ox, oy = bbox(ours, pred)
        n = 0
        for y in range(sprite.height):
            for x in range(sprite.width):
                p = sprite.getpixel((x, y))
                if not keep(p):
                    p = (0, 0, 0, 0)
                X, Y = x - sx + ox, y - sy + oy
                q = ours.getpixel((X, Y)) if 0 <= X < ours.width and 0 <= Y < ours.height else (0, 0, 0, 0)
                if (p[3] or q[3]) and max(abs(a - b) for a, b in zip(p, q)) > 2:
                    n += 1
        return n

    white = lambda p: p[3] == 255 and p[0] > 200
    fails = 0
    # GAME OVER: one pixel of R is the artist's (4 screen pixels, and the
    # shadow near it); GET READY: the corners of D and Y
    for name, sprite, limit in [('GAME OVER', 'gameover', 40), ('GET READY', 'ready', 200)]:
        n = differing(atlas.sprite(sprite), load_raw(os.path.join(d, 'font_%s.raw' % sprite)), white)
        ok = n <= limit
        fails += not ok
        print('%s %-9s redrawn: %d of its pixels differ (the artist\'s own touches: up to %d)' %
              ('OK:  ' if ok else 'FAIL:', name, n, limit))
    n = sum(differing(atlas.sprite('number_score_%02d' % i), load_raw(os.path.join(d, 'font_digit%d.raw' % i)), white)
            for i in range(10))
    fails += n != 0
    print('%s the ten score digits redrawn: %d pixels differ' % ('OK:  ' if n == 0 else 'FAIL:', n))
    panel = atlas.sprite('score_panel').crop((120, 40, 238, 100))  # BEST, under SCORE
    lab = lambda p: p[:3] in ((252, 120, 88), (240, 234, 161))
    n = differing(panel, load_raw(os.path.join(d, 'font_best.raw')), lambda p: p[:3] == (252, 120, 88), lab)
    fails += n != 0
    print('%s the score panel\'s BEST redrawn: %d pixels differ' % ('OK:  ' if n == 0 else 'FAIL:', n))
    return fails


def mock_game_over(atlas):
    """The 1P game over screen, roughly: something to lay the UI over."""
    from PIL import Image
    bg = Image.new('RGBA', (768, 432), (0, 0, 0, 255))
    bg.alpha_composite(atlas.sprite('bg'), (0, 0))
    bg.alpha_composite(atlas.sprite('ground'), (0, 432 - 110))
    bg.alpha_composite(atlas.sprite('pipe_up'), (150, 260))
    bg.alpha_composite(atlas.sprite('gameover'), ((768 - 188) // 2, 70))
    bg.alpha_composite(atlas.sprite('score_panel'), ((768 - 238) // 2, 140))
    bg.alpha_composite(atlas.sprite('medals_1'), ((768 - 238) // 2 + 26, 140 + 42))
    bg.alpha_composite(atlas.sprite('button_continue'), (768 // 2 - 90, 290))
    bg.alpha_composite(atlas.sprite('button_score'), (768 // 2 + 16, 290))
    return bg


def main():
    args = [a for a in sys.argv[1:]]
    preview = None
    if '--preview' in args:
        i = args.index('--preview')
        preview = os.path.abspath(args[i + 1])
        del args[i:i + 2]
        os.makedirs(preview, exist_ok=True)
    apk = args[0] if args else None
    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(t, 'h.c')
        open(src, 'w').write(HARNESS)
        exe = os.path.join(t, 'h')
        subprocess.check_call(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                               '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                               '-I', SRC, '-I', RT, '-DPORT_PAYLOAD_NAME="fbf_nx"', src] + [os.path.join(SRC, f) for f in
                                                  ('fbf_board.c', 'fbf_ui.c', 'fbf_font.c')] + ['-o', exe])
        run = [exe]
        atlas = None
        if apk:
            size, px, text = atlas_of(apk)
            atlas = Atlas(size, px, text)
            ap, tp = os.path.join(t, 'atlas.raw'), os.path.join(t, 'atlas.txt')
            open(ap, 'wb').write(struct.pack('<ii', *size) + px)
            open(tp, 'wb').write(text)
            run += [ap, tp, t]
        r = subprocess.run(run, capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip())
            sys.exit(1)
        fails = 0
        if atlas:
            fails = font_checks(atlas, t)
            if preview:
                bg = mock_game_over(atlas)
                for f in sorted(os.listdir(t)):
                    if f.endswith('.raw') and not f.startswith('font_') and f != 'atlas.raw':
                        im = bg.copy()
                        im.alpha_composite(load_raw(os.path.join(t, f)))
                        im.convert('RGB').save(os.path.join(preview, f[:-4] + '.png'))
                print('previews in', preview)
        if fails:
            sys.exit(1)
        print('OK: the leaderboard (places, file), the name entry and the board (keys, touch, input '
              'hand-over)%s' % (', and the lettering against the game\'s art' if atlas else ''))


if __name__ == '__main__':
    main()
