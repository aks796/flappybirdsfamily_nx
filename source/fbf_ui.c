/* fbf_ui.c -- the leaderboard's two screens, in the game's own art.
 *
 * The game's leaderboard button (on the 1P game over screen) asked Amazon
 * GameCircle for a leaderboard; here it shows the port's own (fbf_board.c):
 *
 *   NAME ENTRY, when a 1P round makes the board. The engine reports a
 *   finished round's score once its panel has counted up (its event 0); a
 *   moment later -- the medal and the NEW sign have shown -- the screen dims
 *   and HIGH SCORE, the score, and three letters in the main menu's green
 *   squares come up, with the main menu's play button after them. As the
 *   main menu's bird selector works, so do the letters: the focused one has
 *   the selector's blue arrows,
 *       Up / Down      the next / previous letter (held: they run on)
 *       Left / Right   the letter before / after, and the play button
 *       A (any button) the next letter; on the last one or the play button,
 *                      done
 *       B, -           the letter before
 *       +              done (when + does not pause)
 *   The first letter is A-Z; the second and third may also be left empty.
 *   The letters start as the last name entered. Touch: a tap on a letter
 *   focuses it (on the focused one: the next letter), on its arrows turns
 *   it, on the play button is done. Then the board, the new place marked.
 *
 *   THE BOARD, on the leaderboard button and after an entry: LEADERBOARD
 *   over a panel like the score panel with the five places -- place, name,
 *   score -- and the game's exit button. Any button or a tap closes it.
 *
 * While either shows (and in the moment before the entry), the controllers
 * and the touch screen are the UI's: the game underneath gets nothing, and
 * keys held as it opens or closes give the game no key-up (it acts on
 * releases). The engine keeps running under the dimmed screen.
 *
 * Everything is drawn from the game's atlas -- the score panel, the green
 * squares, the play and exit buttons, the selector's arrows, the NEW sign
 * and the digits, copied out of the user's own APK at start-up -- and
 * lettering in its styles (fbf_font.c), into one 768x432 picture, the size
 * of the engine's own, which fbf_overlay.c lays over its frame the way the
 * engine draws its sprites. No art ships with the port. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbf.h"
#include "fbf_font.h"
#include "util.h"

/* ------------------------------------------------------------ sprites */
typedef struct {
  int w, h;
  uint8_t *px;
} Sprite;

enum {
  SP_PANEL, SP_SQUARE, SP_PLAY, SP_SELECTOR, SP_INDICATOR, SP_EXIT, SP_NEW,
  SP_DIGIT,                 /* number_score_00..09: the panel's score digits */
  SP_RANK = SP_DIGIT + 10,  /* number_context_00..09 */
  SP_BIG = SP_RANK + 10,    /* font_048..057: the big score digits */
  SP_COUNT = SP_BIG + 10
};
static Sprite g_sp[SP_COUNT];
static int g_have_sprites;

static void sprite_name(int i, char *out, size_t cap) {
  static const char *const fixed[] = {"score_panel", "button_enable", "play", "selector_p1",
                                      "indicator_p1", "button_exit_selected", "new"};
  if (i < SP_DIGIT)
    snprintf(out, cap, "%s", fixed[i]);
  else if (i < SP_RANK)
    snprintf(out, cap, "number_score_%02d", i - SP_DIGIT);
  else if (i < SP_BIG)
    snprintf(out, cap, "number_context_%02d", i - SP_RANK);
  else
    snprintf(out, cap, "font_%03d", 48 + i - SP_BIG);
}

