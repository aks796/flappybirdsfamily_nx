/* fbf_gl.h -- the GL ES 2 calls the port itself makes (the atlas texture,
 * the pause overlay, the leaderboard's picture), looked up through the GL layer (dcr_gl_lookup) like
 * the engine's own, so the same code runs on Mesa and on the null renderer.
 * No GL headers: the build without Mesa has none. MIT. */
#ifndef FBF_GL_H
#define FBF_GL_H
#include <stdint.h>

typedef uint32_t fGLenum, fGLuint, fGLbitfield;
typedef int32_t fGLint, fGLsizei;
typedef uint8_t fGLboolean;
typedef float fGLfloat;

#define F_GL_TEXTURE_2D 0x0DE1
#define F_GL_RGBA 0x1908
#define F_GL_UNSIGNED_BYTE 0x1401
#define F_GL_FLOAT 0x1406
#define F_GL_LINEAR 0x2601
#define F_GL_TEXTURE_MAG_FILTER 0x2800
#define F_GL_TEXTURE_MIN_FILTER 0x2801
#define F_GL_UNPACK_ALIGNMENT 0x0CF5
#define F_GL_COLOR_BUFFER_BIT 0x4000
#define F_GL_BLEND 0x0BE2
#define F_GL_SCISSOR_TEST 0x0C11
#define F_GL_DEPTH_TEST 0x0B71
#define F_GL_STENCIL_TEST 0x0B90
#define F_GL_CULL_FACE 0x0B44
#define F_GL_SRC_ALPHA 0x0302
#define F_GL_ONE_MINUS_SRC_ALPHA 0x0303
#define F_GL_TRIANGLES 0x0004
#define F_GL_FRAMEBUFFER 0x8D40
#define F_GL_FRAMEBUFFER_BINDING 0x8CA6
#define F_GL_VERTEX_SHADER 0x8B31
#define F_GL_FRAGMENT_SHADER 0x8B30
#define F_GL_COMPILE_STATUS 0x8B81
#define F_GL_LINK_STATUS 0x8B82
#define F_GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define F_GL_NO_ERROR 0
#define F_GL_TEXTURE0 0x84C0
#define F_GL_TEXTURE_WRAP_S 0x2802
#define F_GL_TEXTURE_WRAP_T 0x2803
#define F_GL_CLAMP_TO_EDGE 0x812F

#define FBF_GL_FUNCS(X)                                                                          \
  X(void, glGenTextures, (fGLsizei n, fGLuint *t))                                               \
  X(void, glBindTexture, (fGLenum target, fGLuint t))                                            \
  X(void, glTexImage2D, (fGLenum target, fGLint level, fGLint ifmt, fGLsizei w, fGLsizei h,      \
                         fGLint border, fGLenum fmt, fGLenum type, const void *px))              \
  X(void, glTexParameteri, (fGLenum target, fGLenum pname, fGLint v))                            \
  X(void, glPixelStorei, (fGLenum pname, fGLint v))                                              \
  X(fGLenum, glGetError, (void))                                                                 \
  X(void, glGetIntegerv, (fGLenum pname, fGLint *v))                                             \
  X(fGLboolean, glIsEnabled, (fGLenum cap))                                                      \
  X(void, glEnable, (fGLenum cap))                                                               \
  X(void, glDisable, (fGLenum cap))                                                              \
  X(void, glBlendFunc, (fGLenum s, fGLenum d))                                                   \
  X(void, glColorMask, (fGLboolean r, fGLboolean g, fGLboolean b, fGLboolean a))                 \
  X(void, glClearColor, (fGLfloat r, fGLfloat g, fGLfloat b, fGLfloat a))                        \
  X(void, glClear, (fGLbitfield mask))                                                           \
  X(void, glViewport, (fGLint x, fGLint y, fGLsizei w, fGLsizei h))                              \
  X(void, glBindFramebuffer, (fGLenum target, fGLuint fb))                                       \
  X(fGLuint, glCreateShader, (fGLenum type))                                                     \
  X(void, glShaderSource, (fGLuint s, fGLsizei n, const char *const *src, const fGLint *len))    \
  X(void, glCompileShader, (fGLuint s))                                                          \
  X(void, glGetShaderiv, (fGLuint s, fGLenum pname, fGLint *v))                                  \
  X(fGLuint, glCreateProgram, (void))                                                            \
  X(void, glAttachShader, (fGLuint p, fGLuint s))                                                \
  X(void, glBindAttribLocation, (fGLuint p, fGLuint idx, const char *name))                      \
  X(void, glLinkProgram, (fGLuint p))                                                            \
  X(void, glGetProgramiv, (fGLuint p, fGLenum pname, fGLint *v))                                 \
  X(void, glUseProgram, (fGLuint p))                                                             \
  X(fGLint, glGetUniformLocation, (fGLuint p, const char *name))                                 \
  X(void, glUniform4f, (fGLint loc, fGLfloat a, fGLfloat b, fGLfloat c, fGLfloat d))             \
  X(void, glGetVertexAttribiv, (fGLuint idx, fGLenum pname, fGLint *v))                          \
  X(void, glEnableVertexAttribArray, (fGLuint idx))                                              \
  X(void, glDisableVertexAttribArray, (fGLuint idx))                                             \
  X(void, glVertexAttribPointer, (fGLuint idx, fGLint size, fGLenum type, fGLboolean norm,       \
                                  fGLsizei stride, const void *ptr))                             \
  X(void, glDrawArrays, (fGLenum mode, fGLint first, fGLsizei count))                            \
  X(void, glUniform1i, (fGLint loc, fGLint v))                                                   \
  X(void, glUniform1f, (fGLint loc, fGLfloat v))                                                 \
  X(void, glTexSubImage2D, (fGLenum target, fGLint level, fGLint x, fGLint y, fGLsizei w,        \
                            fGLsizei h, fGLenum fmt, fGLenum type, const void *px))              \
  X(void, glActiveTexture, (fGLenum unit))                                                       \
  X(void, glDeleteTextures, (fGLsizei n, const fGLuint *t))

typedef struct {
#define FBF_GL_FIELD(ret, name, args) ret (*name) args;
  FBF_GL_FUNCS(FBF_GL_FIELD)
#undef FBF_GL_FIELD
} FbfGl;

/* Resolved once (fbf_overlay.c); NULL entries were not found. Returns the
 * table, or NULL if any is missing. */
const FbfGl *fbf_gl(void);

#endif
