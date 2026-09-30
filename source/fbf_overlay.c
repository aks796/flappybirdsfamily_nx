/* fbf_overlay.c -- what the port draws over the game's frame, before it is
 * presented: the leaderboard's picture (fbf_ui.c), the pause screen (the
 * screen dimmed, a pause sign in the middle), and the alpha fix-up; and,
 * before the engine starts, the splash screen's picture.
 *
 * It draws between two of the engine's frames without disturbing it. The
 * engine sets its framebuffer, viewport, program, attribute pointers,
 * texture, blend function and clear colour itself at every frame
 * (dot_GL::render), but enables blending, scissoring and its attribute arrays
 * once, and never touches the colour mask; so exactly those are saved and put
 * back here. Like the engine, it draws from client-side arrays (no buffer
 * objects: binding one would break the engine's own client-side draws).
 *
 * The alpha fix-up: the game asked for an opaque RGB565 window, but Mesa's
 * Switch platform has RGBA8888 only, and the engine leaves whatever alpha its
 * blending produced; the frame's alpha is set to 1 before it goes to the
 * compositor, so what the display shows cannot depend on it. MIT.
 */
#include <math.h>
#include <string.h>

#include "fbf.h"
#include "fbf_font.h"
#include "fbf_gl.h"
#include "gl_layer.h"
#include "util.h"

static FbfGl g_gl;
static int g_gl_state; /* 0 not looked up, 1 ready, -1 missing */

