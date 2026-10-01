/* dcr_config.c -- Flappy Birds Family's settings: config.ini's options, on
 * the runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * The options, their order, defaults and help text are the ones this port
 * has always written, so players' config.ini files read the same: written
 * whole on the first start, the options a newer build adds appended at the
 * end ("# Added by build ..."), [config] version = 1 last. Booleans take
 * true/false, yes/no, on/off, 1/0; a value that is not one of them, or a
 * number out of its range, falls back to the default (logged). Read once at
 * start-up: changes apply the next time the game starts. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

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

static const CfgOpt k_opts[] = {
    {"controls", "plus_pauses", "true",
     "+ pauses the game and resumes it (the screen dims while paused).\n"
     "# The game on Android had no pause button; this is the port's.",
     CFG_BOOL, NULL, &g_cfg.plus_pause},
    {"controls", "minus_on_main_menu_closes_game", "true",
     "- is the Android Back key: in a game it returns to the main menu, and on\n"
     "# the main menu it closes the game (as Back does on Android). false: - on\n"
     "# the main menu does nothing (close the game from the HOME menu).",
     CFG_BOOL, NULL, &g_cfg.back_exits},
    {"controls", "controller_screen_for_2p", "true",
     "On the main menu, A on the mode button (1P / 2P) with only one player asks\n"
     "# for 2P: a second controller that is connected joins as player 2; with none,\n"
     "# the Switch's controller screen opens to connect one. false: a second\n"
     "# player joins only by pressing a button on its controller (the game's way).",
     CFG_BOOL, NULL, &g_cfg.controller_screen},
    {"controls", "left_stick_as_dpad", "true",
     "The left stick works as the D-pad (moving through the menus), as Android\n"
     "# turns a gamepad's stick into D-pad keys. In a game, pushing it flaps like\n"
     "# any button. Single Joy-Cons need it: the right one has no D-pad.",
     CFG_BOOL, NULL, &g_cfg.stick_dpad},
    {"controls", "single_joycon_sideways", "true",
     "A single Joy-Con (one player each) is held sideways, SL and SR on top:\n"
     "# its stick and its four buttons are turned with it (the button on the\n"
     "# right is A, the bottom one B). false: held upright, nothing turned.",
     CFG_BOOL, NULL, &g_cfg.joycon_sideways},
    {"touch", "tap_to_flap", "true",
     "Tapping the touch screen flaps, as on the phone (handheld mode).",
     CFG_BOOL, NULL, &g_cfg.touch},
    {"sound", "volume", "100", "Sound effects volume, 0 to 100 (the console's volume applies too).",
     CFG_INT, NULL, &g_cfg.volume, 0, 100},
    {"leaderboard", "enabled", "true",
     "The leaderboard button (1P game over) shows the five best rounds, and a\n"
     "# round that makes it asks for a name (3 letters, Up / Down like the bird\n"
     "# picker). Kept in leaderboard.txt next to this file; delete it to start\n"
     "# over. false: the button does nothing, as without Amazon's leaderboard.",
     CFG_BOOL, NULL, &g_cfg.board},
    {"leaderboard", "min_score", "10",
     "The least a round must score to go on the leaderboard (10: a bronze medal),\n"
     "# so it is not filled by first tries; 1 to 9999.",
     CFG_INT, NULL, &g_cfg.board_min, 1, 9999},
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
                       "# starts). The game draws its own 768x432 picture and scales it up; the\n"
                       "# Switch scales the result to the screen."),
    CFG_ROW_BOOST("CPU at 1785 MHz while the game starts (until its first picture).", &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch. Off: the log appears only\n"
                     "# while something is being set up (first launch, a new APK or NRO).",
                     &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (for bug reports).", &g_cfg.log_jni),
    {"debug", "log_input", "false",
     "Write every key and touch the game receives, and what the controllers\n"
     "# report, to debug.log (for bug reports).",
     CFG_BOOL, NULL, &g_cfg.log_input},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config(); /* the resolution: rt_cfg.c sets the window to it */
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s); + pauses %s, - on the main menu %s, stick as D-pad %s, "
              "single Joy-Cons %s, touch %s, volume %d%%, CPU boost %s\n",
              g_cfg.res_w, g_cfg.res_h, rt_config_get("display", "resolution"), docked ? "docked" : "handheld",
              g_cfg.plus_pause ? "on" : "off", g_cfg.back_exits ? "closes the game" : "does nothing",
              g_cfg.stick_dpad ? "on" : "off", g_cfg.joycon_sideways ? "sideways" : "upright",
              g_cfg.touch ? "on" : "off", g_cfg.volume, g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
