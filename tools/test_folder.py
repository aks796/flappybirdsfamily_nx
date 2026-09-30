#!/usr/bin/env python3
"""Host test for source/dcr_apkfind.c: the game folder moved from its old
name, and the player's APK found whatever it is called.

    python3 tools/test_folder.py [the game's APK]

Builds dcr_apkfind.c with the manifest reader and tools/host's miniz over
zlib (ASan/UBSan) and runs it on folders made here:

  * the old folder's APK, config.ini, data/ and leaderboard.txt move into the
    new one; its .nro files stay, and so does anything the new folder already
    has (its debug.log); an old folder left empty is removed;
  * an APK is found by what it holds (the engine and the atlas), not by name:
    an odd name and an upper-case .APK work, a zip without the game is passed
    over, no APK or only other APKs give a reason; with several, the highest
    version code wins, then the first by name. Given the real APK, its
    manifest's version code is what decides.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
SRC = os.path.join(TOP, 'source')

HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcr_apkfind.h"
void debugPrintf(const char *fmt, ...) {
  if (getenv("FOLDER_LOG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
}
int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "move")) {
    printf("%d\n", dcr_move_old_folder(argv[2], argv[3]));
    return 0;
  }
  if (argc == 3 && !strcmp(argv[1], "find")) {
    char out[600], why[300];
    if (dcr_find_apk(argv[2], "libflapfire.so", out, sizeof out, why, sizeof why) == 0)
      printf("found %s\n", strrchr(out, '/') + 1);
    else
      printf("none: %s\n", why);
    return 0;
  }
  return 2;
}
'''


def game_zip(path, manifest=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('lib/armeabi-v7a/libflapfire.so', b'\x7fELF not really')
        z.writestr('res/raw/atlas.png', b'\x89PNG not really')
        if manifest:
            z.writestr('AndroidManifest.xml', manifest)


def other_zip(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('lib/armeabi-v7a/libsomethingelse.so', b'x')
        z.writestr('res/raw/atlas.png', b'x')


def write(path, data=b'x'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)


def main():
    real = sys.argv[1] if len(sys.argv) > 1 else None
    fails = 0

    def check(what, got, want):
        nonlocal fails
        ok = got == want
        fails += not ok
        if not ok:
            print('FAIL %s: got %r, want %r' % (what, got, want))

    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(t, 'h.c')
        open(src, 'w').write(HARNESS)
        exe = os.path.join(t, 'h')
        subprocess.check_call(['cc', '-O1', '-g', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                               '-fno-sanitize-recover=undefined', '-I', os.path.join(HERE, 'host'), '-I', SRC,
                               src, os.path.join(SRC, 'dcr_apkfind.c'), os.path.join(SRC, 'dcr_manifest.c'),
                               os.path.join(HERE, 'host', 'miniz_host.c'), '-lz', '-o', exe])

        def run(*a):
            r = subprocess.run([exe] + list(a), capture_output=True, text=True)
            if r.returncode:
                print(r.stdout, r.stderr)
                sys.exit(1)
            return r.stdout.strip()

        # ---- the move
        old, new = os.path.join(t, 'switch', 'flappybirdsfamily'), os.path.join(t, 'switch', 'flappybirdsfamily_nx')
        game_zip(os.path.join(old, 'game.apk'))
        write(os.path.join(old, 'config.ini'), b'[sound]\nvolume = 40\n')
        write(os.path.join(old, 'data', 'shared_prefs', 'flapfire.xml'), b'<map/>')
        write(os.path.join(old, 'leaderboard.txt'), b'42 AKS\n')
        write(os.path.join(old, 'FlappyBirdsFamily.nro'))
        write(os.path.join(old, 'debug.log'), b'old log')
        write(os.path.join(new, 'debug.log'), b'new log')
        check('entries moved', run('move', old, new), '4')
        check('new folder', sorted(os.listdir(new)), ['config.ini', 'data', 'debug.log', 'game.apk', 'leaderboard.txt'])
        check('the saves came along', open(os.path.join(new, 'data', 'shared_prefs', 'flapfire.xml'), 'rb').read(), b'<map/>')
        check('what the new folder had is kept', open(os.path.join(new, 'debug.log'), 'rb').read(), b'new log')
        check('old folder keeps its .nro and what the new one had', sorted(os.listdir(old)),
              ['FlappyBirdsFamily.nro', 'debug.log'])
        check('a second start moves nothing', run('move', old, new), '0')
        check('no old folder: nothing', run('move', os.path.join(t, 'nowhere'), new), '0')
        shutil.rmtree(old)
        write(os.path.join(old, 'crash.log'))
        check('the last entry', run('move', old, new), '1')
        check('an emptied old folder is removed', os.path.exists(old), False)

        # ---- the APK
        root = os.path.join(t, 'root')
        os.makedirs(root)
        check('no APK', run('find', root), 'none: There is no APK in %s.' % root)
        other_zip(os.path.join(root, 'Some Other Game.apk'))
        check('only another game', run('find', root),
              'none: Some Other Game.apk in %s is not this game (no lib/armeabi-v7a/libflapfire.so).' % root)
        write(os.path.join(root, 'broken.apk'), b'not a zip')
        check('another game and a broken file', run('find', root).startswith('none: broken.apk and the other APKs in'), True)
        game_zip(os.path.join(root, 'Flappy Birds Family v1.0.4 (Amazon).APK'))
        check('any name, any case', run('find', root), 'found Flappy Birds Family v1.0.4 (Amazon).APK')
        os.makedirs(os.path.join(root, 'folder.apk'))
        game_zip(os.path.join(root, 'b.apk'))
        check('two alike: the first by name', run('find', root), 'found b.apk')
        if real:
            with zipfile.ZipFile(real) as z:
                manifest = z.read('AndroidManifest.xml')
            game_zip(os.path.join(root, 'zz_real.apk'), manifest)
            check('the higher version code wins over the name order', run('find', root), 'found zz_real.apk')
    if fails:
        sys.exit(1)
    print('OK: the old game folder moves over (not its .nro, not over what is there); the APK is found by '
          'what it holds, whatever its name%s' % (', and by version code' if real else ''))


if __name__ == '__main__':
    main()
