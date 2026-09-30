/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

typedef struct {
  int back_exits;        /* [controls] minus_on_main_menu_closes_game */
  int stick_dpad;        /* [controls] left_stick_as_dpad */
  int joycon_sideways;   /* [controls] single_joycon_sideways */
  int plus_pause;        /* [controls] plus_pauses */
  int controller_screen; /* [controls] controller_screen_for_2p */
  int touch;             /* [touch] tap_to_flap */
  int board;             /* [leaderboard] enabled */
  int board_min;         /* [leaderboard] min_score */
  int volume;            /* [sound] volume, 0-100 */
  int res_w, res_h;      /* [display] resolution */
  int boost;             /* [performance] boost_cpu_when_loading */
  int gl_selftest;       /* [debug] gl_selftest */
  int boot_log;          /* [debug] boot_log_on_screen */
  int log_jni;           /* [debug] log_java_calls */
  int log_input;         /* [debug] log_input */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
