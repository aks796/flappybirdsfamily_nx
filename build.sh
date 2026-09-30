#!/bin/sh
# Build fbf_nx.nsp inside the AArch32 Switch toolchain container (devkitARM +
# libnx32 from the vita2hos image). Arguments are passed to make, e.g.
#   ./build.sh            # fbf_nx.nsp
#   ./build.sh clean
#   ./build.sh DCR_GL_MESA=0   # the null renderer
#
# libnx itself is the patched libnx32 (github.com/aks796/libnx32, the vita2hos
# AArch32 libnx with that project's fixes: IPC data that depended on the size
# of an enum, among it the supported-controller list that kept wireless
# controllers out). Its headers and archives are mounted over the image's; the
# image's other libraries in the same folder (miniz, deko3d...) stay. Clone it
# next to this folder and run its ./build.sh; DCR_LIBNX32 names another install.
#
# The renderer, Mesa for AArch32, is mesa32's prefix copied to ./portlibs32
# (tools/get_portlibs.sh does that); without it the build uses the null
# renderer.
set -e
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$DCR_LIBNX32" ]; then
  for c in "$HERE/../libnx32/prefix" "$HERE/../../libnx32/prefix" "$HERE/../../thirtytwo/libnx32/prefix"; do
    if [ -f "$c/lib/libnx.a" ]; then DCR_LIBNX32="$c"; break; fi
  done
fi
LIBNX32="${DCR_LIBNX32:-$HERE/../libnx32/prefix}"
if [ ! -f "$LIBNX32/lib/libnx.a" ] || [ ! -f "$LIBNX32/include/switch.h" ]; then
  echo "build.sh: the patched libnx32 is not at $LIBNX32" >&2
  echo "  clone github.com/aks796/libnx32 next to this folder and run its ./build.sh," >&2
  echo "  or set DCR_LIBNX32 to its prefix" >&2
  exit 1
fi
LIBNX32="$(cd "$LIBNX32" && pwd)"
if [ ! -f "$HERE/portlibs32/lib/libEGL.a" ]; then
  echo "build.sh: no portlibs32/ (Mesa): building with the null renderer (run tools/get_portlibs.sh)" >&2
fi
NXD=/opt/devkitpro/libnx32
exec docker run --rm --platform linux/amd64 \
  -v "$HERE:/work" \
  -v "$LIBNX32/include/switch:$NXD/include/switch:ro" \
  -v "$LIBNX32/include/switch.h:$NXD/include/switch.h:ro" \
  -v "$LIBNX32/lib/libnx.a:$NXD/lib/libnx.a:ro" \
  -v "$LIBNX32/lib/libnxd.a:$NXD/lib/libnxd.a:ro" \
  -w /work "$IMAGE" \
  bash -lc "make -j\$(nproc) $*"
