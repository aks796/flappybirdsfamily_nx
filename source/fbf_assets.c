/* fbf_assets.c -- what the Java reads out of the APK's res/raw, read here.
 *
 *   atlas.png       com.dotgears.a: BitmapFactory.decodeStream, getPixels
 *                   (ARGB, unpremultiplied), repacked as RGBA bytes and
 *                   uploaded with glTexImage2D(GL_RGBA, GL_UNSIGNED_BYTE):
 *                   the texture setAtlas hands the engine. One 2048x2048
 *                   picture with all of the game's art.
 *   atlas_text.txt  read whole into a String: the sprite table setAtlas
 *                   parses (name, size, texture rectangle per line).
 *   sfx_*.ogg       the SoundPool's six sounds (Vorbis, 44.1 kHz).
 *   drawable/splash.png  SplashScreen's picture (the dotGears logo).
 *
 * Nothing is unpacked to the SD card: the user's APK is read in place
 * (miniz), the PNG decoded with stb_image, the sounds with stb_vorbis (both
 * public domain, vendored). Plain C, no libnx: tools/host tests it against
 * the user's own APK. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "fbf.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wshadow"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.h"
#pragma GCC diagnostic pop

#define RAW "res/raw/"

const char *const fbf_sfx_names[FBF_SFX_COUNT] = {
    [FBF_SFX_DIE] = "sfx_die",
    [FBF_SFX_HIT] = "sfx_hit",
    [FBF_SFX_POINT] = "sfx_point",
    [FBF_SFX_SWOOSHING] = "sfx_swooshing",
    [FBF_SFX_WING] = "sfx_wing",
    [FBF_SFX_BUTTON] = "sfx_button",
};

static void *extract(mz_zip_archive *zip, const char *name, size_t *len) {
  int i = mz_zip_reader_locate_file(zip, name, NULL, 0);
  *len = 0;
  if (i < 0)
    return NULL;
  return mz_zip_reader_extract_to_heap(zip, (mz_uint)i, len, 0);
}

/* Skia's SkMulDiv255Round: the premultiply BitmapFactory does. */
static inline unsigned mul_div_255_round(unsigned a, unsigned b) {
  unsigned prod = a * b + 128;
  return (prod + (prod >> 8)) >> 8;
}

void fbf_android_bitmap_roundtrip(uint8_t *px, size_t pixels) {
  for (size_t i = 0; i < pixels; i++, px += 4) {
    const unsigned a = px[3];
    if (a == 255)
      continue;
    if (a == 0) {
      px[0] = px[1] = px[2] = 0;
      continue;
    }
    for (int c = 0; c < 3; c++) {
      unsigned v = (mul_div_255_round(px[c], a) * 255 + a / 2) / a;
      px[c] = (uint8_t)(v > 255 ? 255 : v);
    }
  }
}

static void say(char *err, size_t cap, const char *fmt, const char *arg) {
  if (err && cap)
    snprintf(err, cap, fmt, arg);
}

int fbf_assets_load(const char *apk, FbfAssets *a, char *err, size_t errcap) {
  memset(a, 0, sizeof *a);
  if (err && errcap)
    err[0] = 0;
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, apk, 0)) {
    say(err, errcap, "%s cannot be read as an APK (zip).", apk);
    return -1;
  }
  int rc = -1;
  size_t len = 0;

  /* the texture atlas */
  void *png = extract(&zip, RAW "atlas.png", &len);
  if (!png) {
    say(err, errcap, "%s has no res/raw/atlas.png: is it Flappy Birds Family?", apk);
    goto out;
  }
  int n = 0;
  a->rgba = stbi_load_from_memory(png, (int)len, &a->w, &a->h, &n, 4);
  mz_free(png);
  if (!a->rgba) {
    say(err, errcap, "res/raw/atlas.png could not be decoded (%s).", stbi_failure_reason());
    goto out;
  }
  fbf_android_bitmap_roundtrip(a->rgba, (size_t)a->w * (size_t)a->h);

  /* the sprite table */
  char *txt = extract(&zip, RAW "atlas_text.txt", &len);
  if (!txt) {
    say(err, errcap, "%s has no res/raw/atlas_text.txt: is it Flappy Birds Family?", apk);
    goto out;
  }
  a->atlas_text = malloc(len + 1);
  if (!a->atlas_text) {
    mz_free(txt);
    say(err, errcap, "out of memory reading %s", "atlas_text.txt");
    goto out;
  }
  memcpy(a->atlas_text, txt, len);
  a->atlas_text[len] = 0;
  a->atlas_len = strlen(a->atlas_text); /* String.length(): the file is ASCII */
  mz_free(txt);

  /* the sounds: a missing or broken one stays silent, as SoundPool.load
   * failing would leave it */
  for (int i = 0; i < FBF_SFX_COUNT; i++) {
    char name[64];
    snprintf(name, sizeof name, RAW "%s.ogg", fbf_sfx_names[i]);
    void *ogg = extract(&zip, name, &len);
    if (!ogg)
      continue;
    int ch = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(ogg, (int)len, &ch, &rate, &pcm);
    mz_free(ogg);
    if (frames <= 0 || !pcm || ch < 1 || ch > 2 || rate < 8000) {
      free(pcm);
      continue;
    }
    a->sfx[i].pcm = pcm;
    a->sfx[i].frames = frames;
    a->sfx[i].channels = ch;
    a->sfx[i].rate = rate;
  }
  rc = 0;
out:
  mz_zip_reader_end(&zip);
  if (rc)
    fbf_assets_free(a);
  return rc;
}

uint8_t *fbf_assets_splash(const char *apk, int *w, int *h) {
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, apk, 0))
    return NULL;
  size_t len = 0;
  void *png = extract(&zip, "res/drawable/splash.png", &len);
  mz_zip_reader_end(&zip);
  if (!png)
    return NULL;
  int n = 0;
  uint8_t *rgba = stbi_load_from_memory(png, (int)len, w, h, &n, 4);
  mz_free(png);
  return rgba;
}

void fbf_assets_free_pixels(uint8_t *rgba) {
  if (rgba)
    stbi_image_free(rgba);
}

void fbf_assets_free(FbfAssets *a) {
  if (a->rgba)
    stbi_image_free(a->rgba);
  a->rgba = NULL;
  free(a->atlas_text);
  a->atlas_text = NULL;
  for (int i = 0; i < FBF_SFX_COUNT; i++) {
    free(a->sfx[i].pcm);
    a->sfx[i].pcm = NULL;
  }
}
