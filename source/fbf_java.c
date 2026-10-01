/* fbf_java.c -- the Java side of Flappy Birds Family, as the engine sees it.
 *
 * The game's Java (com.dotgears.game.GameActivity, its GLSurfaceView "f" and
 * renderer "j", the SoundPool "k", the texture loader com.dotgears.a) does not
 * run here: fbf_game.c, fbf_input.c, fbf_audio.c and fbf_assets.c do what it
 * did. What the ENGINE calls back through JNI is small, and all of it is in
 * Java_com_dotgears_dot_1JNILib_init:
 *
 *     cls = GetObjectClass(activity)
 *     id  = GetMethodID(cls, "getPackageName", "()Ljava/lang/String;")
 *     if (strcmp(GetStringUTFChars(CallObjectMethod(activity, id)),
 *                "com.dotgears.flapfire")) hasPermission = 0;
 *
 * and with hasPermission cleared renderFrame never draws. The answer is the
 * user's own APK's package (its manifest), which is that name. The other
 * natives use only arrays (setInputDevices: GetArrayLength /
 * Get/ReleaseIntArrayElements; getOutputEvents: NewIntArray /
 * SetIntArrayRegion) and strings (setAtlas: GetStringUTFChars), which the JNI
 * core does itself. Anything else the engine asked for would be logged once
 * as unhandled (jni_core.c). MIT.
 */
#include <string.h>

#include "config.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "fbf.h"
#include "jni.h"
#include "util.h"

#define GA "com/dotgears/game/GameActivity"
#define JL "com/dotgears/dot_JNILib"
#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity;
void *g_jnilib;

H(h_getPackageName) {
  const char *pkg = dcr_manifest_loaded() && dcr_manifest_package()[0] ? dcr_manifest_package()
                                                                        : FBF_PACKAGE;
  return jv_l(jni_str(pkg));
}

const JMethodDef jni_method_defs[] = {
    {"android/content/Context", "getPackageName", "()" S, h_getPackageName},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {GA, "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {NULL, NULL},
};

/* The game's own dex has no optional classes the engine probes for. */
const char *const jni_missing_classes[] = {
    NULL,
};

void fbf_java_init(void) {
  jni_init();
  g_activity = jni_singleton(GA);
  g_jnilib = jni_class(JL)->obj;
  debugPrintf("[java] GameActivity %p, dot_JNILib %p; package %s\n", (void *)g_activity, g_jnilib,
              dcr_manifest_loaded() ? dcr_manifest_package() : FBF_PACKAGE " (default)");
}
