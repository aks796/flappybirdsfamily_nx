/* fbf_launcher.c -- Flappy Birds Family's hook in the shared launcher
 * (runtime/launcher/): under "APK: <file>", which build of the game's engine
 * it holds, told by its libflapfire.so's CRC (the zip's directory: nothing
 * is unpacked). MIT.
 */
#include <stdio.h>
#include <string.h>

#include "launcher.h"

typedef struct {
  int found;
  uint32_t crc;
} LibCrc;

static int find_lib(const RtZipEntry *e, void *ctx) {
  LibCrc *l = ctx;
  if (strcmp(e->name, "lib/armeabi-v7a/libflapfire.so"))
    return 0;
  l->found = 1;
  l->crc = e->crc;
  return 1;
}

void port_launcher_apk_note(const char *apk_path) {
  if (launcher_bar_on())
    return;
  LibCrc l = {0, 0};
  if (rt_zip_walk_path(apk_path, find_lib, &l) != 0 || !l.found)
    return;
  const char *build = l.crc == 0x1beb066cu ? "1.0.4" : l.crc == 0x888be3efu ? "1.0" : NULL;
  if (build)
    printf("  Flappy Birds Family, engine build %s\n", build);
  else
    printf("  Flappy Birds Family (a build this port has not seen)\n");
}
