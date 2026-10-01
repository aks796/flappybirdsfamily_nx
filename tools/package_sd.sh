#!/bin/sh
# package_sd.sh -- assemble SD_CARD/ (and SD_CARD.zip) for a Switch.
#
#   tools/package_sd.sh [path/to/your/FlappyBirdsFamily.apk]
#
# Builds the wrapper and the launcher, runs the host tests, then lays out
# what goes on the card:
#   SD_CARD/switch/flappybirdsfamily_nx/flappybirdsfamily_nx.nro
#   SD_CARD/switch/flappybirdsfamily_nx/<your APK, its own name>
#                                  (only if an APK path is given: your own
#                                   copy, for your card)
#   SD_CARD/README_FIRST.txt
# SD_CARD.zip never contains an APK: it is the port alone, fit to share.
# The ExeFS override is not included: the launcher writes it for whichever
# sphaira forwarder it is started from.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
[ -f portlibs32/lib/libEGL.a ] || tools/get_portlibs.sh
./build.sh
launcher/build.sh
python3 runtime/tools/gen_imports.py --check
python3 tools/test_input.py
python3 tools/test_menu.py
python3 tools/test_ui.py
if [ -n "$1" ]; then
  python3 tools/check_engine.py "$1"
  python3 tools/test_ui.py "$1"
  python3 tools/test_setup.py "$1" launcher/flappybirdsfamily_nx.nro
  python3 tools/test_game.py "$1"
fi

rm -rf SD_CARD SD_CARD.zip
mkdir -p SD_CARD/switch/flappybirdsfamily_nx
cp launcher/flappybirdsfamily_nx.nro SD_CARD/switch/flappybirdsfamily_nx/
cp tools/README_FIRST.txt SD_CARD/
(cd SD_CARD && zip -qr ../SD_CARD.zip .)
if [ -n "$1" ]; then
  cp -p "$1" SD_CARD/switch/flappybirdsfamily_nx/ # its own name; -p: keep its date (the same file every build)
fi
echo "build $(cat fbf_nx.build): SD_CARD/ and SD_CARD.zip (no APK in the zip) ready"