/* atlas_text.txt: "name w h u v du dv" a line, u v the top left in [0,1). */
int fbf_ui_init(const uint8_t *rgba, int aw, int ah, const char *text) {
  int found = 0;
  for (int i = 0; i < SP_COUNT; i++) {
    free(g_sp[i].px);
    g_sp[i].px = NULL;
  }
  for (const char *p = text; p && *p;) {
    const char *e = p + strcspn(p, "\r\n");
    char line[128], name[64];
    int w, h;
    double u, v;
    snprintf(line, sizeof line, "%.*s", (int)(e - p < 127 ? e - p : 127), p);
    p = *e ? e + 1 : e;
    if (sscanf(line, "%63s %d %d %lf %lf", name, &w, &h, &u, &v) != 5)
      continue;
    for (int i = 0; i < SP_COUNT; i++) {
      char want[32];
      sprite_name(i, want, sizeof want);
      if (strcmp(name, want) || g_sp[i].px)
        continue;
      const int x = (int)lround(u * aw), y = (int)lround(v * ah);
      if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > aw || y + h > ah)
        continue;
      g_sp[i].px = malloc((size_t)w * (size_t)h * 4);
      if (!g_sp[i].px)
        continue;
      g_sp[i].w = w, g_sp[i].h = h;
      for (int r = 0; r < h; r++)
        memcpy(g_sp[i].px + (size_t)r * (size_t)w * 4, rgba + ((size_t)(y + r) * (size_t)aw + (size_t)x) * 4,
               (size_t)w * 4);
      found++;
    }
  }
  g_have_sprites = found == SP_COUNT;
  if (!g_have_sprites) {
    for (int i = 0; i < SP_COUNT; i++)
      if (!g_sp[i].px) {
        char n[32];
        sprite_name(i, n, sizeof n);
        debugPrintf("[board] the atlas has no %s: the leaderboard is off\n", n);
        break;
      }
  }
  return g_have_sprites ? 0 : -1;
}

/* ------------------------------------------------------------ drawing */
static FbfCanvas g_cv;

static void blit_part(const Sprite *s, int sx, int sy, int w, int h, int x, int y) {
  for (int r = 0; r < h; r++) {
    const int ty = y + r;
    if (ty < 0 || ty >= g_cv.h)
      continue;
    for (int k = 0; k < w; k++) {
      const int tx = x + k;
      if (tx < 0 || tx >= g_cv.w)
        continue;
      const uint8_t *p = s->px + ((size_t)(sy + r) * (size_t)s->w + (size_t)(sx + k)) * 4;
      fbf_blend(g_cv.px + ((size_t)ty * (size_t)g_cv.w + (size_t)tx) * 4, p[0], p[1], p[2], p[3]);
    }
  }
}
static void blit(int sp, int x, int y) { blit_part(&g_sp[sp], 0, 0, g_sp[sp].w, g_sp[sp].h, x, y); }

/* The score panel, any size: its corners as they are, its edges and its
 * middle drawn out from one clean line of each (the labels, the medal's
 * place and the digits' are left out). */
#define PANEL_CORNER 20
static void panel(int x, int y, int w, int h) {
  const Sprite *s = &g_sp[SP_PANEL];
  const int c = PANEL_CORNER, mx = s->w / 2, my = s->h / 2 - 4, r = s->w - c, b = s->h - c;
  blit_part(s, 0, 0, c, c, x, y);
  blit_part(s, r, 0, c, c, x + w - c, y);
  blit_part(s, 0, b, c, c, x, y + h - c);
  blit_part(s, r, b, c, c, x + w - c, y + h - c);
  for (int i = c; i < w - c; i++) {
    blit_part(s, mx, 0, 1, c, x + i, y);
    blit_part(s, mx, b, 1, c, x + i, y + h - c);
  }
  for (int j = c; j < h - c; j++) {
    blit_part(s, 0, my, c, 1, x, y + j);
    blit_part(s, r, my, c, 1, x + w - c, y + j);
    for (int i = c; i < w - c; i++)
      blit_part(s, mx, my, 1, 1, x + i, y + j);
  }
}

/* A number in sprite digits, right edge at x (right) or centred on it. */
static int number_width(int first, int n) {
  char t[16];
  snprintf(t, sizeof t, "%d", n);
  int w = 0;
  for (const char *p = t; *p; p++)
    w += g_sp[first + (*p - '0')].w;
  return w;
}
static void number(int first, int n, int x, int y) {
  char t[16];
  snprintf(t, sizeof t, "%d", n);
  for (const char *p = t; *p; p++) {
    blit(first + (*p - '0'), x, y);
    x += g_sp[first + (*p - '0')].w;
  }
}

static void dim(void) {
  for (size_t i = 0; i < (size_t)g_cv.w * (size_t)g_cv.h; i++) {
    uint8_t *p = g_cv.px + i * 4;
    p[0] = p[1] = p[2] = 0;
    p[3] = 168; /* the game under it stays, but gives way */
  }
}

