/* SPDX-License-Identifier: MIT
 * The game's pad calls on Xbox 360: counterpart of the pad part of port/src/plat_stub.c and of Port_PadRead /
 * Port_PadVibrate in port/src/gs/gs_input.c. Only the controller path of the PC port is here: no keyboard, no
 * touch pad, no settings window, no per-button remap, no deadzone setting (the sticks are passed through, the
 * PC's default: the game applies its own dead zone), no "Square counts in clashes", no recording, no online.
 *
 * The PC port's default layout is kept: A = Cross, B = Circle, X = Square, Y = Triangle, LB/RB = L1/R1, LT/RT =
 * L2/R2 (pressed from 12000 of 32767, here 93 of 255), thumb clicks = L3/R3, Start = START, Back = SELECT.
 * The 12 pressure bytes of the report stay at zero, as in the PC port.
 *
 * TODO-VERIFICAR: XamInputGetState / XamInputSetState are the console's own exports (the XInput layer); the
 * arguments below are the ones I know (user index, flags, state). Check that xsdk exports them and how it
 * declares them: only xb_xam_poll / xb_xam_rumble change. XINPUT_STATE is read as is: the console is
 * big-endian, like the structure.
 */
#include <stdint.h>
#include <string.h>
#include "xb_pad.h"

#ifdef XB_USE_XAM
#include "xsdk/kernel.h"

static int xb_xam_poll(int index, XbPadState *out) {
    INPUT_STATE s;

    memset(&s, 0, sizeof(s));
    if (XamInputGetState((uint32_t)index, 0, &s) != 0) { /* 0 = ERROR_SUCCESS */
        return -1;
    }
    out->connected = 1;
    out->buttons = s.gamepad.buttons;
    out->lt = s.gamepad.left_trigger; out->rt = s.gamepad.right_trigger;
    out->lx = s.gamepad.thumb_lx; out->ly = s.gamepad.thumb_ly;
    out->rx = s.gamepad.thumb_rx; out->ry = s.gamepad.thumb_ry;
    return 0;
}
static void xb_xam_rumble(int index, uint16_t left, uint16_t right) {
    INPUT_VIBRATION v;

    v.left_motor_speed = left;
    v.right_motor_speed = right;
    XamInputSetState((uint32_t)index, 0, &v);
}
int (*gXbPadPoll)(int, XbPadState *) = xb_xam_poll;
void (*gXbPadRumble)(int, uint16_t, uint16_t) = xb_xam_rumble;
#else
int (*gXbPadPoll)(int, XbPadState *) = 0;
void (*gXbPadRumble)(int, uint16_t, uint16_t) = 0;
#endif

/* XINPUT_GAMEPAD_* */
enum { X_UP = 0x0001, X_DOWN = 0x0002, X_LEFT = 0x0004, X_RIGHT = 0x0008, X_START = 0x0010, X_BACK = 0x0020, X_LTHUMB = 0x0040,
       X_RTHUMB = 0x0080, X_LB = 0x0100, X_RB = 0x0200, X_A = 0x1000, X_B = 0x2000, X_X = 0x4000, X_Y = 0x8000 };
/* the game's bits (include/sys/pad.h): byte 0 low, byte 1 high, 1 = pressed here */
enum { B_SELECT = 0x0001, B_L3 = 0x0002, B_R3 = 0x0004, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040,
       B_LEFT = 0x0080, B_L2 = 0x0100, B_R2 = 0x0200, B_L1 = 0x0400, B_R1 = 0x0800, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000,
       B_CROSS = 0x4000, B_SQUARE = 0x8000 };
#define TRIGGER_AT 93

/* The PC's pass-through: (v + 32768) >> 8; the game's y is down = 0xFF, XInput's is up = positive. */
static unsigned char stick_x(int v) { return (unsigned char)((v + 32768) >> 8); }
static unsigned char stick_y(int v) {
    int d = v <= -32768 ? 32767 : -v; /* flipped */
    return (unsigned char)((d + 32768) >> 8);
}

void XbPad_Convert(const XbPadState *st, unsigned char *data) {
    unsigned b = 0;
    static const struct { unsigned x, g; } map[] = {
        {X_UP, B_UP}, {X_DOWN, B_DOWN}, {X_LEFT, B_LEFT}, {X_RIGHT, B_RIGHT}, {X_A, B_CROSS}, {X_B, B_CIRCLE}, {X_X, B_SQUARE},
        {X_Y, B_TRIANGLE}, {X_LB, B_L1}, {X_RB, B_R1}, {X_LTHUMB, B_L3}, {X_RTHUMB, B_R3}, {X_START, B_START}, {X_BACK, B_SELECT},
    };
    unsigned i;

    memset(data, 0, 18);
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (st->buttons & map[i].x) { b |= map[i].g; }
    }
    if (st->lt > TRIGGER_AT) { b |= B_L2; }
    if (st->rt > TRIGGER_AT) { b |= B_R2; }
    data[0] = (unsigned char)(~b & 0xFF); /* active low */
    data[1] = (unsigned char)(~(b >> 8) & 0xFF);
    data[2] = stick_x(st->rx); data[3] = stick_y(st->ry);
    data[4] = stick_x(st->lx); data[5] = stick_y(st->ly);
}

static void idle(unsigned char *data) {
    memset(data, 0, 18);
    data[0] = data[1] = 0xFF;
    data[2] = data[3] = data[4] = data[5] = 0x80;
}

int Port_PadRead(int socket, unsigned char *data) {
    XbPadState st;

    if (socket < 0 || socket > 1 || gXbPadPoll == 0) {
        return 0;
    }
    memset(&st, 0, sizeof(st));
    if (gXbPadPoll(socket, &st) != 0) {
        return 0;
    }
    if (!st.connected) { return 0; }
    XbPad_Convert(&st, data);
    return 18;
}

static int sPadSockets;
int sceDbcInit() { return 1; }
int scePad2Init() { sPadSockets=0;return 1; }
int scePad2CreateSocket() { return sPadSockets++; }
int scePad2GetState() { return 1; } /* connected and ready, as on the PC (a missing controller reads as idle) */
int scePad2GetButtonProfile(int socket, unsigned char *profile) {
    (void)socket;
    profile[0] = 0xFF; profile[1] = 0xFF; profile[2] = 0xFF; profile[3] = 0x03; /* the PC's DualShock 2 profile */
    return 4;
}

/* The last bytes the game read per pad (the PC keeps them for its pad table). */
static unsigned char sPadLast[2][18];
int scePad2Read(int socket, unsigned char *data) {
    if (Port_PadRead(socket, data) == 0) {
        idle(data);
    }
    if (socket >= 0 && socket < 2) {
        memcpy(sPadLast[socket], data, 18);
    }
    return 18;
}

int sceVibGetProfile() { return 0; }
/* Two bytes: bit 0 = small motor on/off, bits 1..7 = the large motor's power 0..127. */
int sceVibSetActParam(int socket, int profileSize, unsigned char *profile, int dataSize, unsigned char *data) {
    (void)profileSize; (void)profile;
    if (dataSize >= 1 && data != 0 && socket >= 0 && socket < 2 && gXbPadRumble != 0) {
        gXbPadRumble(socket, (uint16_t)((data[0] >> 1) * 65535 / 127), (uint16_t)((data[0] & 1) ? 65535 : 0));
    }
    return 0;
}
