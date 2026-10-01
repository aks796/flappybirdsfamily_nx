#!/bin/sh
# Build flappybirdsfamily_nx.nro (the launcher) with the runtime's launcher
# build (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../fbf_nx.nsp and ../fbf_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=fbf_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