static void title(const char *t) {
  const int w = fbf_font_width(FBF_FONT_TITLE, t) * 2;
  fbf_font_draw(&g_cv, FBF_FONT_TITLE, t, (FBF_UI_W - w) / 2 / 2 * 2, 40);
}

/* ------------------------------------------------------------ state */
typedef enum { UI_OFF, UI_WAIT, UI_ENTRY, UI_BOARD } Mode;
static const char *const k_mode[] = {"off", "waiting", "name entry", "leaderboard"};

static FbfBoard g_board;
static int g_enabled, g_min_score = 10;
static Mode g_mode;
static float g_t;         /* in this mode */
static int g_score, g_slot, g_mark = -1;
static char g_name[3];
static int g_rep_key;     /* Up / Down held on a letter */
static float g_rep_t;
static int g_touching, g_touch_x, g_touch_y;
static int g_swallow, g_changed;
static int g_phase = -1;  /* the arrows' bob, drawn */

#define WAIT_S 0.9f    /* the panel's medal and NEW show first */
#define DEAF_S 0.25f   /* a new screen takes no keys at once */
#define FADE_S 0.18f
#define REPEAT_DELAY 0.40f
#define REPEAT_EVERY 0.09f

/* The entry's row: three letters and the play button. */
#define SQ_W 64
#define SQ_H 68
#define SQ_GAP 12
#define SQ_Y 200
static int square_x(int i) { return (FBF_UI_W - (4 * SQ_W + 3 * SQ_GAP)) / 2 + i * (SQ_W + SQ_GAP); }

/* The board's panel. */
#define PN_W 264
#define PN_H 184
#define PN_X ((FBF_UI_W - PN_W) / 2)
#define PN_Y 100
#define ROW_Y (PN_Y + 46)
#define ROW_H 24
#define EXIT_Y (PN_Y + PN_H + 10)

static void set_mode(Mode m) {
  if (m != g_mode)
    debugPrintf("[board] %s -> %s\n", k_mode[g_mode], k_mode[m]);
  if ((g_mode == UI_OFF) != (m == UI_OFF) || m == UI_ENTRY || m == UI_BOARD)
    g_swallow = 1; /* input changes hands: what is held now ends here */
  g_mode = m;
  g_t = 0.0f;
  g_rep_key = 0;
  g_touching = 0;
  g_changed = 1;
}

void fbf_ui_set_board(const FbfBoard *b, int enabled, int min_score) {
  g_board = *b;
  g_enabled = enabled;
  g_min_score = min_score;
}

int fbf_ui_active(void) { return g_mode != UI_OFF; }

int fbf_ui_take_swallow(void) {
  const int s = g_swallow;
  g_swallow = 0;
  return s;
}

void fbf_ui_round_over(int score) {
  if (!g_enabled || !g_have_sprites || g_mode != UI_OFF)
    return;
  const int place = fbf_board_place(&g_board, score, g_min_score);
  if (place < 0) {
    debugPrintf("[board] a round of %d: not on the leaderboard (%s)\n", score,
                score < g_min_score ? "under min_score" : "five better ones");
    return;
  }
  debugPrintf("[board] a round of %d: place %d -- name entry\n", score, place + 1);
  g_score = score;
  set_mode(UI_WAIT);
}

void fbf_ui_show_board(void) {
  if (!g_enabled || !g_have_sprites || g_mode != UI_OFF)
    return;
  g_mark = -1;
  set_mode(UI_BOARD);
  fbf_audio_play(FBF_SFX_SWOOSHING, 1.0f);
}

/* ----------------------------------------------------------- the entry */
static const char k_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

static void turn(int by) {
  const int n = g_slot == 0 ? 26 : 27; /* the first letter is never empty */
  const char *at = strchr(k_letters, g_name[g_slot]);
  int i = at ? (int)(at - k_letters) : 0;
  i = ((i + by) % n + n) % n;
  g_name[g_slot] = k_letters[i];
  g_changed = 1;
}

static void begin_entry(void) {
  /* the last name, or AAA */
  const size_t n = strlen(g_board.last);
  for (int i = 0; i < 3; i++)
    g_name[i] = !n ? 'A' : (size_t)i < n ? g_board.last[i] : ' ';
  if (g_name[0] == ' ')
    g_name[0] = 'A';
  g_slot = 0;
  set_mode(UI_ENTRY);
  fbf_audio_play(FBF_SFX_SWOOSHING, 1.0f);
}

