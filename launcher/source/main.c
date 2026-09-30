/* flappybirdsfamily_nx.nro -- the launcher: the one file the port ships.
 *
 * The game itself is 32-bit ARM, and a 32-bit program cannot be an NRO
 * (hbloader, which runs NROs, is 64-bit). So the game program -- the wrapper,
 * fbf_nx.nsp -- rides in this NRO's romfs, and is installed on the console as
 * an Atmosphere ExeFS override for the HOME-menu icon it was launched from:
 *
 *   1. the user makes a sphaira forwarder for this NRO and launches it;
 *   2. this runs inside that forwarder title: it checks for the user's own
 *      Flappy Birds Family APK (any *.apk in the game folder that holds the
 *      game), writes
 *      /atmosphere/contents/<the forwarder's title id>/exefs.nsp (the wrapper,
 *      main.npdm retargeted to that title id: source/dcr_exefs.h) and
 *      restarts the title;
 *   3. Atmosphere now starts the wrapper for that icon instead of hbloader.
 *      On its first run the wrapper unpacks the game's engine from the APK
 *      (source/dcr_setup.c); later NROs update it in place.
 *
 * The game folder is /switch/flappybirdsfamily_nx. Builds up to 202609260029
 * used /switch/flappybirdsfamily: an APK still there counts, and the wrapper
 * moves it (with the settings and saves) on its first start
 * (source/dcr_apkfind.c).
 *
 * The override is only ever written for a forwarder (title id 05xx...) that
 * this program is running as -- never for a real game or a system title, and
 * never from hbmenu. (From the Crossy Road / PvZ ports.) MIT.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "dcr_exefs.h"
#include "fwd_mine.h"

#define GAME_DIR "sdmc:/switch/flappybirdsfamily_nx"
#define OLD_DIR "sdmc:/switch/flappybirdsfamily" /* builds up to 202609260029 */

static PadState g_pad;

static void show(void) { consoleUpdate(NULL); }

/* Waits for + (or the HOME menu closing us). */
static void wait_exit(void) {
  printf("\nPress + to exit.\n");
  while (appletMainLoop()) {
    padUpdate(&g_pad);
    if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus)
      break;
    show();
  }
}

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *len = b ? (size_t)n : 0;
  return b;
}

