/* dcr_apkfind.c -- where the game's files are, before anything is read.
 *
 * THE FOLDER. The game folder is /switch/flappybirdsfamily_nx, named after
 * the NRO (flappybirdsfamily_nx.nro). Builds up to 202609260029 used
 * /switch/flappybirdsfamily: on a start, whatever is still there -- the APK,
 * config.ini, the saved high score (data/), leaderboard.txt, the unpacked
 * engine -- is moved into the new folder, so nothing is lost. .nro files stay
 * (a sphaira forwarder may point at one); so does anything the new folder
 * already has. Moving is a rename on the SD card: no copying.
 *
 * THE APK. The player's own APK may have any name: every *.apk in the
 * folder is opened, and the one that holds this game -- its engine
 * (lib/armeabi-v7a/libflapfire.so) and its art (res/raw/atlas.png) -- is
 * used. With more than one, the highest version code wins (then the first
 * by name); the others are named in debug.log. Plain C over miniz and the
 * manifest reader: tools/test_setup.py runs it on a PC. MIT.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "dcr_apkfind.h"
#include "dcr_manifest.h"
#include "util.h"

#define MAX_ENTRIES 256

static int ends_with(const char *s, const char *tail) {
  const size_t n = strlen(s), t = strlen(tail);
  return n >= t && !strcasecmp(s + n - t, tail);
}

/* The folder's entries (but . and ..), read before any of them moves. */
static int list_dir(const char *root, char (*names)[256], int cap) {
  DIR *d = opendir(root);
  if (!d)
    return -1;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) && n < cap) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    snprintf(names[n++], 256, "%s", e->d_name);
  }
  closedir(d);
  return n;
}

static int cmp_names(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

int dcr_move_old_folder(const char *old_root, const char *new_root) {
  static char names[MAX_ENTRIES][256];
  const int n = list_dir(old_root, names, MAX_ENTRIES);
  if (n <= 0)
    return 0;
  int moved = 0, kept = 0, nros = 0;
  for (int i = 0; i < n; i++) {
    char src[600], dst[600];
    struct stat st;
    snprintf(src, sizeof src, "%.300s/%.255s", old_root, names[i]);
    snprintf(dst, sizeof dst, "%.300s/%.255s", new_root, names[i]);
    if (ends_with(names[i], ".nro")) {
      nros++;
      continue;
    }
    if (stat(dst, &st) == 0) {
      kept++;
      debugPrintf("[move] %s stays in %s: %s already has one\n", names[i], old_root, new_root);
      continue;
    }
    if (rename(src, dst) == 0) {
      moved++;
      debugPrintf("[move] %s -> %s\n", src, dst);
    } else {
      kept++;
      debugPrintf("[move] could not move %s to %s\n", src, dst);
    }
  }
  if (moved)
    debugPrintf("[move] moved %d item%s from the old game folder %s to %s%s\n", moved, moved == 1 ? "" : "s",
                old_root, new_root,
                nros ? " (its .nro stays there: put new NROs in the new folder)" : "");
  if (!nros && !kept && rmdir(old_root) == 0)
    debugPrintf("[move] %s was empty and is removed\n", old_root);
  return moved;
}

/* Does this zip hold the game: its engine and its art? */
static int holds_game(const char *path, const char *lib) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_file(&z, path, 0))
    return 0;
  char want[160];
  snprintf(want, sizeof want, "lib/armeabi-v7a/%s", lib);
  const int ok = mz_zip_reader_locate_file(&z, want, NULL, 0) >= 0 &&
                 mz_zip_reader_locate_file(&z, "res/raw/atlas.png", NULL, 0) >= 0;
  mz_zip_reader_end(&z);
  return ok;
}

int dcr_find_apk(const char *root, const char *lib, char *out, size_t cap, char *why, size_t whycap) {
  static char names[MAX_ENTRIES][256];
  const int n = list_dir(root, names, MAX_ENTRIES);
  if (n > 0)
    qsort(names, (size_t)n, sizeof names[0], cmp_names);
  int apks = 0, found = 0, best_code = 0;
  char other[160] = "";
  for (int i = 0; i < n; i++) {
    char path[600];
    struct stat st;
    snprintf(path, sizeof path, "%.300s/%.255s", root, names[i]);
    if (!ends_with(names[i], ".apk") || stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
      continue;
    apks++;
    if (!holds_game(path, lib)) {
      debugPrintf("[apk] %s: not this game (no lib/armeabi-v7a/%s or res/raw/atlas.png)\n", names[i], lib);
      if (!other[0])
        snprintf(other, sizeof other, "%s", names[i]);
      continue;
    }
    const int code = dcr_manifest_load(path) == 0 ? dcr_manifest_version_code() : 0;
    debugPrintf("[apk] %s: the game (version code %d)\n", names[i], code);
    if (!found || code > best_code) {
      if (found)
        debugPrintf("[apk] %s is newer than %s: using it\n", names[i], out);
      snprintf(out, cap, "%s", path);
      best_code = code;
      found = 1;
    } else {
      debugPrintf("[apk] %s: not used (%s is as new or newer)\n", names[i], out);
    }
  }
  if (found)
    return 0;
  if (!apks)
    snprintf(why, whycap, "There is no APK in %s.", root);
  else
    snprintf(why, whycap, "%s%s in %s %s not this game (no lib/armeabi-v7a/%s).", other,
             apks > 1 ? " and the other APKs" : "", root, apks > 1 ? "are" : "is", lib);
  return -1;
}
