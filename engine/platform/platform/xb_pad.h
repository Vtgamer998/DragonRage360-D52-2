/* SPDX-License-Identifier: MIT
 * Controllers of the Xbox 360 build. The game talks to a DualShock 2 through scePad2*; this module answers those
 * calls from the console's controllers (user index = game port: port 0 is controller 1, port 1 is controller 2). */
#ifndef XB_PAD_H
#define XB_PAD_H
#include <stdint.h>

/* What one controller reports, in the XInput layout (the console's native, big-endian structure). */
typedef struct XbPadState {
    int connected;
    uint16_t buttons;        /* XINPUT_GAMEPAD_* bits */
    uint8_t lt, rt;          /* 0..255 */
    int16_t lx, ly, rx, ry;  /* -32768..32767, y positive = up */
} XbPadState;

/* Where controller data comes from. The default reads XamInputGetState when built with -DXB_USE_XAM; any other
   source (a test, a recording, a different xsdk call) replaces it here. Returns 0 if the controller exists. */
extern int (*gXbPadPoll)(int index, XbPadState *out);
/* Where the game's motor commands go: left = large motor, right = small motor, 0..65535. */
extern void (*gXbPadRumble)(int index, uint16_t left, uint16_t right);

/* The bytes the game's scePad2Read gets for a state: 18 bytes, buttons active low, sticks centred at 0x80. Exposed
   so the mapping can be tested on a PC. */
void XbPad_Convert(const XbPadState *st, unsigned char *data);
#endif
