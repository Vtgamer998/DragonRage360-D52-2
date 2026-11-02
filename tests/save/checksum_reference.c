/* SPDX-License-Identifier: AGPL-3.0-only; reference from z3xox and contributors. */
#include "common.h"
s32 McCard_Checksum(u32 *buf, s32 size, s32 write) {
    u32 sum = 0;
    u8 *p;
    s32 i;

    if (size <= 0) {
        return 1;
    }
    p = (u8 *)buf + 8;
    size -= 8;
    if (write == 1) {
        buf[0] = 0;
        buf[1] = 0;
    }
    for (i = 0; i < size; i++) {
        if (i & 1) {
            sum += *p << 8;
        } else {
            sum += *p;
        }
        p++;
        sum -= (sum > 0xFFFF) * 0x10000;
    }
    if (write == 1) {
        buf[0] = sum >> 8;
        buf[1] = sum & 0xFF;
    } else {
        if (buf[0] != sum >> 8 && buf[1] != (sum & 0xFF)) {
            return 1;
        }
    }
    return 0;
}
