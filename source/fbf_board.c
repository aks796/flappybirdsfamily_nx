/* fbf_board.c -- the leaderboard: the five best rounds of 1P, and the
 * names (up to three letters) their players entered.
 *
 * The game's leaderboard button asked Amazon GameCircle for its own; there
 * is none on a Switch, so the port keeps one here (fbf_ui.c shows it). A
 * round goes on it when it scores at least [leaderboard] min_score (config.
 * ini; 10, the bronze medal, by default -- so the board is not a list of
 * first tries) and beats the fifth place, or the board has room. An equal
 * score goes below the ones already there.
 *
 * <game folder>/leaderboard.txt, plain text, one line a place:
 *     42 AKS
 * and the last name entered (the next entry starts from it):
 *     last AKS
 * Written whole to leaderboard.txt.part and renamed into place, as the
 * preferences are (fbf_prefs.c): a start that finds no leaderboard.txt reads
 * a finished .part. Deleting the file empties the board. MIT.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbf.h"

/* A name as stored and shown: A-Z and spaces, at most three, no spaces at
 * the end; "" if nothing is left. */
static void clean_name(char out[4], const char *in) {
  int n = 0;
  for (const char *p = in; *p && n < 3; p++) {
    const char c = (char)toupper((unsigned char)*p);
    if ((c >= 'A' && c <= 'Z') || c == ' ')
      out[n++] = c;
    else
      break;
  }
  while (n > 0 && out[n - 1] == ' ')
    n--;
  out[n] = 0;
}

int fbf_board_place(const FbfBoard *b, int score, int min_score) {
  if (score < min_score || score <= 0)
    return -1;
  for (int i = 0; i < b->n; i++)
    if (score > b->e[i].score)
      return i;
  return b->n < FBF_BOARD_SIZE ? b->n : -1;
}

int fbf_board_insert(FbfBoard *b, const char *name, int score) {
  const int at = fbf_board_place(b, score, 1);
  if (at < 0)
    return -1;
  const int n = b->n < FBF_BOARD_SIZE ? b->n + 1 : FBF_BOARD_SIZE;
  memmove(&b->e[at + 1], &b->e[at], sizeof b->e[0] * (size_t)(n - 1 - at));
  clean_name(b->e[at].name, name);
  if (!b->e[at].name[0])
    snprintf(b->e[at].name, sizeof b->e[at].name, "%s", "???");
  b->e[at].score = score;
  b->n = n;
  snprintf(b->last, sizeof b->last, "%s", b->e[at].name);
  return at;
}

int fbf_board_parse(FbfBoard *b, const char *text) {
  memset(b, 0, sizeof *b);
  FbfBoard in = {0};
  const char *p = text;
  while (*p) {
    const char *e = strchr(p, '\n');
    char line[64];
    const size_t len = e ? (size_t)(e - p) : strlen(p);
    snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line - 1 ? len : sizeof line - 1), p);
    p += len + (e ? 1 : 0);
    char *s = line;
    while (*s == ' ' || *s == '\t')
      s++;
    s[strcspn(s, "\r")] = 0;
    if (!*s || *s == '#')
      continue;
    if (!strncmp(s, "last ", 5)) {
      clean_name(in.last, s + 5);
      continue;
    }
    char *end;
    const long score = strtol(s, &end, 10);
    if (end == s || *end != ' ' || score <= 0 || score > 999999 || in.n >= 64)
      continue;
    char name[4];
    clean_name(name, end + 1);
    if (!name[0])
      continue;
    /* insert keeps the order (and the tie rule) whatever the file's */
    if (fbf_board_place(&in, (int)score, 1) >= 0) {
      char keep[4];
      memcpy(keep, in.last, sizeof keep);
      fbf_board_insert(&in, name, (int)score);
      memcpy(in.last, keep, sizeof keep);
    }
  }
  *b = in;
  return b->n;
}

int fbf_board_format(const FbfBoard *b, char *out, size_t cap) {
  int len = snprintf(out, cap,
                     "# Flappy Birds Family for Switch -- the leaderboard (1P): score and name,\n"
                     "# best first. Delete this file to empty it.\n");
  for (int i = 0; i < b->n && len > 0 && (size_t)len < cap; i++)
    len += snprintf(out + len, cap - (size_t)len, "%d %s\n", b->e[i].score, b->e[i].name);
  if (b->last[0] && len > 0 && (size_t)len < cap)
    len += snprintf(out + len, cap - (size_t)len, "last %s\n", b->last);
  return len > 0 && (size_t)len < cap ? len : -1;
}

#ifdef __SWITCH__
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

const char *dcr_game_root(void); /* main.c */

static void board_path(char *out, size_t cap) { snprintf(out, cap, "%s/leaderboard.txt", dcr_game_root()); }

void fbf_board_load(FbfBoard *b) {
  memset(b, 0, sizeof *b);
  char path[320], part[330];
  board_path(path, sizeof path);
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(part, sizeof part, "%s.part", path);
    f = fopen(part, "rb");
    if (f)
      debugPrintf("[board] %s missing: reading the finished %s\n", path, part);
  }
  if (!f) {
    debugPrintf("[board] no leaderboard yet\n");
    return;
  }
  char buf[4096];
  const size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  fbf_board_parse(b, buf);
  debugPrintf("[board] %d place%s; best %d\n", b->n, b->n == 1 ? "" : "s", b->n ? b->e[0].score : 0);
}

int fbf_board_save(const FbfBoard *b) {
  char path[320], tmp[330], buf[1024];
  board_path(path, sizeof path);
  snprintf(tmp, sizeof tmp, "%s.part", path);
  const int len = fbf_board_format(b, buf, sizeof buf);
  FILE *f = len > 0 ? fopen(tmp, "wb") : NULL;
  int ok = f && fwrite(buf, 1, (size_t)len, f) == (size_t)len;
  if (f && fclose(f) != 0)
    ok = 0;
  if (ok) {
    unlink(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok) {
    unlink(tmp);
    debugPrintf("[board] could not write %s (is the SD card full or read-only?)\n", path);
    return -1;
  }
  return 0;
}
#endif