static void done(void) {
  char name[4] = {g_name[0], g_name[1], g_name[2], 0};
  g_mark = fbf_board_insert(&g_board, name, g_score);
  debugPrintf("[board] \"%s\" %d at place %d\n", g_board.last, g_score, g_mark + 1);
  fbf_board_save(&g_board);
  fbf_audio_play(FBF_SFX_POINT, 1.0f / 3.0f);
  set_mode(UI_BOARD);
}

static void move(int to) {
  if (to < 0 || to > 3 || to == g_slot)
    return;
  g_slot = to;
  g_changed = 1;
  fbf_audio_play(FBF_SFX_BUTTON, 0.6f);
}

static void entry_key(int code) {
  switch (code) {
  case AK_DPAD_UP:
  case AK_DPAD_DOWN:
    if (g_slot < 3) {
      turn(code == AK_DPAD_UP ? 1 : -1);
      fbf_audio_play(FBF_SFX_BUTTON, 0.6f);
      g_rep_key = code;
      g_rep_t = 0.0f;
    }
    break;
  case AK_DPAD_LEFT: move(g_slot - 1); break;
  case AK_DPAD_RIGHT: move(g_slot + 1); break;
  case AK_BUTTON_B:
  case AK_BACK: move(g_slot - 1); break;
  case AK_BUTTON_START: done(); break;
  default: /* A, and any other button: on to the next letter, or done */
    if (g_slot >= 2)
      done();
    else
      move(g_slot + 1);
    break;
  }
}

void fbf_ui_key(int down, int code, int device) {
  (void)device;
  if (!down) {
    if (code == g_rep_key)
      g_rep_key = 0;
    return;
  }
  if (g_t < DEAF_S)
    return;
  if (g_mode == UI_ENTRY)
    entry_key(code);
  else if (g_mode == UI_BOARD && !(code >= AK_DPAD_UP && code <= AK_DPAD_RIGHT)) {
    fbf_audio_play(FBF_SFX_BUTTON, 1.0f);
    set_mode(UI_OFF);
  }
}

static int hit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

/* A tap: acted on when the finger lifts, where it went down. */
void fbf_ui_touch(int down, int x, int y) {
  if (down) {
    g_touching = g_t >= DEAF_S && (g_mode == UI_ENTRY || g_mode == UI_BOARD);
    g_touch_x = x, g_touch_y = y;
    return;
  }
  if (!g_touching)
    return;
  g_touching = 0;
  x = g_touch_x, y = g_touch_y;
  if (g_mode == UI_BOARD) {
    fbf_audio_play(FBF_SFX_BUTTON, 1.0f);
    set_mode(UI_OFF);
    return;
  }
  if (g_mode != UI_ENTRY)
    return;
  if (g_slot < 3) { /* the focused letter's arrows */
    const int ax = square_x(g_slot) - 8;
    if (hit(x, y, ax, SQ_Y - 34, SQ_W + 16, 34)) {
      turn(1);
      fbf_audio_play(FBF_SFX_BUTTON, 0.6f);
      return;
    }
    if (hit(x, y, ax, SQ_Y + SQ_H, SQ_W + 16, 34)) {
      turn(-1);
      fbf_audio_play(FBF_SFX_BUTTON, 0.6f);
      return;
    }
  }
  for (int i = 0; i < 4; i++)
    if (hit(x, y, square_x(i) - SQ_GAP / 2, SQ_Y, SQ_W + SQ_GAP, SQ_H)) {
      if (i == 3)
        done();
      else if (i == g_slot) {
        turn(1);
        fbf_audio_play(FBF_SFX_BUTTON, 0.6f);
      } else
        move(i);
      return;
    }
}

void fbf_ui_frame(float dt) {
  if (g_mode == UI_OFF)
    return;
  if (dt > 0.1f)
    dt = 0.1f; /* back from the HOME menu */
  g_t += dt;
  if (g_mode == UI_WAIT && g_t >= WAIT_S)
    begin_entry();
  if (g_mode == UI_ENTRY && g_rep_key) {
    g_rep_t += dt;
    if (g_rep_t >= REPEAT_DELAY) {
      g_rep_t -= REPEAT_EVERY;
      turn(g_rep_key == AK_DPAD_UP ? 1 : -1);
      fbf_audio_play(FBF_SFX_BUTTON, 0.35f);
    }
  }
}

