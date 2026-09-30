/* fbf_font.h -- lettering in the game's own three styles (fbf_font.c). MIT. */
#ifndef FBF_FONT_H
#define FBF_FONT_H
#include <stdint.h>

/* An RGBA picture, not premultiplied, rows top to bottom. */
typedef struct FbfCanvas {
  uint8_t *px;
  int w, h;
} FbfCanvas;

typedef enum {
  FBF_FONT_TITLE, /* GAME OVER: white blocks 17 art pixels tall, dark edges, a soft shadow */
  FBF_FONT_SMALL, /* the score digits: white, 8 tall, a black edge */
  FBF_FONT_LABEL, /* the score panel's MEDAL / SCORE: salmon, 5 tall, a light line under */
} FbfFont;

/* The game's art is drawn at 2 screen pixels (of its 768x432 picture) per
 * art pixel; so is this. Widths and heights here are in art pixels, of the
 * letters' bodies (the edges and the shadow lie outside). */
int fbf_font_width(FbfFont f, const char *text);
int fbf_font_height(FbfFont f);
/* Whether the font has this character (A-Z, and per font 0-9, space, -). */
int fbf_font_has(FbfFont f, char c);
/* Draws text with the top left of its body at x, y (the picture's pixels,
 * even numbers), alpha-blended over what is there. */
void fbf_font_draw(FbfCanvas *c, FbfFont f, const char *text, int x, int y);

/* Porter-Duff "over" of one non-premultiplied colour onto a pixel. */
void fbf_blend(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#endif
