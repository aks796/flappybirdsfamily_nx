/* fbf_audio.c -- the game's SoundPool, played through audout.
 *
 * The engine makes no sound itself. It reports events (fbf_game.c), and the
 * Java plays one of six short sounds for each with a SoundPool built as
 * new SoundPool(10, STREAM_MUSIC, 100): up to 10 at once, each played with
 * play(id, vol, vol, priority 1, no loop, rate 1.0), vol = the music volume
 * (full here: the console's own volume applies after) and a third of it for
 * the point sound. When 10 are playing, SoundPool stops the oldest.
 *
 * Here: the sounds are decoded once (fbf_assets.c), and a mixer thread sums
 * the playing voices, resampled from 44.1 kHz to the device's 48 kHz
 * (linear), into 512-frame blocks for audout; three in flight is ~32 ms of
 * sound ahead, and a block is mixed just before it is queued, so a sound
 * starts within that.
 *
 * audout's buffer descriptor is an IPC structure with 64-bit fields for every
 * client, while libnx32's AudioOutBuffer has 32-bit pointers, so append and
 * get-released are issued with the right layout here (from the Crossy Road
 * port, where the self-test below proved it on hardware; the PvZ port uses
 * the same). Buffers are a page each; a block fills part of one (data_size),
 * as SDL's Switch audio does. MIT.
 */
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "fbf.h"
#include "util.h"

typedef struct {
  u64 next, buffer, buffer_size, data_size, data_offset;
} AoBuf;
_Static_assert(sizeof(AoBuf) == 0x28, "audout buffer descriptor");

#define NBUF 3
#define FRAMES_PER_BLOCK 512
#define BUF_BYTES 0x1000 /* one page: >= FRAMES_PER_BLOCK * 4 */
_Static_assert(FRAMES_PER_BLOCK * 4 <= BUF_BYTES, "a block fits its buffer");

static AoBuf g_bufs[NBUF] __attribute__((aligned(16)));
static int16_t *g_pcm[NBUF];
static int g_queued[NBUF];
static int g_ao_ready;
static u32 g_out_rate = 48000;

static Result ao_append(AoBuf *b) {
  u64 tag = (u64)(uintptr_t)b;
  const bool auto_ = hosversionAtLeast(3, 0, 0);
  return serviceDispatchIn(audoutGetServiceSession_AudioOut(), auto_ ? 7 : 3, tag,
                           .buffer_attrs = {auto_ ? (SfBufferAttr_HipcAutoSelect | SfBufferAttr_In)
                                                  : (SfBufferAttr_HipcMapAlias | SfBufferAttr_In)},
                           .buffers = {{b, sizeof(*b)}});
}

static Result ao_released(u64 *tags, u32 max, u32 *count) {
  const bool auto_ = hosversionAtLeast(3, 0, 0);
  return serviceDispatchOut(audoutGetServiceSession_AudioOut(), auto_ ? 8 : 5, *count,
                            .buffer_attrs = {auto_ ? (SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out)
                                                   : (SfBufferAttr_HipcMapAlias | SfBufferAttr_Out)},
                            .buffers = {{tags, max * sizeof(u64)}});
}

/* Mark the buffers the audio server has finished with as free again. */
static void reap(void) {
  u64 tags[NBUF] = {0};
  u32 n = 0;
  if (R_SUCCEEDED(ao_released(tags, NBUF, &n)))
    for (u32 k = 0; k < n && k < NBUF; k++)
      for (int i = 0; i < NBUF; i++)
        if (tags[k] == (u64)(uintptr_t)&g_bufs[i])
          g_queued[i] = 0;
}

static int free_buffer(void) {
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < NBUF; i++)
      if (!g_queued[i])
        return i;
    reap();
  }
  return -1;
}

static int ao_open(void) {
  if (g_ao_ready)
    return 0;
  Result rc = audoutInitialize();
  if (R_FAILED(rc)) {
    debugPrintf("[audio] audoutInitialize failed 0x%x\n", rc);
    return -1;
  }
  rc = audoutStartAudioOut();
  if (R_FAILED(rc)) {
    debugPrintf("[audio] audoutStartAudioOut failed 0x%x\n", rc);
    audoutExit();
    return -1;
  }
  g_out_rate = audoutGetSampleRate() ? audoutGetSampleRate() : 48000;
  for (int i = 0; i < NBUF; i++) {
    g_pcm[i] = memalign(0x1000, BUF_BYTES);
    if (!g_pcm[i])
      return -1;
    memset(g_pcm[i], 0, BUF_BYTES);
    g_bufs[i].buffer = (u64)(uintptr_t)g_pcm[i];
    g_bufs[i].buffer_size = BUF_BYTES;
    g_bufs[i].data_size = FRAMES_PER_BLOCK * 4;
  }
  g_ao_ready = 1;
  debugPrintf("[audio] audout open: %u Hz, %u ch; %d x %d-frame blocks\n", (unsigned)g_out_rate,
              (unsigned)audoutGetChannelCount(), NBUF, FRAMES_PER_BLOCK);
  return 0;
}

