/* fbf_audio.c -- the game's SoundPool, played through audout.
 *
 * The engine makes no sound itself. It reports events (fbf_game.c), and the
 * Java plays one of six short sounds for each with a SoundPool built as
 * new SoundPool(10, STREAM_MUSIC, 100): up to 10 at once, each played with
 * play(id, vol, vol, priority 1, no loop, rate 1.0), vol = the music volume
 * (full here: the console's own volume applies after) and a third of it for
 * the point sound. When 10 are playing, SoundPool stops the oldest.
 *
 * Here: the sounds are decoded once (fbf_assets.c), and the runtime's mixer
 * pump (rt_audout.c) calls mix() for each RT_AUDOUT_FRAMES (512) block: the
 * playing voices summed, resampled from 44.1 kHz to the device's 48 kHz
 * (linear). Three blocks in flight is ~32 ms of sound ahead, and a block is
 * mixed just before it is queued, so a sound starts within that. audout
 * itself (its 64-bit buffer descriptor, the self-test) is the runtime's.
 * MIT.
 */
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "fbf.h"
#include "rt_audout.h"
#include "util.h"

_Static_assert(RT_AUDOUT_FRAMES == 512, "port_config.h: flappy mixes 512-frame blocks");
#define FRAMES_PER_BLOCK RT_AUDOUT_FRAMES

static int g_ao_ready;
static unsigned g_out_rate = 48000;

/* ------------------------------------------------------------- SoundPool */
#define MAX_VOICES 10 /* new SoundPool(10, ...) */

typedef struct {
  int16_t *pcm;
  int frames, channels, rate;
} Sound;

typedef struct {
  int sound;     /* -1: free */
  double pos;    /* in source frames */
  double step;   /* source frames per output frame */
  float gain;
  uint32_t age;  /* start order: the oldest is stopped for a new one */
} Voice;

static Sound g_sounds[FBF_SFX_COUNT];
static Voice g_voices[MAX_VOICES];
static Mutex g_lock;
static uint32_t g_started;
static float g_master = 1.0f;

uint32_t dcr_audio_blocks(void) { return (uint32_t)rt_audout_pump_blocks(); }
unsigned long dcr_audio_underruns(void) {
  RtAudoutStats st;
  rt_audout_stats(&st);
  return st.underruns;
}

int fbf_audio_set_sound(int id, int16_t *pcm, int frames, int channels, int rate) {
  if (id < 0 || id >= FBF_SFX_COUNT || !pcm || frames <= 0)
    return -1;
  mutexLock(&g_lock);
  free(g_sounds[id].pcm);
  g_sounds[id] = (Sound){pcm, frames, channels, rate};
  mutexUnlock(&g_lock);
  return 0;
}

void fbf_audio_play(int id, float volume) {
  if (id < 0 || id >= FBF_SFX_COUNT || !g_sounds[id].pcm || !g_ao_ready)
    return;
  mutexLock(&g_lock);
  int slot = -1;
  for (int i = 0; i < MAX_VOICES && slot < 0; i++)
    if (g_voices[i].sound < 0)
      slot = i;
  if (slot < 0) { /* all busy: stop the oldest */
    slot = 0;
    for (int i = 1; i < MAX_VOICES; i++)
      if ((int32_t)(g_voices[i].age - g_voices[slot].age) < 0)
        slot = i;
  }
  Voice *v = &g_voices[slot];
  v->sound = id;
  v->pos = 0.0;
  v->step = (double)g_sounds[id].rate / (double)g_out_rate;
  v->gain = volume;
  v->age = ++g_started;
  mutexUnlock(&g_lock);
}

/* HOME: the voices hold where they are (Android pauses the output). */
void fbf_audio_pause(int paused) { rt_audout_pause(paused); }

/* One block: every playing voice, summed (the pump's fill function). */
static void mix(int16_t *out, int frames, void *ud) {
  (void)frames, (void)ud; /* always FRAMES_PER_BLOCK */
  static int32_t acc[FRAMES_PER_BLOCK * 2];
  memset(acc, 0, sizeof acc);
  mutexLock(&g_lock);
  for (int k = 0; k < MAX_VOICES; k++) {
    Voice *v = &g_voices[k];
    if (v->sound < 0)
      continue;
    const Sound *s = &g_sounds[v->sound];
    const float g = v->gain * g_master;
    for (int f = 0; f < FRAMES_PER_BLOCK; f++) {
      int i0 = (int)v->pos;
      if (i0 >= s->frames) {
        v->sound = -1;
        break;
      }
      const float t = (float)(v->pos - (double)i0);
      const int i1 = i0 + 1 < s->frames ? i0 + 1 : i0;
      float l, r;
      if (s->channels == 2) {
        l = s->pcm[i0 * 2] + (s->pcm[i1 * 2] - s->pcm[i0 * 2]) * t;
        r = s->pcm[i0 * 2 + 1] + (s->pcm[i1 * 2 + 1] - s->pcm[i0 * 2 + 1]) * t;
      } else {
        l = r = s->pcm[i0] + (s->pcm[i1] - s->pcm[i0]) * t;
      }
      acc[f * 2] += (int32_t)lrintf(l * g);
      acc[f * 2 + 1] += (int32_t)lrintf(r * g);
      v->pos += v->step;
    }
  }
  mutexUnlock(&g_lock);
  for (int i = 0; i < FRAMES_PER_BLOCK * 2; i++)
    out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
}

int fbf_audio_init(void) {
  mutexInit(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    g_voices[i].sound = -1;
  g_master = (float)dcr_config()->volume / 100.0f;
  if (rt_audout_open() != 0)
    return -1;
  g_out_rate = rt_audout_rate();
  g_ao_ready = 1;
  /* Above the game's own thread (priority 59, dcr_sched.c), so a block is
   * always ready before audout runs dry. */
  if (rt_audout_pump_start(mix, NULL, 0x2A, -2) != 0) {
    debugPrintf("[audio] mixer thread -- no sound\n");
    return -1;
  }
  debugPrintf("[audio] SoundPool: %d voices, mixed at %u Hz, volume %d%%\n", MAX_VOICES, g_out_rate,
              dcr_config()->volume);
  return 0;
}

void fbf_audio_shutdown(void) {
  rt_audout_pump_stop();
  rt_audout_close();
  debugPrintf("[audio] closed after %lu blocks (%lu underruns)\n", rt_audout_pump_blocks(), dcr_audio_underruns());
}