static int write_file(const char *path, const uint8_t *d, size_t len) {
  char tmp[160];
  snprintf(tmp, sizeof tmp, "%s.part", path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return -1;
  int ok = fwrite(d, 1, len, f) == len;
  if (fclose(f) != 0)
    ok = 0;
  if (ok) {
    remove(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok)
    remove(tmp);
  return ok ? 0 : -1;
}

/* ------------------------------------------------------------ the APK */
/* A look at the APK's zip directory (nothing is unpacked): does it hold the
 * game's engine and art, and which build of the engine is it. */
typedef struct {
  int zip;              /* a readable zip at all */
  int have_lib, have_atlas;
  uint32_t lib_crc;
} ApkInfo;

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static ApkInfo check_apk(const char *path) {
  ApkInfo r = {0};
  FILE *f = fopen(path, "rb");
  if (!f)
    return r;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  long tail = size < 65557 ? size : 65557; /* EOCD + the longest comment */
  uint8_t *t = malloc((size_t)tail);
  uint8_t *cd = NULL;
  if (!t || fseek(f, size - tail, SEEK_SET) || fread(t, 1, (size_t)tail, f) != (size_t)tail)
    goto out;
  long e = tail - 22;
  while (e >= 0 && rd32(t + e) != 0x06054b50)
    e--;
  if (e < 0)
    goto out;
  uint32_t count = rd16(t + e + 10), cd_size = rd32(t + e + 12), cd_off = rd32(t + e + 16);
  if ((long)cd_off + (long)cd_size > size || !(cd = malloc(cd_size)) || fseek(f, (long)cd_off, SEEK_SET) ||
      fread(cd, 1, cd_size, f) != cd_size)
    goto out;
  r.zip = 1;
  for (uint32_t k = 0, off = 0; k < count && off + 46 <= cd_size; k++) {
    const uint8_t *h = cd + off;
    if (rd32(h) != 0x02014b50)
      break;
    uint32_t nl = rd16(h + 28), xl = rd16(h + 30), cl = rd16(h + 32);
    if (off + 46 + nl > cd_size)
      break;
    const char *nm = (const char *)h + 46;
    static const char lib[] = "lib/armeabi-v7a/libflapfire.so", atlas[] = "res/raw/atlas.png";
    if (nl == sizeof lib - 1 && !memcmp(nm, lib, nl)) {
      r.have_lib = 1;
      r.lib_crc = rd32(h + 16);
    } else if (nl == sizeof atlas - 1 && !memcmp(nm, atlas, nl)) {
      r.have_atlas = 1;
    }
    off += 46 + nl + xl + cl;
  }
out:
  free(cd);
  free(t);
  fclose(f);
  return r;
}

static const char *engine_build(uint32_t crc) {
  switch (crc) {
  case 0x1beb066c: return "1.0.4";
  case 0x888be3ef: return "1.0";
  default: return NULL;
  }
}

static int apk_ok(const ApkInfo *a) { return a->zip && a->have_lib && a->have_atlas; }

static int by_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

/* The player's APK in dir, whatever it is called: the first *.apk (by
 * name) that holds the game; failing that, the first *.apk at all. 1 if
 * there is any, its path in out and what it holds in info. */
static int find_apk(const char *dir, char *out, size_t cap, ApkInfo *info) {
  static char names[64][256];
  int n = 0;
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  struct dirent *e;
  while ((e = readdir(d)) && n < 64) {
    const size_t l = strlen(e->d_name);
    if (l > 4 && !strcasecmp(e->d_name + l - 4, ".apk"))
      snprintf(names[n++], sizeof names[0], "%s", e->d_name);
  }
  closedir(d);
  qsort(names, (size_t)n, sizeof names[0], by_name);
  int any = 0;
  for (int i = 0; i < n; i++) {
    char path[512];
    struct stat st;
    snprintf(path, sizeof path, "%.200s/%.255s", dir, names[i]);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
      continue;
    const ApkInfo a = check_apk(path);
    if (!any || (apk_ok(&a) && !apk_ok(info))) {
      snprintf(out, cap, "%s", path);
      *info = a;
      any = 1;
    }
    if (apk_ok(info))
      break;
  }
  return any;
}

/* ---------------------------------------------------------------- install */
static int install(uint64_t tid) {
  size_t nsp_len = 0, out_len = 0, cur_len = 0;
  uint8_t *nsp = read_file("romfs:/fbf_nx.nsp", &nsp_len), *out = NULL;
  if (!nsp || exefs_build_override(nsp, nsp_len, tid, &out, &out_len)) {
    printf("This launcher's copy of the game program is missing or damaged.\n"
           "Download flappybirdsfamily_nx.nro again.\n");
    free(nsp);
    return -1;
  }
  free(nsp);

  char dir[96], path[128];
  snprintf(dir, sizeof dir, "sdmc:/atmosphere/contents/%016lX", tid);
  snprintf(path, sizeof path, "%s/exefs.nsp", dir);
  uint8_t *cur = read_file(path, &cur_len);
  int same = cur && cur_len == out_len && !memcmp(cur, out, out_len);
  free(cur);
  if (same) {
    /* Already installed, yet this launcher ran instead of the game. */
    printf("The game program is installed for this icon (%s),\n"
           "but Atmosphere started this launcher instead of it.\n\n"
           "Update Atmosphere, then launch the icon again.\n", path);
    free(out);
    return -1;
  }
  mkdir("sdmc:/atmosphere", 0777);
  mkdir("sdmc:/atmosphere/contents", 0777);
  mkdir(dir, 0777);
  int rc = write_file(path, out, out_len);
  free(out);
  if (rc) {
    printf("Could not write %s.\nIs the SD card full or read-only?\n", path);
    return -1;
  }
  printf("Installed the game program for this icon:\n  %s\n", path);
  return 0;
}

int main(int argc, char **argv) {
  consoleInit(NULL);
  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  padInitializeAny(&g_pad);
  Result rrc = romfsInit();

  printf("Flappy Birds Family for Nintendo Switch -- launcher\n"
         "===================================================\n"
         "by aks796 (the Switch port); the game by .GEARS\n\n");
  size_t blen = 0;
  uint8_t *bnum = R_SUCCEEDED(rrc) ? read_file("romfs:/fbf_nx.build", &blen) : NULL;
  printf("Game program build: %.*s\n", bnum ? (int)(blen && bnum[blen - 1] == '\n' ? blen - 1 : blen) : 7,
         bnum ? (const char *)bnum : "missing");
  free(bnum);

  const char *self = argc > 0 && argv[0] ? argv[0] : "";
  if (*self && !strstr(self, "/switch/flappybirdsfamily_nx/"))
    printf("\nNote: this NRO is at %s.\n"
           "The game files belong in /switch/flappybirdsfamily_nx; keeping the NRO\n"
           "there too lets the game update itself when you replace it.\n", self);

  /* the APK: in the game folder, or still in the old one (it moves over on
   * the first start) */
  mkdir(GAME_DIR, 0777);
  char apk_path[512] = "";
  ApkInfo apk = {0};
  int have_apk = find_apk(GAME_DIR, apk_path, sizeof apk_path, &apk);
  int in_old = 0;
  if (!apk_ok(&apk)) {
    char old_path[512] = "";
    ApkInfo old = {0};
    if (find_apk(OLD_DIR, old_path, sizeof old_path, &old) && (apk_ok(&old) || !have_apk)) {
      snprintf(apk_path, sizeof apk_path, "%s", old_path);
      apk = old;
      have_apk = in_old = 1;
    }
  }
  const char *build = NULL;
  if (have_apk) {
    build = apk.have_lib ? engine_build(apk.lib_crc) : NULL;
    printf("\nAPK: %s\n  %s\n", apk_path + 5 /* past "sdmc:" */,
           !apk.zip ? "not a readable APK"
           : !apk.have_lib ? "has no 32-bit ARM Flappy Birds Family engine"
           : !apk.have_atlas ? "has no res/raw/atlas.png"
           : build ? "Flappy Birds Family" : "Flappy Birds Family (a build this port has not seen)");
    if (build)
      printf("  engine build %s\n", build);
    if (in_old && apk_ok(&apk))
      printf("  In the old game folder: it moves to /switch/flappybirdsfamily_nx on\n"
             "  the first start, with your settings and scores.\n");
  } else {
    printf("\nAPK: MISSING\n");
  }
  const int ok_apk = have_apk && apk_ok(&apk);

  u64 tid = 0;
  svcGetInfo(&tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  int in_05 = appletGetAppletType() == AppletType_Application && exefs_is_forwarder_tid(tid);
  int forwarder = in_05 && fwd_is_mine(self);

  if (!forwarder) {
    if (in_05)
      printf("\nThis was opened from inside another icon (sphaira or hbmenu started\n"
             "from its own HOME-menu icon), so nothing is installed there.\n");
    printf("\nStart this from its own HOME-menu icon:\n"
           "  1. put the APK of your own Flappy Birds Family\n"
           "     (com.dotgears.flapfire) in /switch/flappybirdsfamily_nx\n"
           "     (any file name ending in .apk)\n"
           "  2. in sphaira: Homebrew > Flappy Birds Family > Install Forwarder\n"
           "  3. launch the new Flappy Birds Family icon on the HOME menu.\n"
           "The first launch installs the game program for that icon and starts it.\n");
    wait_exit();
  } else if (!ok_apk) {
    printf("\nCopy the APK of your own Flappy Birds Family (com.dotgears.flapfire,\n"
           "with lib/armeabi-v7a, e.g. the Amazon Appstore build 1.0.4) into\n  %s\n"
           "(any file name ending in .apk), then launch this icon again.\n", GAME_DIR + 5);
    wait_exit();
  } else if (install(tid) != 0) {
    wait_exit();
  } else {
    printf("\nStarting Flappy Birds Family...\n"
           "(the first start unpacks the game's engine from the APK)\n");
    show();
    svcSleepThread(1500000000ll);
    romfsExit();
    Result rc = appletRestartProgram(NULL, 0);
    printf("\nRestarting did not work (0x%x): close this and launch\n"
           "Flappy Birds Family again.\n", rc);
    wait_exit();
    consoleExit(NULL);
    return 0;
  }
  romfsExit();
  consoleExit(NULL);
  return 0;
}
