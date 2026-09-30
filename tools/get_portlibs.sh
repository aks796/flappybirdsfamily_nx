#!/bin/sh
# Copy the AArch32 Mesa build (mesa32's prefix/, or its release tarball) into
# ./portlibs32, where the Makefile looks for the renderer. Docker cannot follow
# a symlink out of the mounted project, so it is a copy. By default it looks
# for mesa32 cloned next to this folder; MESA32 (or GFX32) names another
# prefix.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${MESA32:-$GFX32}"
if [ -z "$SRC" ]; then
  for c in "$HERE/../mesa32/prefix" "$HERE/../../thirtytwo/gfx32/prefix"; do
    if [ -f "$c/lib/libEGL.a" ]; then SRC="$c"; break; fi
  done
  SRC="${SRC:-$HERE/../mesa32/prefix}"
fi
if [ ! -f "$SRC/lib/libEGL.a" ]; then
  echo "get_portlibs.sh: no Mesa build at $SRC" >&2
  echo "  clone github.com/aks796/mesa32 next to this folder and run its ./build.sh" >&2
  echo "  (or unpack its release tarball), or set MESA32 to its prefix" >&2
  exit 1
fi
rm -rf "$HERE/portlibs32"
cp -R "$SRC" "$HERE/portlibs32"
echo "portlibs32/ <- $SRC"
