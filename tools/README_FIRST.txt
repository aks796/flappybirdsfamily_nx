Flappy Birds Family for Nintendo Switch (32-bit wrapper)
========================================================
by aks796 (the Switch port) and .GEARS (the game)

The Android game by dotGears, run on the Switch by a wrapper that loads the
game's own code. The wrapper ships no part of the game: you supply the APK
of a copy you own.

WHAT YOU NEED ON THE SD CARD
  switch/flappybirdsfamily_nx/flappybirdsfamily_nx.nro   the launcher
  switch/flappybirdsfamily_nx/<any name>.apk             YOUR copy of the game:
      Flappy Birds Family (com.dotgears.flapfire) with lib/armeabi-v7a,
      e.g. the Amazon Appstore build 1.0.4. Keep its file name: any name
      ending in .apk works.
  Atmosphere and sphaira.
Everything else is made on the console.

SET UP (once)
  1. In sphaira: Homebrew -> Flappy Birds Family -> Install Forwarder.
  2. Launch the new "Flappy Birds Family" icon on the HOME menu. The launcher
     checks the APK, installs the 32-bit game program for that icon
     (atmosphere/contents/<its title id>/exefs.nsp) and restarts it.
  3. The first start unpacks the game's engine from the APK (a second or
     two, with a progress bar; again only when the APK changes).

PLAYING
  One or two players, two controllers at most: the first two connected of
  the Joy-Cons on the console, the controller in slot 1 and the controller
  in slot 2 (docked: players 1 and 2). A pair of Joy-Cons is one
  controller; a single Joy-Con held sideways is one too (its stick and
  buttons turn with it: the button on the right is A, the bottom one B).
  More controllers can stay connected, but they do not play.
  MAIN MENU: press a button to join: the first controller to press one is
  P1, the next one P2. Left / Right move your cursor (mode button, your
  bird, Play); Up / Down change your bird; A on Play starts.
  2P: the game turns to 2P by itself when a second player has joined. Put
  your cursor on the mode button (1P) and press A: a second connected
  controller joins, or the Switch's controller screen opens to connect one.
  IN A GAME:
    A B X Y L R ZL ZR SL SR   flap / choose
    D-pad, left stick         move through the menus
    +                         pause / resume
    -                         back to the main menu; on the main menu,
                              close the game
    touch screen              tap to flap
  The high score is kept in switch/flappybirdsfamily_nx/data/shared_prefs/.
  LEADERBOARD (1P): score 10 or more (a medal) and beat the fifth place,
  and you enter your name: Up / Down change the letter, Left / Right move,
  A next letter / done, B back. The leaderboard button on the game over
  screen shows the five best. They are kept in
  switch/flappybirdsfamily_nx/leaderboard.txt (delete it to start over).

UPDATING
  Copy the new flappybirdsfamily_nx.nro over the old one and launch the
  icon: the game installs the newer build itself and restarts.
  From an older build (folder switch/flappybirdsfamily, FlappyBirdsFamily.nro):
  put flappybirdsfamily_nx.nro in that old folder and launch the icon. The
  game updates itself, then moves your APK, settings, high score and
  leaderboard into switch/flappybirdsfamily_nx (the old .nro stays; delete
  it). Put later NROs in switch/flappybirdsfamily_nx.

SETTINGS
  switch/flappybirdsfamily_nx/config.ini, written at the first start with every
  option explained (controls, touch, volume, leaderboard, resolution).

COPYING ON A MAC
  Your card already has "atmosphere" and "switch" folders. Dragging these
  on top and choosing REPLACE deletes what is there. Hold Option while
  dragging and choose "Merge", or copy the individual files.

UNDO
  Delete atmosphere/contents/<id>/exefs.nsp (the icon then opens the
  launcher again), or remove the forwarder.

WHEN SOMETHING GOES WRONG
  switch/flappybirdsfamily_nx/debug.log and crash.log, and Atmosphere's
  report in atmosphere/crash_reports/.
