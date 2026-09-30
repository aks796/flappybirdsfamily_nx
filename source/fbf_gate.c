/* fbf_gate.c -- GameActivity's one-key-per-device rule.
 *
 *   onKeyDown: if (l.get(deviceId, -1) != -1) return false;  // ignored
 *              l.put(deviceId, 0); ...                       // goes on
 *   onKeyUp:   l.delete(deviceId); ...                       // always goes on
 *
 * So while a device holds one key, its other key-downs never reach the
 * engine, and the first key-up of the device (any key) lets the next one
 * through. Plain C (tools/test_game.py runs it on the host). MIT. */
#include "fbf.h"

int fbf_gate_down(FbfKeyGate *g, int device) {
  if (device < 0 || device >= (int)(sizeof g->held / sizeof g->held[0]))
    return 1;
  if (g->held[device])
    return 0; /* SparseIntArray.get(deviceId) != -1: ignored */
  g->held[device] = 1;
  return 1;
}

void fbf_gate_up(FbfKeyGate *g, int device) {
  if (device >= 0 && device < (int)(sizeof g->held / sizeof g->held[0]))
    g->held[device] = 0; /* SparseIntArray.delete(deviceId) */
}