/* ---------------------------------------------------------- the pictures */
static void draw_entry(int bob) {
  title("HIGH SCORE");
  const int sw = number_width(SP_BIG, g_score);
  number(SP_BIG, g_score, (FBF_UI_W - sw) / 2 / 2 * 2, 104);
  for (int i = 0; i < 4; i++) {
    const int x = square_x(i);
    blit(i < 3 ? SP_SQUARE : SP_PLAY, x, SQ_Y);
    if (i < 3 && g_name[i] != ' ') {
      const char t[2] = {g_name[i], 0};
      const int lw = fbf_font_width(FBF_FONT_TITLE, t) * 2;
      /* in the square's green: 22 x 21 art pixels from (5, 5) */
      fbf_font_draw(&g_cv, FBF_FONT_TITLE, t, x + ((SQ_W - lw) / 2 / 2) * 2, SQ_Y + 12);
    }
  }
  const Sprite *sel = &g_sp[SP_SELECTOR];
  const int x = square_x(g_slot);
  if (g_slot < 3) { /* the selector's two arrows, above and below */
    const int ah = 16, sx = x + (SQ_W - sel->w) / 2;
    blit_part(sel, 0, 0, sel->w, ah, sx, SQ_Y - ah - 2 - bob);
    blit_part(sel, 0, sel->h - ah, sel->w, ah, sx, SQ_Y + SQ_H + 2 + bob);
  } else {
    const Sprite *ind = &g_sp[SP_INDICATOR];
    blit(SP_INDICATOR, x + (SQ_W - ind->w) / 2, SQ_Y - ind->h - 4 - bob);
  }
}

static void draw_board(int bob) {
  (void)bob;
  title("LEADERBOARD");
  panel(PN_X, PN_Y, PN_W, PN_H);
  const int name_x = PN_X + 50, score_r = PN_X + PN_W - 22;
  fbf_font_draw(&g_cv, FBF_FONT_LABEL, "NAME", name_x, PN_Y + 22);
  const int lw = fbf_font_width(FBF_FONT_LABEL, "SCORE") * 2;
  fbf_font_draw(&g_cv, FBF_FONT_LABEL, "SCORE", score_r - lw, PN_Y + 22);
  for (int i = 0; i < FBF_BOARD_SIZE; i++) {
    const int y = ROW_Y + i * ROW_H;
    blit(SP_RANK + i + 1, PN_X + 24, y + 4);
    if (i < g_board.n) {
      fbf_font_draw(&g_cv, FBF_FONT_SMALL, g_board.e[i].name, name_x, y + 2);
      if (i == g_mark)
        blit(SP_NEW, name_x + 72, y + 3);
      number(SP_DIGIT, g_board.e[i].score, score_r - number_width(SP_DIGIT, g_board.e[i].score), y);
    } else {
      fbf_font_draw(&g_cv, FBF_FONT_SMALL, "---", name_x, y + 2);
    }
  }
  blit(SP_EXIT, (FBF_UI_W - g_sp[SP_EXIT].w) / 2, EXIT_Y);
}

const FbfCanvas *fbf_ui_picture(int *changed, float *alpha) {
  *changed = 0;
  *alpha = 1.0f;
  if (g_mode != UI_ENTRY && g_mode != UI_BOARD)
    return NULL;
  if (!g_cv.px) {
    g_cv.w = FBF_UI_W, g_cv.h = FBF_UI_H;
    g_cv.px = malloc((size_t)g_cv.w * (size_t)g_cv.h * 4);
    if (!g_cv.px)
      return NULL;
  }
  const int phase = g_mode == UI_ENTRY ? (int)(g_t / 0.35f) & 1 : 0;
  if (g_changed || phase != g_phase) {
    g_changed = 0;
    g_phase = phase;
    dim();
    if (g_mode == UI_ENTRY)
      draw_entry(phase * 2);
    else
      draw_board(phase * 2);
    *changed = 1;
  }
  *alpha = g_t < FADE_S ? g_t / FADE_S : 1.0f;
  return &g_cv;
}
