#!/usr/bin/env python3
"""Host test for the port's game layer (the parts with no libnx in them).

Builds source/fbf_assets.c (with tools/host's miniz-over-zlib), fbf_prefs.c
and fbf_gate.c natively, with the address and undefined-behaviour sanitizers,
and checks them against independent references and the user's own APK:

  1. assets: atlas.png decoded by stb_image and put through Android's
     premultiply/unpremultiply round trip equals Pillow's decode put through
     the same round trip written again here, pixel for pixel; atlas_text.txt
     is the APK's, and every sprite in it lies inside the texture; each sound
     decodes (stb_vorbis) to exactly the number of frames its Ogg stream's
     last granule position gives, with the channels and rate of its header;
  2. preferences: the Android SharedPreferences file written and read back,
     and files as Android writes them (other keys, other order, quotes);
  3. the key rule: GameActivity's one-key-per-device behaviour;
  4. the splash picture (res/drawable/splash.png) against Pillow;
  5. the manifest reader: the package getPackageName() answers the engine's
     check with must be com.dotgears.flapfire.

    python3 tools/test_game.py <the game's APK> [<another APK> ...]
"""
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "fbf.h"
#include "dcr_manifest.h"

void debugPrintf(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int assets(const char *apk, const char *out_rgba, const char *out_txt) {
  FbfAssets a;
  char err[256];
  if (fbf_assets_load(apk, &a, err, sizeof err)) { printf("ERROR %s\n", err); return 1; }
  printf("atlas %d %d %u\n", a.w, a.h, (unsigned)a.atlas_len);
  for (int i = 0; i < FBF_SFX_COUNT; i++)
    printf("sfx %s %d %d %d\n", fbf_sfx_names[i], a.sfx[i].frames, a.sfx[i].channels, a.sfx[i].rate);
  FILE *f = fopen(out_rgba, "wb"); fwrite(a.rgba, 4, (size_t)a.w * a.h, f); fclose(f);
  f = fopen(out_txt, "wb"); fwrite(a.atlas_text, 1, a.atlas_len, f); fclose(f);
  fbf_assets_free(&a);
  return 0;
}

static void prefs(void) {
  FbfPrefs p = {42, 7, 1}, q = {0};
  char buf[512];
  CHECK(fbf_prefs_format(&p, buf, sizeof buf) > 0);
  CHECK(fbf_prefs_parse(buf, &q) == 3 && q.score == 42 && q.playcount == 7 && q.rated == 1);
  /* as Android writes it: other keys, any order, single quotes, a negative */
  const char *android =
      "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n"
      "    <boolean name=\"sound\" value=\"true\" />\n"
      "    <int name='rated' value='-1' />\n"
      "    <string name=\"score\">99</string>\n"
      "    <int name=\"playcount\" value=\"123\" />\n"
      "    <int name=\"score\" value=\"31\" />\n"
      "    <int name=\"highscore\" value=\"500\" />\n"
      "</map>\n";
  FbfPrefs r = {0};
  CHECK(fbf_prefs_parse(android, &r) == 3);
  CHECK(r.score == 31 && r.playcount == 123 && r.rated == -1);
  FbfPrefs e = {5, 5, 5};
  CHECK(fbf_prefs_parse("", &e) == 0 && e.score == 5); /* nothing found: untouched */
  CHECK(fbf_prefs_parse("<int name=\"score\" value=\"12x\" />", &e) == 0 && e.score == 5);
  CHECK(fbf_prefs_parse("<int", &e) == 0);
  CHECK(fbf_prefs_parse("<int name=\"score\" value=\"", &e) == 0);
  printf("prefs done\n");
}

static void gate(void) {
  FbfKeyGate g;
  memset(&g, 0, sizeof g);
  CHECK(fbf_gate_down(&g, 1) == 1);  /* A down on device 1: goes on */
  CHECK(fbf_gate_down(&g, 1) == 0);  /* B down while A is held: dropped */
  CHECK(fbf_gate_down(&g, 2) == 1);  /* device 2 is its own */
  fbf_gate_up(&g, 1);                /* B up (any key of the device) */
  CHECK(fbf_gate_down(&g, 1) == 1);  /* the next key goes on again */
  fbf_gate_up(&g, 1);
  fbf_gate_up(&g, 1);                /* an extra up is harmless */
  CHECK(fbf_gate_down(&g, 1) == 1);
  CHECK(fbf_gate_down(&g, -1) == 1 && fbf_gate_down(&g, 99) == 1); /* out of range: not gated */
  printf("gate done\n");
}

int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "assets"))
    return assets(argv[2], argv[3], argv[3 + 0]) ? 1 : 0;
  if (argc == 5 && !strcmp(argv[1], "assets"))
    return assets(argv[2], argv[3], argv[4]);
  if (argc == 4 && !strcmp(argv[1], "splash")) {
    int w = 0, h = 0;
    uint8_t *px = fbf_assets_splash(argv[2], &w, &h);
    if (!px) return 1;
    printf("%d %d\n", w, h);
    FILE *f = fopen(argv[3], "wb"); fwrite(px, 4, (size_t)w * h, f); fclose(f);
    fbf_assets_free_pixels(px);
    return 0;
  }
  if (argc == 3 && !strcmp(argv[1], "manifest")) {
    if (dcr_manifest_load(argv[2])) return 1;
    printf("%s %s %d\n", dcr_manifest_package(), dcr_manifest_version_name(), dcr_manifest_version_code());
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "units")) {
    prefs();
    gate();
    printf("%d failure(s)\n", fails);
    return fails != 0;
  }
  return 2;
}
'''


def ogg_info(data):
    """(channels, rate, total frames) from the identification header and the
    last page's granule position."""
    i = data.index(b'\x01vorbis')
    ch = data[i + 11]
    rate, = struct.unpack_from('<I', data, i + 12)
    last = None
    p = 0
    while True:
        p = data.find(b'OggS', p)
        if p < 0:
            break
        granule, = struct.unpack_from('<q', data, p + 6)
        nseg = data[p + 26]
        body = sum(data[p + 27:p + 27 + nseg])
        if granule >= 0:
            last = granule
        p += 27 + nseg + body
    return ch, rate, last


def android_roundtrip(px):
    """BitmapFactory (premultiplied, Skia's SkMulDiv255Round) then
    Bitmap.getPixels (unpremultiplied, rounded), on RGBA bytes."""
    out = bytearray(px)
    for i in range(0, len(out), 4):
        a = out[i + 3]
        if a == 255:
            continue
        if a == 0:
            out[i] = out[i + 1] = out[i + 2] = 0
            continue
        for c in range(3):
            prod = out[i + c] * a + 128
            pm = (prod + (prod >> 8)) >> 8
            out[i + c] = min(255, (pm * 255 + a // 2) // a)
    return bytes(out)


def main():
    apks = sys.argv[1:]
    if not apks:
        sys.exit(__doc__)
    from PIL import Image
    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(t, 'h.c')
        open(src, 'w').write(HARNESS)
        exe = os.path.join(t, 'h')
        subprocess.check_call(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                               '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                               '-I', os.path.join(HERE, 'host'), '-I', SRC, '-I', RT, '-DPORT_PAYLOAD_NAME="fbf_nx"', src,
                               os.path.join(SRC, 'fbf_assets.c'), os.path.join(SRC, 'fbf_prefs.c'),
                               os.path.join(SRC, 'fbf_gate.c'), os.path.join(RT, 'dcr_manifest.c'),
                               os.path.join(HERE, 'host', 'miniz_host.c'),
                               '-lz', '-o', exe])
        out = subprocess.check_output([exe, 'units']).decode()
        print(out.strip())
        assert '0 failure(s)' in out, 'unit checks failed'

        for apk in apks:
            print('==', os.path.basename(apk))
            pkg, vname, vcode = subprocess.check_output([exe, 'manifest', apk]).decode().split()
            assert pkg == 'com.dotgears.flapfire', 'package ' + pkg
            print('OK: manifest: %s %s (version code %s) -- the package the engine checks for'
                  % (pkg, vname, vcode))
            sp = os.path.join(t, 'splash.rgba')
            sw, sh = map(int, subprocess.check_output([exe, 'splash', apk, sp]).decode().split())
            simg = Image.open(zipfile.ZipFile(apk).open('res/drawable/splash.png')).convert('RGBA')
            assert (sw, sh) == simg.size and open(sp, 'rb').read() == simg.tobytes(), 'splash'
            print('OK: splash %dx%d decoded pixel-exact (shown at %dx%d on 720p, %dx%d on 1080p)'
                  % (sw, sh, round(sw * 720 / 540), round(sh * 720 / 540), sw * 2, sh * 2))
            rgba, txt = os.path.join(t, 'atlas.rgba'), os.path.join(t, 'atlas.txt')
            res = subprocess.check_output([exe, 'assets', apk, rgba, txt]).decode().split('\n')
            z = zipfile.ZipFile(apk)
            head = res[0].split()
            w, h, tlen = int(head[1]), int(head[2]), int(head[3])

            img = Image.open(z.open('res/raw/atlas.png')).convert('RGBA')
            assert (w, h) == img.size, 'atlas size %dx%d, Pillow %dx%d' % (w, h, *img.size)
            want = android_roundtrip(img.tobytes())
            got = open(rgba, 'rb').read()
            assert got == want, 'atlas pixels differ from Pillow + the Android round trip'
            opaque = sum(1 for i in range(3, len(want), 4) if want[i] == 255)
            print('OK: atlas %dx%d decoded pixel-exact (%d%% opaque texels; transparent ones black, '
                  'as getPixels gives them)' % (w, h, 100 * opaque // (w * h)))

            raw = z.read('res/raw/atlas_text.txt')
            assert open(txt, 'rb').read() == raw and tlen == len(raw), 'atlas_text.txt'
            n = 0
            for line in raw.decode().splitlines():
                f = line.split()
                if len(f) != 7:
                    continue
                name, sw, sh, u, v, uw, vh = f[0], int(f[1]), int(f[2]), *map(float, f[3:])
                assert 0 <= u and 0 <= v and u + uw <= 1.0001 and v + vh <= 1.0001, name
                assert abs(uw * w - sw) < 1.01 and abs(vh * h - sh) < 1.01, name
                n += 1
            print('OK: sprite table %d bytes, %d sprites, all inside the texture' % (tlen, n))

            for line in res[1:]:
                f = line.split()
                if not f or f[0] != 'sfx':
                    continue
                name, frames, ch, rate = f[1], int(f[2]), int(f[3]), int(f[4])
                if 'res/raw/%s.ogg' % name not in z.namelist():
                    assert frames == 0, name + ' decoded from nothing'
                    print('OK: %-14s not in this APK: stays silent' % name)
                    continue
                wch, wrate, wframes = ogg_info(z.read('res/raw/%s.ogg' % name))
                assert (frames, ch, rate) == (wframes, wch, wrate), \
                    '%s: decoded %d frames %d ch %d Hz, the stream says %d / %d / %d' % (
                        name, frames, ch, rate, wframes, wch, wrate)
                print('OK: %-14s %6d frames (%.2f s), %d ch, %d Hz' % (name, frames, frames / rate, ch, rate))


if __name__ == '__main__':
    main()
