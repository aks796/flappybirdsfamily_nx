/* tools/host/switch.h -- just enough of libnx's pad / touch interface for
 * tools/test_input.py to build source/fbf_input.c on a PC and drive it one
 * frame at a time. Not libnx: the controllers are the test's table
 * (host_pad[] by npad id), and the button values are libnx's. MIT. */
#ifndef HOST_SWITCH_H
#define HOST_SWITCH_H
#include <stdint.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef u32 Result;
#define R_FAILED(rc) ((rc) != 0)
#define BITL(n) (1ULL << (n))

enum {
  HidNpadButton_A = BITL(0), HidNpadButton_B = BITL(1), HidNpadButton_X = BITL(2),
  HidNpadButton_Y = BITL(3), HidNpadButton_StickL = BITL(4), HidNpadButton_StickR = BITL(5),
  HidNpadButton_L = BITL(6), HidNpadButton_R = BITL(7), HidNpadButton_ZL = BITL(8),
  HidNpadButton_ZR = BITL(9), HidNpadButton_Plus = BITL(10), HidNpadButton_Minus = BITL(11),
  HidNpadButton_Left = BITL(12), HidNpadButton_Up = BITL(13), HidNpadButton_Right = BITL(14),
  HidNpadButton_Down = BITL(15), HidNpadButton_LeftSL = BITL(24), HidNpadButton_LeftSR = BITL(25),
  HidNpadButton_RightSL = BITL(26), HidNpadButton_RightSR = BITL(27),
};
enum { HidNpadIdType_No1 = 0, HidNpadIdType_No2 = 1, HidNpadIdType_Handheld = 0x20 };
enum {
  HidNpadStyleTag_NpadFullKey = 1 << 0, HidNpadStyleTag_NpadHandheld = 1 << 1,
  HidNpadStyleTag_NpadJoyDual = 1 << 2, HidNpadStyleTag_NpadJoyLeft = 1 << 3,
  HidNpadStyleTag_NpadJoyRight = 1 << 4, HidNpadStyleTag_NpadGc = 1 << 5,
};
#define HidNpadStyleSet_NpadStandard 0x1f
enum { HidNpadJoyHoldType_Horizontal = 1 };
typedef int HidNpadIdType;

typedef struct {
  s32 x, y;
} HidAnalogStickState;

/* One controller as the test sets it. */
typedef struct {
  int connected;
  u32 style;
  u64 buttons;
  HidAnalogStickState sticks[2];
} HostPad;
extern HostPad host_pad_no1, host_pad_no2, host_pad_handheld;

typedef struct {
  int id;
  u64 cur, old;
  int connected;
  u32 style;
  HidAnalogStickState sticks[2];
} PadState;

static inline HostPad *host_pad_of(int id) {
  return id == HidNpadIdType_Handheld ? &host_pad_handheld : id == HidNpadIdType_No2 ? &host_pad_no2 : &host_pad_no1;
}
#define padInitialize(pad, first, ...) ((pad)->id = (first), (pad)->cur = (pad)->old = 0)
static inline void padConfigureInput(u32 max, u32 styles) { (void)max, (void)styles; }
static inline Result hidSetNpadJoyHoldType(int t) { (void)t; return 0; }
static inline void hidInitializeTouchScreen(void) {}
static inline void padUpdate(PadState *p) {
  HostPad *h = host_pad_of(p->id);
  p->old = p->cur;
  p->connected = h->connected;
  p->cur = h->connected ? h->buttons : 0;
  p->style = h->connected ? h->style : 0;
  p->sticks[0] = h->connected ? h->sticks[0] : (HidAnalogStickState){0, 0};
  p->sticks[1] = h->connected ? h->sticks[1] : (HidAnalogStickState){0, 0};
}
static inline int padIsConnected(const PadState *p) { return p->connected; }
static inline u32 padGetStyleSet(const PadState *p) { return p->style; }
static inline u64 padGetButtons(const PadState *p) { return p->cur; }
static inline u64 padGetButtonsDown(const PadState *p) { return p->cur & ~p->old; }
static inline u64 padGetButtonsUp(const PadState *p) { return ~p->cur & p->old; }
static inline HidAnalogStickState padGetStickPos(const PadState *p, int i) { return p->sticks[i]; }

extern u64 host_tick;
static inline u64 armGetSystemTick(void) { return host_tick; }
static inline u64 armGetSystemTickFreq(void) { return 19200000; }
static inline u64 armNsToTicks(u64 ns) { return (ns * 12) / 625; }

typedef struct {
  u32 finger_id, x, y;
} HidTouchState;
typedef struct {
  s32 count;
  HidTouchState touches[16];
} HidTouchScreenState;
extern HidTouchScreenState host_touch;
static inline int hidGetTouchScreenStates(HidTouchScreenState *s, int n) {
  (void)n;
  *s = host_touch;
  return 1;
}

#endif