const FbfGl *fbf_gl(void) {
  if (!g_gl_state) {
    int missing = 0;
#define FBF_GL_LOOK(ret, name, args)                                                        \
  g_gl.name = (ret(*) args)dcr_gl_lookup(#name);                                            \
  if (!g_gl.name) {                                                                         \
    debugPrintf("[overlay] no %s\n", #name);                                                \
    missing++;                                                                              \
  }
    FBF_GL_FUNCS(FBF_GL_LOOK)
#undef FBF_GL_LOOK
    g_gl_state = missing ? -1 : 1;
  }
  return g_gl_state > 0 ? &g_gl : NULL;
}

static fGLuint g_prog, g_prog_tex;
static fGLint g_u_color, g_u_tex, g_u_alpha;
static int g_ready, g_ready_tex;

static fGLuint compile(const FbfGl *gl, fGLenum type, const char *src) {
  fGLuint sh = gl->glCreateShader(type);
  gl->glShaderSource(sh, 1, &src, NULL);
  gl->glCompileShader(sh);
  fGLint ok = 0;
  gl->glGetShaderiv(sh, F_GL_COMPILE_STATUS, &ok);
  if (!ok)
    debugPrintf("[overlay] shader did not compile\n");
  return sh;
}

int fbf_overlay_init(void) {
  const FbfGl *gl = fbf_gl();
  if (!gl)
    return -1;
  g_prog = gl->glCreateProgram();
  gl->glAttachShader(g_prog, compile(gl, F_GL_VERTEX_SHADER,
                                     "attribute vec2 p;\n"
                                     "void main() { gl_Position = vec4(p, 0.0, 1.0); }\n"));
  gl->glAttachShader(g_prog, compile(gl, F_GL_FRAGMENT_SHADER,
                                     "precision mediump float;\n"
                                     "uniform vec4 c;\n"
                                     "void main() { gl_FragColor = c; }\n"));
  gl->glBindAttribLocation(g_prog, 0, "p");
  gl->glLinkProgram(g_prog);
  fGLint linked = 0;
  gl->glGetProgramiv(g_prog, F_GL_LINK_STATUS, &linked);
  g_u_color = gl->glGetUniformLocation(g_prog, "c");
  g_ready = linked != 0;

  /* a picture: the splash screen */
  g_prog_tex = gl->glCreateProgram();
  gl->glAttachShader(g_prog_tex, compile(gl, F_GL_VERTEX_SHADER,
                                         "attribute vec2 p;\n"
                                         "attribute vec2 uv;\n"
                                         "varying vec2 v;\n"
                                         "void main() { v = uv; gl_Position = vec4(p, 0.0, 1.0); }\n"));
  gl->glAttachShader(g_prog_tex, compile(gl, F_GL_FRAGMENT_SHADER,
                                         "precision mediump float;\n"
                                         "uniform sampler2D t;\n"
                                         "uniform float a;\n"
                                         "varying vec2 v;\n"
                                         "void main() { vec4 c = texture2D(t, v); gl_FragColor = vec4(c.rgb, c.a * a); }\n"));
  gl->glBindAttribLocation(g_prog_tex, 0, "p");
  gl->glBindAttribLocation(g_prog_tex, 1, "uv");
  gl->glLinkProgram(g_prog_tex);
  linked = 0;
  gl->glGetProgramiv(g_prog_tex, F_GL_LINK_STATUS, &linked);
  g_u_tex = gl->glGetUniformLocation(g_prog_tex, "t");
  g_u_alpha = gl->glGetUniformLocation(g_prog_tex, "a");
  g_ready_tex = linked != 0;
  debugPrintf("[overlay] pause overlay %s\n", g_ready ? "ready" : "NOT available (program did not link)");
  return g_ready ? 0 : -1;
}

/* ------------------------------------------------------------ the state */
#define ATTRIBS 4
typedef struct {
  fGLint fb;
  fGLboolean scissor, blend, depth, stencil, cull;
  fGLint attrib_on[ATTRIBS];
} Saved;

static void save(const FbfGl *gl, Saved *s) {
  s->fb = 0;
  gl->glGetIntegerv(F_GL_FRAMEBUFFER_BINDING, &s->fb);
  s->scissor = gl->glIsEnabled(F_GL_SCISSOR_TEST);
  s->blend = gl->glIsEnabled(F_GL_BLEND);
  s->depth = gl->glIsEnabled(F_GL_DEPTH_TEST);
  s->stencil = gl->glIsEnabled(F_GL_STENCIL_TEST);
  s->cull = gl->glIsEnabled(F_GL_CULL_FACE);
  for (int i = 0; i < ATTRIBS; i++) {
    s->attrib_on[i] = 0;
    gl->glGetVertexAttribiv((fGLuint)i, F_GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->attrib_on[i]);
  }
  gl->glBindFramebuffer(F_GL_FRAMEBUFFER, 0);
  gl->glDisable(F_GL_SCISSOR_TEST);
  gl->glDisable(F_GL_DEPTH_TEST);
  gl->glDisable(F_GL_STENCIL_TEST);
  gl->glDisable(F_GL_CULL_FACE);
}

static void set_cap(const FbfGl *gl, fGLenum cap, fGLboolean on) {
  if (on)
    gl->glEnable(cap);
  else
    gl->glDisable(cap);
}

static void restore(const FbfGl *gl, const Saved *s) {
  gl->glColorMask(1, 1, 1, 1);
  set_cap(gl, F_GL_SCISSOR_TEST, s->scissor);
  set_cap(gl, F_GL_BLEND, s->blend);
  set_cap(gl, F_GL_DEPTH_TEST, s->depth);
  set_cap(gl, F_GL_STENCIL_TEST, s->stencil);
  set_cap(gl, F_GL_CULL_FACE, s->cull);
  for (int i = 0; i < ATTRIBS; i++) {
    if (s->attrib_on[i])
      gl->glEnableVertexAttribArray((fGLuint)i);
    else
      gl->glDisableVertexAttribArray((fGLuint)i);
  }
  gl->glBindFramebuffer(F_GL_FRAMEBUFFER, (fGLuint)s->fb);
}

/* A rectangle in pixels (from the top left) as two triangles. */
static void rect(const FbfGl *gl, int w, int h, float x0, float y0, float x1, float y1) {
  const float l = x0 / (float)w * 2.0f - 1.0f, r = x1 / (float)w * 2.0f - 1.0f;
  const float t = 1.0f - y0 / (float)h * 2.0f, b = 1.0f - y1 / (float)h * 2.0f;
  const fGLfloat v[12] = {l, b, r, b, r, t, l, b, r, t, l, t};
  gl->glVertexAttribPointer(0, 2, F_GL_FLOAT, 0, 0, v);
  gl->glDrawArrays(F_GL_TRIANGLES, 0, 6);
}

void fbf_overlay_pause(int w, int h, float t) {
  const FbfGl *gl = fbf_gl();
  if (!gl || !g_ready)
    return;
  Saved s;
  save(gl, &s);
  gl->glViewport(0, 0, w, h);
  gl->glUseProgram(g_prog);
  for (int i = 0; i < ATTRIBS; i++)
    gl->glDisableVertexAttribArray((fGLuint)i);
  gl->glEnableVertexAttribArray(0);
  gl->glEnable(F_GL_BLEND);
  gl->glBlendFunc(F_GL_SRC_ALPHA, F_GL_ONE_MINUS_SRC_ALPHA);

  /* the frozen frame, dimmed */
  gl->glUniform4f(g_u_color, 0.0f, 0.0f, 0.0f, 0.55f);
  rect(gl, w, h, 0.0f, 0.0f, (float)w, (float)h);

  /* the pause sign: two bars, breathing slowly */
  const float u = (float)h / 720.0f;
  const float a = 0.80f + 0.15f * sinf(t * 2.5f);
  const float bw = 34.0f * u, bh = 120.0f * u, gap = 30.0f * u;
  const float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
  gl->glUniform4f(g_u_color, 0.0f, 0.0f, 0.0f, 0.35f * a); /* a soft shadow */
  rect(gl, w, h, cx - gap / 2 - bw + 5 * u, cy - bh / 2 + 5 * u, cx - gap / 2 + 5 * u, cy + bh / 2 + 5 * u);
  rect(gl, w, h, cx + gap / 2 + 5 * u, cy - bh / 2 + 5 * u, cx + gap / 2 + bw + 5 * u, cy + bh / 2 + 5 * u);
  gl->glUniform4f(g_u_color, 1.0f, 1.0f, 1.0f, a);
  rect(gl, w, h, cx - gap / 2 - bw, cy - bh / 2, cx - gap / 2, cy + bh / 2);
  rect(gl, w, h, cx + gap / 2, cy - bh / 2, cx + gap / 2 + bw, cy + bh / 2);

  restore(gl, &s);
}

/* A texture at x, y, iw, ih (pixels, from the top left), filtered, at
 * alpha a; over black (clear) or over the frame. */
static void picture(const FbfGl *gl, unsigned tex, int w, int h, float x, float y, float iw, float ih,
                    float a, int clear) {
  Saved s;
  save(gl, &s);
  gl->glViewport(0, 0, w, h);
  if (clear) {
    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f); /* Theme.Black */
    gl->glClear(F_GL_COLOR_BUFFER_BIT);
  }
  gl->glUseProgram(g_prog_tex);
  gl->glActiveTexture(F_GL_TEXTURE0);
  gl->glBindTexture(F_GL_TEXTURE_2D, (fGLuint)tex);
  gl->glUniform1i(g_u_tex, 0);
  gl->glUniform1f(g_u_alpha, a);
  gl->glEnable(F_GL_BLEND);
  gl->glBlendFunc(F_GL_SRC_ALPHA, F_GL_ONE_MINUS_SRC_ALPHA);
  for (int i = 0; i < ATTRIBS; i++)
    gl->glDisableVertexAttribArray((fGLuint)i);
  gl->glEnableVertexAttribArray(0);
  gl->glEnableVertexAttribArray(1);
  const float l = x / (float)w * 2.0f - 1.0f, r = (x + iw) / (float)w * 2.0f - 1.0f;
  const float t = 1.0f - y / (float)h * 2.0f, b = 1.0f - (y + ih) / (float)h * 2.0f;
  const fGLfloat pos[12] = {l, b, r, b, r, t, l, b, r, t, l, t};
  const fGLfloat uv[12] = {0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0}; /* first row at the top */
  gl->glVertexAttribPointer(0, 2, F_GL_FLOAT, 0, 0, pos);
  gl->glVertexAttribPointer(1, 2, F_GL_FLOAT, 0, 0, uv);
  gl->glDrawArrays(F_GL_TRIANGLES, 0, 6);
  gl->glBindTexture(F_GL_TEXTURE_2D, 0);
  restore(gl, &s);
}

void fbf_overlay_picture(unsigned tex, int w, int h, float x, float y, float iw, float ih) {
  const FbfGl *gl = fbf_gl();
  if (gl && g_ready_tex)
    picture(gl, tex, w, h, x, y, iw, ih, 1.0f, 1);
}

/* The leaderboard's picture: 768x432 like the engine's frame, laid over it
 * where the engine puts its own (fitted to the window, centred), filtered
 * as the engine's atlas is. The texture is sent again only when the
 * picture changed (a key, the arrows' bob): a few times a second at most. */
void fbf_overlay_ui(const struct FbfCanvas *c, int changed, float alpha, int w, int h) {
  static fGLuint tex;
  const FbfGl *gl = fbf_gl();
  if (!gl || !g_ready_tex || !c)
    return;
  if (!tex) {
    gl->glGenTextures(1, &tex);
    gl->glBindTexture(F_GL_TEXTURE_2D, tex);
    gl->glPixelStorei(F_GL_UNPACK_ALIGNMENT, 4);
    gl->glTexImage2D(F_GL_TEXTURE_2D, 0, F_GL_RGBA, c->w, c->h, 0, F_GL_RGBA, F_GL_UNSIGNED_BYTE, c->px);
    gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MIN_FILTER, F_GL_LINEAR);
    gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_MAG_FILTER, F_GL_LINEAR);
    gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_WRAP_S, F_GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(F_GL_TEXTURE_2D, F_GL_TEXTURE_WRAP_T, F_GL_CLAMP_TO_EDGE);
    gl->glBindTexture(F_GL_TEXTURE_2D, 0);
  } else if (changed) {
    gl->glBindTexture(F_GL_TEXTURE_2D, tex);
    gl->glPixelStorei(F_GL_UNPACK_ALIGNMENT, 4);
    gl->glTexSubImage2D(F_GL_TEXTURE_2D, 0, 0, 0, c->w, c->h, F_GL_RGBA, F_GL_UNSIGNED_BYTE, c->px);
    gl->glBindTexture(F_GL_TEXTURE_2D, 0);
  }
  const float sc = fminf((float)w / (float)c->w, (float)h / (float)c->h);
  const float iw = (float)c->w * sc, ih = (float)c->h * sc;
  picture(gl, tex, w, h, ((float)w - iw) / 2, ((float)h - ih) / 2, iw, ih, alpha, 0);
}

void fbf_overlay_opaque(int w, int h) {
  const FbfGl *gl = fbf_gl();
  if (!gl)
    return;
  Saved s;
  save(gl, &s);
  gl->glViewport(0, 0, w, h);
  gl->glColorMask(0, 0, 0, 1);
  gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl->glClear(F_GL_COLOR_BUFFER_BIT);
  restore(gl, &s);
}
