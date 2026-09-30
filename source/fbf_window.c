/* fbf_window.c -- the one window: libnx's default NWindow, at the rendering
 * size config.ini chose (the vi layer scales it to the screen). The shared
 * EGL layer (gl_mesa.c) calls dcr_window_prepare right before it makes the
 * window surface. (The PvZ port kept these in android_ndk.c, beside its
 * ANativeWindow; this game has no native window of its own.) MIT. */
#include <switch.h>

#include "config.h"
#include "fbf.h"
#include "util.h"

static int g_win_w = DCR_FORCE_SCREEN_W, g_win_h = DCR_FORCE_SCREEN_H;

void dcr_window_size(int *w, int *h) {
  if (w)
    *w = g_win_w;
  if (h)
    *h = g_win_h;
}

void dcr_window_set_size(int w, int h) {
  if (w > 0 && h > 0) {
    g_win_w = w;
    g_win_h = h;
  }
}

void dcr_window_prepare(void) {
  NWindow *w = nwindowGetDefault();
  if (!nwindowIsValid(w) || log_console_active())
    return; /* the on-screen boot log still owns it */
  nwindowSetDimensions(w, (u32)g_win_w, (u32)g_win_h);
  nwindowSetCrop(w, 0, 0, (u32)g_win_w, (u32)g_win_h);
  nwindowSetSwapInterval(w, 1);
}
