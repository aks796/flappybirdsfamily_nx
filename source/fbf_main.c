/* fbf_main.c -- Flappy Birds Family's part of the boot: the runtime's main()
 * (runtime/source/main.c) does the rest -- the log, config.ini, the old
 * folder moved in, the NRO self-update, the APK found by what it holds, its
 * package checked -- and calls these.
 *
 * The first launch (runtime/source/dcr_setup.c, from the plan below):
 * libflapfire.so out of lib/armeabi-v7a/ (the whole game engine) and
 * classes.txt (the Java class names its classes*.dex define, which
 * jni_core.c answers FindClass with), made again whenever the APK changes
 * (.setup stamps, keys "libflapfire.so" and "classes.txt"). The game's
 * pictures and sounds are read straight out of the APK at every start
 * (fbf_assets.c). The bar, in permille of the first launch:
 *     0- 200  (the APK found and checked: the runtime's main())
 *   200- 800  libflapfire.so unpacked (by bytes written)
 *   800- 950  the Java class list
 *        1000 the game starts
 * MIT.
 */
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "fbf.h"
#include "rt_boot.h"
#include "util.h"

static const char *const k_libs[] = {FBF_LIB};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's engine",
    .apk_requirement = "This port needs Flappy Birds Family (com.dotgears.flapfire) for\n"
                       "32-bit ARM (armeabi-v7a): use the APK of your own copy.",
    .libs_p0 = 200,
    .libs_p1 = 800,
    .classes_p0 = 800,
    .classes_p1 = 950,
};

/* From the APK to the game's first code: libflapfire.so and classes.txt
 * (again when the APK changed), then the engine loaded, relocated, resolved
 * against the shims and mapped as code. */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (fbf_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" FBF_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n" FBF_LIB
                " and .setup there to unpack it again. See debug.log.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary("flapfire"): the library's constructors (gnustl's
 * locale and streams, the engine's statics); then GameActivity. */
void port_run(void) {
  fbf_run_constructors();
  fbf_game_run();
}

/* For the error screens. */
const char *port_apk_help(void) {
  return "Copy the APK of your own Flappy Birds Family (com.dotgears.flapfire,\n"
         "armeabi-v7a) into /switch/" PORT_NAME ". Any file name ending in .apk\n"
         "works: the game's code, pictures and sounds are read from it.";
}
