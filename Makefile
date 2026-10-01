#---------------------------------------------------------------------------------
# Flappy Birds Family -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the game's own APK (the user's copy,
# any file name) is read at run time; its engine is unpacked from it on the
# first launch, its pictures and sounds read from it at every start
# (source/fbf_assets.c).
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: fbf_nx.nsp, which the launcher NRO carries (launcher/).
#---------------------------------------------------------------------------------
TARGET               := fbf_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001F1A
PORT_NPDM_VERSION    := 0.1.0
PORT_NPDM_MAIN_STACK := 0x400000
include runtime/runtime.mk

# The decoders (stb_image, stb_vorbis) are third-party code included here;
# their own warnings are theirs.
$(BUILD)/fbf_assets.o: CFLAGS += -Wno-sign-compare -Wno-unused-but-set-variable
