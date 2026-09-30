/* dcr_apkfind.h -- the game folder moved from its old name, and the APK in
 * it found by what it holds (dcr_apkfind.c). */
#ifndef DCR_APKFIND_H
#define DCR_APKFIND_H
#include <stddef.h>

/* Moves everything but .nro files from old_root into new_root (which must
 * exist); an entry new_root already has stays where it is. The old folder
 * is removed if that empties it. Returns the number of entries moved. */
int dcr_move_old_folder(const char *old_root, const char *new_root);

/* The APK in root that holds this game (lib/armeabi-v7a/<lib> and
 * res/raw/atlas.png), whatever its name; with several, the highest version
 * code (then the first by name). 0 and its path in out, or -1 and a reason
 * for the player in why. */
int dcr_find_apk(const char *root, const char *lib, char *out, size_t cap, char *why, size_t whycap);

#endif
