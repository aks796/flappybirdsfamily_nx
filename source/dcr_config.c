/* dcr_config.c -- <game folder>/config.ini, the user's settings.
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file is appended to (options a newer build adds,
 * at the end, with their defaults), so edits and comments survive updates.
 * Plain INI: [section], key = value, # comments; booleans take true/false,
 * yes/no, on/off, 1/0. Read once at start-up: changes apply the next time the
 * game starts. (The machinery is the Crossy Road / PvZ ports'; the options
 * are this game's.) MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "dcr_build.h"
#include "dcr_config.h"
#include "fbf.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

static DcrConfig g_cfg = {
    .back_exits = 1,
    .stick_dpad = 1,
    .joycon_sideways = 1,
    .plus_pause = 1,
    .controller_screen = 1,
    .touch = 1,
    .board = 1,
    .board_min = 10,
    .volume = 100,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

enum { K_BOOL, K_TEXT };

typedef struct {
  const char *section, *key, *def, *help;
  int kind;
} Opt;

static const Opt k_opts[] = {
    {"controls", "plus_pauses", "true",
     "+ pauses the game and resumes it (the screen dims while paused).\n"
     "# The game on Android had no pause button; this is the port's.",
     K_BOOL},
    {"controls", "minus_on_main_menu_closes_game", "true",
     "- is the Android Back key: in a game it returns to the main menu, and on\n"
     "# the main menu it closes the game (as Back does on Android). false: - on\n"
     "# the main menu does nothing (close the game from the HOME menu).",
     K_BOOL},
    {"controls", "controller_screen_for_2p", "true",
     "On the main menu, A on the mode button (1P / 2P) with only one player asks\n"
     "# for 2P: a second controller that is connected joins as player 2; with none,\n"
     "# the Switch's controller screen opens to connect one. false: a second\n"
     "# player joins only by pressing a button on its controller (the game's way).",
     K_BOOL},
    {"controls", "left_stick_as_dpad", "true",
     "The left stick works as the D-pad (moving through the menus), as Android\n"
     "# turns a gamepad's stick into D-pad keys. In a game, pushing it flaps like\n"
     "# any button. Single Joy-Cons need it: the right one has no D-pad.",
     K_BOOL},
    {"controls", "single_joycon_sideways", "true",
     "A single Joy-Con (one player each) is held sideways, SL and SR on top:\n"
     "# its stick and its four buttons are turned with it (the button on the\n"
     "# right is A, the bottom one B). false: held upright, nothing turned.",
     K_BOOL},
    {"touch", "tap_to_flap", "true",
     "Tapping the touch screen flaps, as on the phone (handheld mode).", K_BOOL},
    {"sound", "volume", "100", "Sound effects volume, 0 to 100 (the console's volume applies too).",
     K_TEXT},
    {"leaderboard", "enabled", "true",
     "The leaderboard button (1P game over) shows the five best rounds, and a\n"
     "# round that makes it asks for a name (3 letters, Up / Down like the bird\n"
     "# picker). Kept in leaderboard.txt next to this file; delete it to start\n"
     "# over. false: the button does nothing, as without Amazon's leaderboard.",
     K_BOOL},
    {"leaderboard", "min_score", "10",
     "The least a round must score to go on the leaderboard (10: a bronze medal),\n"
     "# so it is not filled by first tries; 1 to 9999.",
     K_TEXT},
    {"display", "resolution", "auto",
     "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
     "# starts). The game draws its own 768x432 picture and scales it up; the\n"
     "# Switch scales the result to the screen.",
     K_TEXT},
    {"performance", "boost_cpu_when_loading", "true",
     "CPU at 1785 MHz while the game starts (until its first picture).", K_BOOL},
    {"debug", "gl_selftest", "false", "Graphics self-test picture at start-up.", K_BOOL},
    {"debug", "boot_log_on_screen", "false",
     "Show the start-up log on screen at every launch. Off: the log appears only\n"
     "# while something is being set up (first launch, a new APK or NRO).",
     K_BOOL},
    {"debug", "log_java_calls", "false",
     "Write every Java method the game calls to debug.log (for bug reports).", K_BOOL},
    {"debug", "log_input", "false",
     "Write every key and touch the game receives, and what the controllers\n"
     "# report, to debug.log (for bug reports).",
     K_BOOL},
    {"config", "version", "1", "Settings file format; leave as it is.", K_TEXT},
};
#define O_COUNT ((int)(sizeof k_opts / sizeof k_opts[0]))

static char g_val[O_COUNT][24];
static int g_have[O_COUNT];

static int opt_index(const char *section, const char *key) {
  for (int i = 0; i < O_COUNT; i++)
    if (!strcmp(k_opts[i].section, section) && !strcmp(k_opts[i].key, key))
      return i;
  return -1;
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    *--e = 0;
  return s;
}

static void parse(FILE *f) {
  char line[256], section[32] = "";
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      char *e = strchr(s, ']');
      if (e) {
        *e = 0;
        snprintf(section, sizeof section, "%s", trim(s + 1));
      }
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq)
      continue;
    *eq = 0;
    char *key = trim(s), *val = trim(eq + 1);
    char *hash = strpbrk(val, "#;");
    if (hash) {
      *hash = 0;
      val = trim(val);
    }
    for (int i = 0; i < O_COUNT; i++)
      if (!strcasecmp(section, k_opts[i].section) && !strcasecmp(key, k_opts[i].key)) {
        snprintf(g_val[i], sizeof g_val[i], "%s", val);
        g_have[i] = 1;
      }
  }
}

static void write_opts(FILE *f, int only_missing) {
  const char *last = NULL;
  for (int i = 0; i < O_COUNT; i++) {
    if (only_missing && g_have[i])
      continue;
    if (!last || strcmp(last, k_opts[i].section))
      fprintf(f, "\n[%s]\n", k_opts[i].section);
    last = k_opts[i].section;
    if (k_opts[i].help)
      fprintf(f, "# %s\n", k_opts[i].help);
    fprintf(f, "%s = %s\n", k_opts[i].key, g_val[i]);
  }
}

static int as_bool(int i) {
  const char *v = g_val[i];
  if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcmp(v, "1"))
    return 1;
  if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off") || !strcmp(v, "0"))
    return 0;
  debugPrintf("[config] %s = %s: not true/false, using %s\n", k_opts[i].key, v, k_opts[i].def);
  return !strcmp(k_opts[i].def, "true");
}

static int opt_bool(const char *section, const char *key) { return as_bool(opt_index(section, key)); }
static const char *opt_text(const char *section, const char *key) { return g_val[opt_index(section, key)]; }

void dcr_config_load(void) {
  for (int i = 0; i < O_COUNT; i++)
    snprintf(g_val[i], sizeof g_val[i], "%s", k_opts[i].def);
  char path[300];
  snprintf(path, sizeof path, "%s/config.ini", dcr_game_root());
  FILE *f = fopen(path, "r");
  if (f) {
    parse(f);
    fclose(f);
    int missing = 0;
    for (int i = 0; i < O_COUNT; i++)
      missing += !g_have[i];
    if (missing && (f = fopen(path, "a"))) {
      fprintf(f, "\n# Added by build %llu (new options, at their defaults):\n",
              (unsigned long long)DCR_BUILD);
      write_opts(f, 1);
      fclose(f);
      debugPrintf("[config] added %d new option%s to config.ini\n", missing, missing > 1 ? "s" : "");
    }
  } else if ((f = fopen(path, "w"))) {
    fputs("# Flappy Birds Family for Switch -- settings.\n"
          "# Changes apply the next time the game starts. Delete this file to get\n"
          "# the defaults back.\n",
          f);
    write_opts(f, 0);
    fclose(f);
    debugPrintf("[config] wrote config.ini with the defaults\n");
  }

  g_cfg.plus_pause = opt_bool("controls", "plus_pauses");
  g_cfg.back_exits = opt_bool("controls", "minus_on_main_menu_closes_game");
  g_cfg.controller_screen = opt_bool("controls", "controller_screen_for_2p");
  g_cfg.stick_dpad = opt_bool("controls", "left_stick_as_dpad");
  g_cfg.joycon_sideways = opt_bool("controls", "single_joycon_sideways");
  g_cfg.touch = opt_bool("touch", "tap_to_flap");
  g_cfg.board = opt_bool("leaderboard", "enabled");
  g_cfg.boost = opt_bool("performance", "boost_cpu_when_loading");
  g_cfg.gl_selftest = opt_bool("debug", "gl_selftest");
  g_cfg.boot_log = opt_bool("debug", "boot_log_on_screen");
  g_cfg.log_jni = opt_bool("debug", "log_java_calls");
  g_cfg.log_input = opt_bool("debug", "log_input");

  const char *vol = opt_text("sound", "volume");
  char *end = NULL;
  long v = strtol(vol, &end, 10);
  if (!*vol || (end && *end) || v < 0 || v > 100) {
    debugPrintf("[config] volume = %s: not 0 to 100, using 100\n", vol);
    v = 100;
  }
  g_cfg.volume = (int)v;

  const char *ms = opt_text("leaderboard", "min_score");
  end = NULL;
  v = strtol(ms, &end, 10);
  if (!*ms || (end && *end) || v < 1 || v > 9999) {
    debugPrintf("[config] min_score = %s: not 1 to 9999, using 10\n", ms);
    v = 10;
  }
  g_cfg.board_min = (int)v;

  const char *r = opt_text("display", "resolution");
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  int h = !strcmp(r, "720") ? 720 : !strcmp(r, "1080") ? 1080 : !strcasecmp(r, "auto") ? (docked ? 1080 : 720) : 0;
  if (!h) {
    debugPrintf("[config] resolution = %s: not 720, 1080 or auto, using auto\n", r);
    h = docked ? 1080 : 720;
  }
  g_cfg.res_h = h;
  g_cfg.res_w = h * 16 / 9;
  dcr_window_set_size(g_cfg.res_w, g_cfg.res_h);

  debugPrintf("[config] %dx%d (%s, %s); + pauses %s, - on the main menu %s, stick as D-pad %s, "
              "single Joy-Cons %s, touch %s, volume %d%%, CPU boost %s\n",
              g_cfg.res_w, g_cfg.res_h, r, docked ? "docked" : "handheld",
              g_cfg.plus_pause ? "on" : "off", g_cfg.back_exits ? "closes the game" : "does nothing",
              g_cfg.stick_dpad ? "on" : "off", g_cfg.joycon_sideways ? "sideways" : "upright",
              g_cfg.touch ? "on" : "off", g_cfg.volume, g_cfg.boost ? "on" : "off");
}
