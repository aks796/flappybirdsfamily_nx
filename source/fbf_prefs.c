/* fbf_prefs.c -- GameActivity's SharedPreferences "flapfire".
 *
 * The Java keeps three ints there: score (the high score, handed to the
 * engine by setHighScore at start and saved again whenever the engine reports
 * a better one, event 9), playcount (+1 at every start) and rated (the rate
 * prompt's flag; nothing here sets it). It saves with edit().clear() + three
 * putInt + commit(). The file is Android's own format, at the place the path
 * mapping gives /data/data/com.dotgears.flapfire/shared_prefs/flapfire.xml,
 * so a save copied off a phone works as it is:
 *
 *   <?xml version='1.0' encoding='utf-8' standalone='yes' ?>
 *   <map>
 *       <int name="score" value="12" />
 *       ...
 *   </map>
 *
 * Written whole to flapfire.xml.part, then the old file removed and the new
 * one renamed into place (the SD card's rename does not replace); a start
 * that finds no flapfire.xml reads the finished .part, so a power cut never
 * loses the score. The parser and formatter are plain C with no
 * libnx (tools/host tests them). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbf.h"

#ifdef __SWITCH__
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"
const char *dcr_game_root(void); /* main.c */
#endif

#define PREFS_DIR "data/shared_prefs"
#define PREFS_FILE "flapfire.xml"

/* The value of attr="..." inside the tag [tag, end), copied to out. */
static int attr(const char *tag, const char *end, const char *name, char *out, size_t cap) {
  size_t n = strlen(name);
  for (const char *p = tag; p + n + 2 < end; p++) {
    if (strncmp(p, name, n) || p[n] != '=' || (p[n + 1] != '"' && p[n + 1] != '\''))
      continue;
    if (p > tag && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\n' && p[-1] != '\r')
      continue; /* part of a longer attribute name */
    const char q = p[n + 1];
    const char *v = p + n + 2, *e = v;
    while (e < end && *e != q)
      e++;
    if (e >= end)
      return 0;
    size_t len = (size_t)(e - v);
    if (len >= cap)
      len = cap - 1;
    memcpy(out, v, len);
    out[len] = 0;
    return 1;
  }
  return 0;
}

int fbf_prefs_parse(const char *xml, FbfPrefs *p) {
  int found = 0;
  for (const char *t = strstr(xml, "<int"); t; t = strstr(t + 4, "<int")) {
    if (t[4] != ' ' && t[4] != '\t' && t[4] != '\n' && t[4] != '\r')
      continue;
    const char *end = strchr(t, '>');
    if (!end)
      break;
    char name[32], value[32];
    if (!attr(t, end, "name", name, sizeof name) || !attr(t, end, "value", value, sizeof value))
      continue;
    char *stop = NULL;
    long v = strtol(value, &stop, 10);
    if (stop == value || *stop)
      continue;
    if (!strcmp(name, "score"))
      p->score = (int)v, found++;
    else if (!strcmp(name, "playcount"))
      p->playcount = (int)v, found++;
    else if (!strcmp(name, "rated"))
      p->rated = (int)v, found++;
  }
  return found;
}

int fbf_prefs_format(const FbfPrefs *p, char *out, size_t cap) {
  return snprintf(out, cap,
                  "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n"
                  "<map>\n"
                  "    <int name=\"score\" value=\"%d\" />\n"
                  "    <int name=\"playcount\" value=\"%d\" />\n"
                  "    <int name=\"rated\" value=\"%d\" />\n"
                  "</map>\n",
                  p->score, p->playcount, p->rated);
}

#ifdef __SWITCH__
void fbf_prefs_path(char *out, size_t cap) {
  snprintf(out, cap, "%s/" PREFS_DIR "/" PREFS_FILE, dcr_game_root());
}

void fbf_prefs_load(FbfPrefs *p) {
  memset(p, 0, sizeof *p);
  char path[320];
  fbf_prefs_path(path, sizeof path);
  FILE *f = fopen(path, "rb");
  if (!f) {
    /* A save stopped between putting the old file away and the new one in
     * place (power, a crash): the .part is complete, since the old file is
     * only removed after it was written and closed. */
    char part[330];
    snprintf(part, sizeof part, "%s.part", path);
    f = fopen(part, "rb");
    if (f)
      debugPrintf("[prefs] %s missing: reading the finished %s\n", path, part);
  }
  if (!f) {
    debugPrintf("[prefs] %s: none yet (a first start)\n", path);
    return;
  }
  char buf[4096];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  int found = fbf_prefs_parse(buf, p);
  debugPrintf("[prefs] high score %d, played %d times, rated %d (%d of 3 keys in the file)\n",
              p->score, p->playcount, p->rated, found);
}

int fbf_prefs_save(const FbfPrefs *p) {
  char dir[320], path[320], tmp[330], buf[512];
  snprintf(dir, sizeof dir, "%s/data", dcr_game_root());
  mkdir(dir, 0777);
  snprintf(dir, sizeof dir, "%s/" PREFS_DIR, dcr_game_root());
  mkdir(dir, 0777);
  fbf_prefs_path(path, sizeof path);
  snprintf(tmp, sizeof tmp, "%s.part", path);
  int len = fbf_prefs_format(p, buf, sizeof buf);
  FILE *f = fopen(tmp, "wb");
  int ok = f && len > 0 && fwrite(buf, 1, (size_t)len, f) == (size_t)len;
  if (f && fclose(f) != 0)
    ok = 0;
  if (ok) {
    unlink(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok) {
    unlink(tmp);
    debugPrintf("[prefs] could not write %s (is the SD card full or read-only?)\n", path);
    return -1;
  }
  return 0;
}
#endif