static unsigned long g_underruns, g_append_fails, g_dropped, g_submits;
static volatile int g_closing;

/* Queue one block of stereo s16; blocks while every buffer is in use. */
static void submit(const int16_t *frames) {
  int i;
  reap();
  int queued = 0;
  for (int k = 0; k < NBUF; k++)
    queued += g_queued[k];
  if (!queued && g_submits > NBUF)
    g_underruns++; /* audout had nothing left to play */
  while ((i = free_buffer()) < 0) {
    if (g_closing)
      return;
    svcSleepThread(1000000ll);
  }
  memcpy(g_pcm[i], frames, FRAMES_PER_BLOCK * 4);
  armDCacheFlush(g_pcm[i], FRAMES_PER_BLOCK * 4);
  g_bufs[i].data_size = FRAMES_PER_BLOCK * 4;
  g_bufs[i].data_offset = 0;
  for (int attempt = 0; attempt < 5; attempt++) {
    Result rc = ao_append(&g_bufs[i]);
    if (R_SUCCEEDED(rc)) {
      g_queued[i] = 1;
      g_submits++;
      return;
    }
    if (g_append_fails++ < 3)
      debugPrintf("[audio] audout append failed 0x%x (retrying)\n", (unsigned)rc);
    svcSleepThread(2000000ll);
    reap();
  }
  g_dropped++;
}

/* Self-check of the descriptor layout, before the game runs: blocks of
 * silence must come back from the audio server. */
void dcr_audio_selftest(void) {
  if (ao_open() != 0)
    return;
  static int16_t silence[FRAMES_PER_BLOCK * 2];
  submit(silence);
  submit(silence);
  u64 t0 = armGetSystemTick();
  int back = 0;
  while (armTicksToNs(armGetSystemTick() - t0) < 500000000ull) {
    reap();
    back = 0;
    for (int i = 0; i < NBUF; i++)
      back += !g_queued[i];
    if (back == NBUF)
      break;
    svcSleepThread(5000000ll);
  }
  debugPrintf("[audio] self-test: %s (%d/%d buffers returned in %llu ms)\n",
              back == NBUF ? "OK" : "FAILED -- buffer descriptor not accepted", back, NBUF,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

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
static volatile int g_paused;
static volatile uint32_t g_blocks;
static float g_master = 1.0f;
static Thread g_thread;
static int g_thread_up;

uint32_t dcr_audio_blocks(void) { return g_blocks; }
unsigned long dcr_audio_underruns(void) { return g_underruns; }

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

void fbf_audio_pause(int paused) { g_paused = paused; }

/* One block: every playing voice, summed. */
static void mix(int16_t *out) {
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

static void mixer(void *arg) {
  (void)arg;
  static int16_t block[FRAMES_PER_BLOCK * 2];
  while (!g_closing) {
    if (g_paused) {
      /* HOME: the voices hold where they are (Android pauses the output). */
      svcSleepThread(10000000ll);
      continue;
    }
    mix(block);
    submit(block); /* waits while three blocks are queued: the pacing */
    g_blocks++;
    if (g_blocks % 30000 == 0)
      debugPrintf("[audio] %lu blocks; %lu underruns, %lu failed submits (%lu dropped)\n",
                  (unsigned long)g_blocks, g_underruns, g_append_fails, g_dropped);
  }
}

int fbf_audio_init(void) {
  mutexInit(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    g_voices[i].sound = -1;
  g_master = (float)dcr_config()->volume / 100.0f;
  if (ao_open() != 0)
    return -1;
  /* Above the game's own thread (priority 59, dcr_sched.c), so a block is
   * always ready before audout runs dry. */
  Result rc = threadCreate(&g_thread, mixer, NULL, NULL, 0x8000, 0x2A, -2);
  if (R_SUCCEEDED(rc))
    rc = threadStart(&g_thread);
  if (R_FAILED(rc)) {
    debugPrintf("[audio] mixer thread: 0x%x -- no sound\n", rc);
    return -1;
  }
  g_thread_up = 1;
  debugPrintf("[audio] SoundPool: %d voices, mixed at %u Hz, volume %d%%\n", MAX_VOICES,
              (unsigned)g_out_rate, dcr_config()->volume);
  return 0;
}

void fbf_audio_shutdown(void) {
  g_closing = 1;
  if (g_thread_up) {
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_thread_up = 0;
  }
  if (g_ao_ready) {
    audoutStopAudioOut();
    audoutExit();
    g_ao_ready = 0;
  }
  debugPrintf("[audio] closed after %lu blocks (%lu underruns)\n", (unsigned long)g_blocks,
              g_underruns);
}
