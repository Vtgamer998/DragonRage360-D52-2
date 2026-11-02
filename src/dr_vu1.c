/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Generated from dfdx047/z3xox gs_vu1.c, revision 3ca419c7966b11438e2dc3c9a7e1914f102ed545,
 * after patches/vu1-powerpc-endian.patch. See tools/build_vu_reference.py.
 */
/*
 * VU1: the vertex unit. The game uploads nine vertex programs (its own machine code for this unit) and, per
 * object, a block of matrices and vertices; the program transforms, lights and clips them and "kicks" finished
 * GIF packets to the GS. This file is an interpreter for the unit, so the ORIGINAL programs run unchanged:
 *     VIF1 UNPACK / MPG / MSCAL  ->  VU1 memory and program  ->  interpreter  ->  XGKICK  ->  GIF (gs_core.c)
 * It is the reference for the 3D scene, as gs_core.c's rasteriser is for the GS: once the picture is right, each
 * program can be replaced by a GPU vertex shader that is checked against it.
 *
 * Implemented: the instructions the nine programs use (see docs/port/README.md for the survey) and a few
 * neighbours; anything else is counted and reported once. Timing: the Q register's latency and the four-step
 * delay of the clipping flags are modelled (the programs read them), MAC / status flags are not (never read).
 * Arithmetic is the host's float unit with infinities and NaNs clamped: this is drawing, not simulation.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dr_vu1.h"
#include "dr_vu_ftoi.h"
#ifdef DR_XBOX360
#include <xsdk/log.h>
#endif
static DrVuGif gif_callback;
static void *gif_opaque;
static int kick_error;

typedef union Reg {
    float f[4];
    uint32_t u[4];
    int32_t i[4];
} Reg;

static struct {
    Reg vf[32], acc;
    uint16_t vi[16];
    float q, i, p;
    float qPending;
    uint32_t qReady, cycle;
    uint32_t clip;                      /* what the flag instructions see */
    uint32_t clipLatest;                /* clipping results including those still in the pipeline */
    struct { uint32_t cycle, value; } clipQueue[8];
    int clipHead, clipCount;
    uint32_t pc;                        /* in instructions */
    uint8_t mem[0x4000], micro[0x4000];
    /* VIF1 side */
    uint32_t top, tops, base, ofst, dbf, itop, itops, mode, mask, cl, wl, row[4], col[4];
} vu;

/* Canonical little-endian VU data/microcode bytes on every CPU. Registers
   remain native numeric values. Do not memcpy file bytes into native scalars. */
static uint32_t vu_le(const void *source, unsigned bytes) {
    const uint8_t *p = source;
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < bytes; i++) value |= (uint32_t)p[i] << (i * 8);
    return value;
}
static void vu_store(void *target, uint32_t value) {
    uint8_t *p = target;
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}
static void vu_load_reg(Reg *target, const void *source) {
    const uint8_t *p = source;
    unsigned i;
    for (i = 0; i < 4; i++) target->u[i] = vu_le(p + i * 4, 4);
}
static void vu_store_vector(void *target, const uint32_t *values) {
    uint8_t *p = target;
    unsigned i;
    for (i = 0; i < 4; i++) vu_store(p + i * 4, values[i]);
}

static unsigned sUnknown[2][128];
static unsigned sStatRuns, sStatInstr, sStatKicks;
static unsigned sProgSize;
static uint32_t trace_active,trace_kick;
void dr_vu1_trace(DrVuTrace *t){if(t){*t=(DrVuTrace){trace_active,sProgSize,vu.pc,trace_kick,vu.top,0};memcpy(&t->q_bits,&vu.q,4);}}

static struct { unsigned size, runs, instr, kicks; } sProg[16]; /* per loaded program, this frame */

static inline float clampf(float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    if ((u & 0x7F800000u) == 0x7F800000u) {
        u = (u & 0x80000000u) | 0x7F7FFFFFu;
        memcpy(&v, &u, 4);
    }
    return v;
}

/* ---------------------------------------------------------------------------------------------- VIF1 side */

static void legacy_setcycle(uint32_t cl, uint32_t wl) { vu.cl = cl; vu.wl = wl; }
static void legacy_setbase(uint32_t v) { vu.base = v & 0x3FF; }
static void legacy_setoffset(uint32_t v) { vu.ofst = v & 0x3FF; vu.dbf = 0; vu.tops = vu.base; }
static void legacy_setitop(uint32_t v) { vu.itops = v & 0x3FF; }
static void legacy_setmode(uint32_t v) { vu.mode = v & 3; }
static void legacy_setmask(uint32_t v) { vu.mask = v; }
static void legacy_setrow(const void *v) { unsigned i; for (i = 0; i < 4; i++) vu.row[i] = vu_le((const uint8_t *)v + i * 4, 4); }
static void legacy_setcol(const void *v) { unsigned i; for (i = 0; i < 4; i++) vu.col[i] = vu_le((const uint8_t *)v + i * 4, 4); }

static void legacy_program(uint32_t addr, const void *words, uint32_t count) {
    uint32_t i;
    /* which program is loaded: identified by its size in instructions (an upload at 0 starts a new one) */
    sProgSize = addr == 0 ? count / 2 : sProgSize + count / 2;
    for (i = 0; i < count; i++) {
        memcpy(&vu.micro[((addr * 8) + i * 4) & 0x3FFC], (const uint8_t *)words + i * 4, 4);
    }
}

/* Words of data an UNPACK command with these parameters is followed by. */
static uint32_t legacy_unpackwords(uint32_t cmd, uint32_t num) {
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, n = num ? num : 256;
    uint32_t bits = vn == 3 && vl == 3 ? 16 : (32u >> vl) * (vn + 1);

    if (vu.wl > vu.cl && vu.wl != 0) {
        n = vu.cl * (n / vu.wl) + (n % vu.wl > vu.cl ? vu.cl : n % vu.wl);
    }
    return (n * bits + 31) / 32;
}

static void legacy_unpack(uint32_t cmd, uint32_t num, uint32_t imm, const void *data) {
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, masked = (cmd >> 4) & 1, usn = (imm >> 14) & 1;
    uint32_t addr = (imm & 0x3FF) + ((imm >> 15) & 1 ? vu.tops : 0), n = num ? num : 256;
    uint32_t cl = vu.cl ? vu.cl : 1, wl = vu.wl ? vu.wl : 1, cyc = 0, bitpos = 0, k, c;
    const uint8_t *src = (const uint8_t *)data;

    /* The usual case by far (every vertex of every model): no mask, no row arithmetic, consecutive vectors. The
       same result as the general loop below, one tight loop per format. */
    if (!masked && vu.mode == 0 && cl == wl && !(vn == 3 && vl == 3) && vn != 0 && vl != 3) {
        uint32_t comps = vn + 1;
        if (vn == 3 && vl == 0) { /* four words per vector, which is all the game's models and effects send: a copy */
            for (k = 0; k < n; k++) {
                uint32_t at = (addr + k) & 0x3FF, run = 0x400 - at;
                if (run > n - k) {
                    run = n - k;
                }
                memcpy(&vu.mem[at * 16], src + k * 16, run * 16);
                k += run - 1;
            }
            return;
        }
        for (k = 0; k < n; k++) {
            uint32_t out[4] = {0, 0, 0, 0};
            if (vl == 0) {
                for (c = 0; c < comps; c++) {
                    out[c] = vu_le(src + c * 4, 4);
                }
                src += comps * 4;
            } else if (vl == 1) {
                for (c = 0; c < comps; c++) {
                    uint16_t w;
                    w = (uint16_t)vu_le(src + c * 2, 2);
                    out[c] = usn ? (uint32_t)w : (uint32_t)(int32_t)(int16_t)w;
                }
                src += comps * 2;
            } else {
                for (c = 0; c < comps; c++) {
                    out[c] = usn ? (uint32_t)src[c] : (uint32_t)(int32_t)(int8_t)src[c];
                }
                src += comps;
            }
            vu_store_vector(&vu.mem[((addr + k) & 0x3FF) * 16], out);
        }
        return;
    }
    for (k = 0; k < n; k++) {
        uint32_t v[4] = {0, 0, 0, 0}, out[4];
        int have = !(wl > cl && cyc >= cl); /* filling write: vectors beyond CL come from the registers only */

        if (have) {
            if (vn == 3 && vl == 3) {
                uint32_t w = src[bitpos / 8] | src[bitpos / 8 + 1] << 8;
                v[0] = (w & 31) << 3; v[1] = ((w >> 5) & 31) << 3; v[2] = ((w >> 10) & 31) << 3; v[3] = ((w >> 15) & 1) << 7;
                bitpos += 16;
            } else {
                uint32_t size = 32u >> vl;
                for (c = 0; c <= vn; c++) {
                    uint32_t w = 0;
                    w = vu_le(&src[bitpos / 8], size / 8);
                    if (!usn && size == 16) { w = (uint32_t)(int32_t)(int16_t)w; }
                    if (!usn && size == 8) { w = (uint32_t)(int32_t)(int8_t)w; }
                    v[c] = w;
                    bitpos += size;
                }
                if (vn == 0) {
                    v[1] = v[2] = v[3] = v[0];
                }
            }
        }
        for (c = 0; c < 4; c++) {
            uint32_t m = masked ? (vu.mask >> (2 * ((cyc > 3 ? 3 : cyc) * 4 + c))) & 3 : 0;
            if (m == 0) {
                out[c] = v[c];
                if (vu.mode == 1) {
                    out[c] = v[c] + vu.row[c];
                } else if (vu.mode == 2) {
                    vu.row[c] += v[c];
                    out[c] = vu.row[c];
                }
            } else if (m == 1) {
                out[c] = vu.row[c];
            } else if (m == 2) {
                out[c] = vu.col[cyc > 3 ? 3 : cyc];
            } else {
                out[c] = vu_le(&vu.mem[((addr & 0x3FF) * 16) + c * 4], 4);
            }
        }
        vu_store_vector(&vu.mem[(addr & 0x3FF) * 16], out);
        addr++;
        if (++cyc == (wl > cl ? wl : wl)) {
            if (cl > wl) {
                addr += cl - wl;
            }
            cyc = 0;
        }
    }
}

/* ---------------------------------------------------------------------------------------------- interpreter */

static void kick(uint32_t addr) {
    uint8_t buf[0x4000];
    uint32_t a = (addr & 0x3FF) * 16, len = 0;

    sStatKicks++;
    /* the packet may wrap around the end of VU memory; hand the GIF interpreter one tag at a time until EOP */
    for (;;) {
        uint64_t lo;
        uint32_t nloop, flg, nreg, qw;
        lo = (uint64_t)vu_le(&vu.mem[a & 0x3FFF], 4) | (uint64_t)vu_le(&vu.mem[(a + 4) & 0x3FFF], 4) << 32;
        nloop = lo & 0x7FFF;
        flg = (lo >> 58) & 3;
        nreg = (lo >> 60) & 15;
        if (nreg == 0) { nreg = 16; }
        qw = 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if (len + qw * 16 > sizeof(buf)) {
            kick_error = 1;
            return;
        }
        for (; qw != 0; qw--, a += 16, len += 16) {
            memcpy(&buf[len], &vu.mem[a & 0x3FFF], 16);
        }
        if ((lo >> 15) & 1) {
            break;
        }
    }
    if (gif_callback != NULL){trace_active=1;trace_kick=addr;gif_callback(gif_opaque, buf, len / 16);trace_active=0;}
}

#define DEST(n) ((dest >> (3 - (n))) & 1) /* field order in the instruction: x y z w */

/* Runs the program at instruction `start` until its end (E bit). */
static int run(uint32_t start, uint32_t budget) {
    int ending = 0, branch = 0;
    uint32_t target = 0, guard;

    unsigned slot, before = sStatInstr, kbefore = sStatKicks;

    vu.pc = start & 0x7FF;
    sStatRuns++;
    for (slot = 0; slot < 15 && sProg[slot].size != 0 && sProg[slot].size != sProgSize; slot++) {
    }
    sProg[slot].size = sProgSize;
    sProg[slot].runs++;
    for (guard = 0; guard < budget; guard++) {
        uint32_t lo, hi, dest, ft, fs, fd, op, bc, c;
        Reg res, *wr = NULL;
        uint32_t wmask = 0;
        int wacc = 0, lower = 1;
        uint32_t next = (vu.pc + 1) & 0x7FF;

        lo = vu_le(&vu.micro[vu.pc * 8], 4);
        hi = vu_le(&vu.micro[vu.pc * 8 + 4], 4);
        vu.cycle++;
        sStatInstr++;
        if (vu.qReady != 0 && vu.cycle >= vu.qReady) {
            vu.q = vu.qPending;
            vu.qReady = 0;
        }
        while (vu.clipCount != 0 && vu.clipQueue[vu.clipHead].cycle <= vu.cycle) {
            vu.clip = vu.clipQueue[vu.clipHead].value;
            vu.clipHead = (vu.clipHead + 1) & 7;
            vu.clipCount--;
        }
        dest = (hi >> 21) & 15;
        ft = (hi >> 16) & 31;
        fs = (hi >> 11) & 31;
        fd = (hi >> 6) & 31;
        op = hi & 0x3F;
        bc = hi & 3;
        if (hi & 0x80000000u) { /* I bit: the lower word is a float for the I register */
            memcpy(&vu.i, &lo, 4);
            lower = 0;
        }
        /* ---- upper instruction: result into `res`, committed after the lower one has read its operands ---- */
#define A(c) vu.vf[fs].f[c]
#define B(c) vu.vf[ft].f[c]
#define BC vu.vf[ft].f[bc]
#define EACH for (c = 0; c < 4; c++) if (DEST(c))
#define TO_FD wr = &vu.vf[fd]; wmask = dest
#define TO_ACC wacc = 1; wmask = dest
        if (hi == 0x000002FFu) {
            goto upper_done; /* NOP: three quarters of the programs' upper instructions */
        }
        res = op >= 0x3C ? vu.acc : vu.vf[fd];
        if (op < 0x3C) {
            switch (op) {
            case 0x00: case 0x01: case 0x02: case 0x03: EACH res.f[c] = clampf(A(c) + BC); TO_FD; break;
            case 0x04: case 0x05: case 0x06: case 0x07: EACH res.f[c] = clampf(A(c) - BC); TO_FD; break;
            case 0x08: case 0x09: case 0x0A: case 0x0B: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * BC); TO_FD; break;
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * BC); TO_FD; break;
            case 0x10: case 0x11: case 0x12: case 0x13: EACH res.f[c] = A(c) > BC ? A(c) : BC; TO_FD; break;
            case 0x14: case 0x15: case 0x16: case 0x17: EACH res.f[c] = A(c) < BC ? A(c) : BC; TO_FD; break;
            case 0x18: case 0x19: case 0x1A: case 0x1B: EACH res.f[c] = clampf(A(c) * BC); TO_FD; break;
            case 0x1C: EACH res.f[c] = clampf(A(c) * vu.q); TO_FD; break;
            case 0x1D: EACH res.f[c] = A(c) > vu.i ? A(c) : vu.i; TO_FD; break;
            case 0x1E: EACH res.f[c] = clampf(A(c) * vu.i); TO_FD; break;
            case 0x1F: EACH res.f[c] = A(c) < vu.i ? A(c) : vu.i; TO_FD; break;
            case 0x20: EACH res.f[c] = clampf(A(c) + vu.q); TO_FD; break;
            case 0x21: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * vu.q); TO_FD; break;
            case 0x22: EACH res.f[c] = clampf(A(c) + vu.i); TO_FD; break;
            case 0x23: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * vu.i); TO_FD; break;
            case 0x24: EACH res.f[c] = clampf(A(c) - vu.q); TO_FD; break;
            case 0x25: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * vu.q); TO_FD; break;
            case 0x26: EACH res.f[c] = clampf(A(c) - vu.i); TO_FD; break;
            case 0x27: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * vu.i); TO_FD; break;
            case 0x28: EACH res.f[c] = clampf(A(c) + B(c)); TO_FD; break;
            case 0x29: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * B(c)); TO_FD; break;
            case 0x2A: EACH res.f[c] = clampf(A(c) * B(c)); TO_FD; break;
            case 0x2B: EACH res.f[c] = A(c) > B(c) ? A(c) : B(c); TO_FD; break;
            case 0x2C: EACH res.f[c] = clampf(A(c) - B(c)); TO_FD; break;
            case 0x2D: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * B(c)); TO_FD; break;
            case 0x2E: /* OPMSUB: cross product, second half */
                res.f[0] = clampf(vu.acc.f[0] - A(1) * B(2)); res.f[1] = clampf(vu.acc.f[1] - A(2) * B(0));
                res.f[2] = clampf(vu.acc.f[2] - A(0) * B(1)); wr = &vu.vf[fd]; wmask = 0xE; break;
            case 0x2F: EACH res.f[c] = A(c) < B(c) ? A(c) : B(c); TO_FD; break;
            default: sUnknown[0][op]++; break;
            }
        } else {
            uint32_t sp = (fd << 2) | bc;
            switch (sp) {
            case 0x00: case 0x01: case 0x02: case 0x03: EACH res.f[c] = clampf(A(c) + BC); TO_ACC; break;
            case 0x04: case 0x05: case 0x06: case 0x07: EACH res.f[c] = clampf(A(c) - BC); TO_ACC; break;
            case 0x08: case 0x09: case 0x0A: case 0x0B: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * BC); TO_ACC; break;
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * BC); TO_ACC; break;
            case 0x10: res = vu.vf[ft]; EACH res.f[c] = (float)vu.vf[fs].i[c]; wr = &vu.vf[ft]; wmask = dest; break;
            case 0x11: res = vu.vf[ft]; EACH res.f[c] = (float)vu.vf[fs].i[c] * (1.0f / 16.0f); wr = &vu.vf[ft]; wmask = dest; break;
            case 0x12: res = vu.vf[ft]; EACH res.f[c] = (float)vu.vf[fs].i[c] * (1.0f / 4096.0f); wr = &vu.vf[ft]; wmask = dest; break;
            case 0x13: res = vu.vf[ft]; EACH res.f[c] = (float)vu.vf[fs].i[c] * (1.0f / 32768.0f); wr = &vu.vf[ft]; wmask = dest; break;
            case 0x14: case 0x15: case 0x16: case 0x17: {
                static const unsigned fraction[4]={0,4,12,15};
                res = vu.vf[ft];
                EACH res.u[c]=dr_vu_ftoi(vu.vf[fs].u[c],fraction[sp-0x14]);
                wr = &vu.vf[ft]; wmask = dest; break;
            }
            case 0x18: case 0x19: case 0x1A: case 0x1B: EACH res.f[c] = clampf(A(c) * BC); TO_ACC; break;
            case 0x1C: EACH res.f[c] = clampf(A(c) * vu.q); TO_ACC; break;
            case 0x1D: res = vu.vf[ft]; EACH res.u[c] = vu.vf[fs].u[c] & 0x7FFFFFFFu; wr = &vu.vf[ft]; wmask = dest; break;
            case 0x1E: EACH res.f[c] = clampf(A(c) * vu.i); TO_ACC; break;
            case 0x1F: { /* CLIP: fs.xyz against +/-|ft.w| */
                float w = fabsf(B(3));
                uint32_t f = (A(0) > w) | (A(0) < -w) << 1 | (A(1) > w) << 2 | (A(1) < -w) << 3 | (A(2) > w) << 4 | (A(2) < -w) << 5;
                int slot = (vu.clipHead + vu.clipCount) & 7;
                vu.clipLatest = ((vu.clipLatest << 6) | f) & 0xFFFFFF;
                vu.clipQueue[slot].cycle = vu.cycle + 4;
                vu.clipQueue[slot].value = vu.clipLatest;
                if (vu.clipCount < 8) { vu.clipCount++; }
                break;
            }
            case 0x20: EACH res.f[c] = clampf(A(c) + vu.q); TO_ACC; break;
            case 0x21: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * vu.q); TO_ACC; break;
            case 0x22: EACH res.f[c] = clampf(A(c) + vu.i); TO_ACC; break;
            case 0x23: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * vu.i); TO_ACC; break;
            case 0x24: EACH res.f[c] = clampf(A(c) - vu.q); TO_ACC; break;
            case 0x26: EACH res.f[c] = clampf(A(c) - vu.i); TO_ACC; break;
            case 0x28: EACH res.f[c] = clampf(A(c) + B(c)); TO_ACC; break;
            case 0x29: EACH res.f[c] = clampf(vu.acc.f[c] + A(c) * B(c)); TO_ACC; break;
            case 0x2A: EACH res.f[c] = clampf(A(c) * B(c)); TO_ACC; break;
            case 0x2C: EACH res.f[c] = clampf(A(c) - B(c)); TO_ACC; break;
            case 0x2D: EACH res.f[c] = clampf(vu.acc.f[c] - A(c) * B(c)); TO_ACC; break;
            case 0x2E: /* OPMULA: cross product, first half */
                res.f[0] = clampf(A(1) * B(2)); res.f[1] = clampf(A(2) * B(0)); res.f[2] = clampf(A(0) * B(1)); wacc = 1; wmask = 0xE; break;
            case 0x2F: break; /* NOP */
            default: sUnknown[0][64 + (sp & 63)]++; break;
            }
        }
upper_done:
        /* ---- lower instruction ---- */
        if (lower && lo != 0x8000033Cu) { /* 0x8000033C: the lower NOP (move vf0, vf0) */
            uint32_t lop = lo >> 25, it = (lo >> 16) & 31, is = (lo >> 11) & 31, id = (lo >> 6) & 31, ld = (lo >> 21) & 15;
            int32_t imm11 = (int32_t)(lo << 21) >> 21;
            uint32_t imm15 = ((lo >> 10) & 0x7800) | (lo & 0x7FF);
#define LDEST(n) ((ld >> (3 - (n))) & 1)
#define MEM(a) (&vu.mem[((uint32_t)(a) & 0x3FF) * 16])
#define SETVI(r, v) do { if ((r) & 15) { vu.vi[(r) & 15] = (uint16_t)(v); } } while (0)
            switch (lop) {
            case 0x00: { Reg m; vu_load_reg(&m, MEM((int32_t)vu.vi[is & 15] + imm11)); for (c = 0; c < 4; c++) if (LDEST(c) && it) vu.vf[it].u[c] = m.u[c]; break; }
            case 0x01: { uint8_t *m = MEM((int32_t)vu.vi[it & 15] + imm11); for (c = 0; c < 4; c++) { if (LDEST(c)) { vu_store(m + c * 4, vu.vf[is].u[c]); } } break; }
            case 0x04: { uint8_t *m = MEM((int32_t)vu.vi[is & 15] + imm11); for (c = 0; c < 4; c++) if (LDEST(c)) { uint32_t w; w = vu_le(m + c * 4, 4); SETVI(it, w); } break; }
            case 0x05: { uint8_t *m = MEM((int32_t)vu.vi[is & 15] + imm11); for (c = 0; c < 4; c++) if (LDEST(c)) { uint32_t w = vu.vi[it & 15]; vu_store(m + c * 4, w); } break; }
            case 0x08: SETVI(it, vu.vi[is & 15] + imm15); break;
            case 0x09: SETVI(it, vu.vi[is & 15] - imm15); break;
            case 0x10: SETVI(1, (vu.clip & 0xFFFFFF) == (lo & 0xFFFFFF)); break;
            case 0x11: vu.clip = vu.clipLatest = lo & 0xFFFFFF; vu.clipCount = 0; break;
            case 0x12: SETVI(1, (vu.clip & lo & 0xFFFFFF) != 0); break;
            case 0x13: SETVI(1, ((vu.clip | lo) & 0xFFFFFF) == 0xFFFFFF); break;
            case 0x1C: SETVI(it, vu.clip & 0xFFF); break;
            case 0x20: branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; break;
            case 0x21: SETVI(it, (vu.pc + 2) & 0x7FF); branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; break;
            case 0x24: branch = 2; target = vu.vi[is & 15] & 0x7FF; break; /* JR: the register holds an instruction number */
            case 0x25: { uint32_t t = vu.vi[is & 15] & 0x7FF; SETVI(it, (vu.pc + 2) & 0x7FF); branch = 2; target = t; break; }
            case 0x28: if (vu.vi[it & 15] == vu.vi[is & 15]) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x29: if (vu.vi[it & 15] != vu.vi[is & 15]) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x2C: if ((int16_t)vu.vi[is & 15] < 0) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x2D: if ((int16_t)vu.vi[is & 15] > 0) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x2E: if ((int16_t)vu.vi[is & 15] <= 0) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x2F: if ((int16_t)vu.vi[is & 15] >= 0) { branch = 2; target = (vu.pc + 1 + (uint32_t)imm11) & 0x7FF; } break;
            case 0x40: {
                uint32_t f = lo & 0x3F;
                if (f < 0x3C) {
                    switch (f) {
                    case 0x30: SETVI(id, vu.vi[is & 15] + vu.vi[it & 15]); break;
                    case 0x31: SETVI(id, vu.vi[is & 15] - vu.vi[it & 15]); break;
                    case 0x32: SETVI(it, vu.vi[is & 15] + (uint32_t)((int32_t)(lo << 21) >> 27)); break;
                    case 0x34: SETVI(id, vu.vi[is & 15] & vu.vi[it & 15]); break;
                    case 0x35: SETVI(id, vu.vi[is & 15] | vu.vi[it & 15]); break;
                    default: sUnknown[1][f]++; break;
                    }
                } else {
                    uint32_t sp = (id << 2) | (lo & 3), fsf = (lo >> 21) & 3, ftf = (lo >> 23) & 3;
                    switch (sp) {
                    case 0x30: if (it) { for (c = 0; c < 4; c++) if (LDEST(c)) vu.vf[it].u[c] = vu.vf[is].u[c]; } break; /* MOVE */
                    case 0x31: if (it) { Reg s = vu.vf[is]; if (LDEST(0)) vu.vf[it].u[0] = s.u[1]; if (LDEST(1)) vu.vf[it].u[1] = s.u[2];
                                         if (LDEST(2)) { vu.vf[it].u[2] = s.u[3]; } if (LDEST(3)) { vu.vf[it].u[3] = s.u[0]; } } break; /* MR32 */
                    case 0x34: { Reg m; vu_load_reg(&m, MEM(vu.vi[is & 15])); if (it) { for (c = 0; c < 4; c++) if (LDEST(c)) vu.vf[it].u[c] = m.u[c]; }
                                 SETVI(is, vu.vi[is & 15] + 1); break; } /* LQI */
                    case 0x35: { uint8_t *m = MEM(vu.vi[it & 15]); for (c = 0; c < 4; c++) { if (LDEST(c)) { vu_store(m + c * 4, vu.vf[is].u[c]); } }
                                 SETVI(it, vu.vi[it & 15] + 1); break; } /* SQI */
                    case 0x36: { Reg m; SETVI(is, vu.vi[is & 15] - 1); vu_load_reg(&m, MEM(vu.vi[is & 15]));
                                 if (it) { for (c = 0; c < 4; c++) if (LDEST(c)) vu.vf[it].u[c] = m.u[c]; } break; } /* LQD */
                    case 0x37: { uint8_t *m; SETVI(it, vu.vi[it & 15] - 1); m = MEM(vu.vi[it & 15]);
                                 for (c = 0; c < 4; c++) { if (LDEST(c)) { vu_store(m + c * 4, vu.vf[is].u[c]); } } break; } /* SQD */
                    case 0x38: { float d = vu.vf[it].f[ftf], n = vu.vf[is].f[fsf]; /* DIV */
                                 vu.qPending = d != 0.0f ? clampf(n / d) : ((n < 0.0f) != (d < 0.0f) || (n < 0.0f && d == 0.0f) ? -3.4028235e38f : 3.4028235e38f);
                                 vu.qReady = vu.cycle + 7; break; }
                    case 0x39: vu.qPending = sqrtf(fabsf(vu.vf[it].f[ftf])); vu.qReady = vu.cycle + 7; break;
                    case 0x3A: { float d = sqrtf(fabsf(vu.vf[it].f[ftf])); vu.qPending = d != 0.0f ? clampf(vu.vf[is].f[fsf] / d) : 3.4028235e38f;
                                 vu.qReady = vu.cycle + 13; break; }
                    case 0x3B: if (vu.qReady != 0) { vu.q = vu.qPending; vu.qReady = 0; } break; /* WAITQ */
                    case 0x3C: SETVI(it, vu.vf[is].u[fsf]); break;                                /* MTIR */
                    case 0x3D: if (it) { for (c = 0; c < 4; c++) if (LDEST(c)) vu.vf[it].i[c] = (int16_t)vu.vi[is & 15]; } break; /* MFIR */
                    case 0x3E: { uint8_t *m = MEM(vu.vi[is & 15]); for (c = 0; c < 4; c++) if (LDEST(c)) { uint32_t w; w = vu_le(m + c * 4, 4); SETVI(it, w); } break; } /* ILWR */
                    case 0x3F: { uint8_t *m = MEM(vu.vi[is & 15]); for (c = 0; c < 4; c++) if (LDEST(c)) { uint32_t w = vu.vi[it & 15]; vu_store(m + c * 4, w); } break; } /* ISWR */
                    case 0x64: if (it) { for (c = 0; c < 4; c++) if (LDEST(c)) vu.vf[it].f[c] = vu.p; } break; /* MFP */
                    case 0x68: SETVI(it, vu.top); break;   /* XTOP */
                    case 0x69: SETVI(it, vu.itop); break;  /* XITOP */
                    case 0x6C: kick(vu.vi[is & 15]); break; /* XGKICK */
                    case 0x7B: break;                       /* WAITP */
                    default: sUnknown[1][64 + (sp & 63)]++; break;
                    }
                }
                break;
            }
            default: sUnknown[1][lop & 63]++; break;
            }
        }
        /* ---- commit the upper instruction's result ---- */
        if (wr != NULL && wr != &vu.vf[0]) {
            for (c = 0; c < 4; c++) if ((wmask >> (3 - c)) & 1) wr->u[c] = res.u[c];
        } else if (wacc) {
            for (c = 0; c < 4; c++) if ((wmask >> (3 - c)) & 1) vu.acc.u[c] = res.u[c];
        }
        vu.vf[0].f[0] = vu.vf[0].f[1] = vu.vf[0].f[2] = 0.0f;
        vu.vf[0].f[3] = 1.0f;
        /* ---- next instruction: branches take effect after one more instruction; so does the end ---- */
        if (ending) {
            vu.pc = next;
            sProg[slot].instr += sStatInstr - before;
            sProg[slot].kicks += sStatKicks - kbefore;
            return DR_VU_OK;
        }
        if (hi & 0x40000000u) {
            ending = 1;
        }
        if (branch == 1) {
            vu.pc = target;
            branch = 0;
        } else {
            if (branch == 2) {
                branch = 1;
            }
            vu.pc = next;
        }
    }
#ifdef DR_XBOX360
    xsdk_log_write("VU budget exhausted start=%u pc=%u prog_instructions=%u top=%u tops=%u base=%u ofst=%u itop=%u",start,vu.pc,sProgSize,vu.top,vu.tops,vu.base,vu.ofst,vu.itop);
    xsdk_log_write("VU vi: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u; top2=%08x",vu.vi[1],vu.vi[2],vu.vi[3],vu.vi[4],vu.vi[5],vu.vi[6],vu.vi[7],vu.vi[8],vu.vi[9],vu.vi[10],vu.vi[11],vu.vi[12],vu.vi[13],vu.vi[14],vu.vi[15],vu_le(vu.mem+(((vu.top+2)&1023)*16),4));
#endif
    return DR_VU_BUDGET;
}

/* Checked interface. Drawing state only; not a combat snapshot/rollback API. */
#include "dr_vu1_stage.inc"
#include "dr_vu1_model.inc"
void dr_vu1_reset(DrVuGif callback, void *opaque) {
    memset(&vu, 0, sizeof(vu));
    memset(sUnknown, 0, sizeof(sUnknown));
    memset(sProg, 0, sizeof(sProg));
    sStatRuns = sStatInstr = sStatKicks = sProgSize = 0;
    stage_active=stage_header=stage_since_vm=0;stage_draws=stage_fallbacks=0;stage_dirty=1;model_dirty=1;model_active=model_since_vm=0;model_draws=model_fallbacks=0;
    vu.cl = vu.wl = 1;
    vu.vf[0].f[3] = 1.0f;
    gif_callback = callback; gif_opaque = opaque; kick_error = 0;
}
int dr_vu1_control(uint32_t cl, uint32_t wl, uint32_t mode, uint32_t mask, const void *row16, const void *col16) {
    if (!cl || !wl || cl > 256 || wl > 256 || mode > 2 || row16 == NULL || col16 == NULL) return DR_VU_INVALID;
    legacy_setcycle(cl, wl); legacy_setmode(mode); legacy_setmask(mask);
    legacy_setrow(row16); legacy_setcol(col16);
    return DR_VU_OK;
}
int dr_vu1_address(uint32_t base, uint32_t offset, uint32_t itop) {
    if (base > 1023 || offset > 1023 || itop > 1023) return DR_VU_INVALID;
    legacy_setbase(base); legacy_setoffset(offset); legacy_setitop(itop);
    return DR_VU_OK;
}
int dr_vu1_vif_control(uint32_t code,const void *payload,size_t bytes) {
    uint32_t cmd=(code>>24)&127,imm=code&65535;
    switch(cmd) {
    case 1:
        if(bytes)return DR_VU_INVALID;
        legacy_setcycle((imm&255)?imm&255:256,(imm>>8)?imm>>8:256);break;
    case 2: case 3: case 4:
        if(bytes || imm>1023)return DR_VU_INVALID;
        if(cmd==2)legacy_setoffset(imm);
        else if(cmd==3)legacy_setbase(imm);
        else legacy_setitop(imm);
        break;
    case 5:
        if(bytes || imm>2)return DR_VU_INVALID;
        legacy_setmode(imm);break;
    case 0x20:
        if(!payload || bytes!=4)return DR_VU_INVALID;
        legacy_setmask(vu_le(payload,4));break;
    case 0x30: case 0x31:
        if(!payload || bytes!=16)return DR_VU_INVALID;
        if(cmd==0x30)legacy_setrow(payload);else legacy_setcol(payload);
        break;
    default:return DR_VU_INVALID;
    }
    return 0;
}
int dr_vu1_program(uint32_t address, const void *bytes, size_t size) {
    if (address > 2047 || bytes == NULL || !size || size > 16384 || size % 8) return DR_VU_INVALID;
    legacy_program(address, bytes, (uint32_t)(size / 4));
    stage_dirty=1;model_dirty=1;model_active=model_since_vm=0;stage_active=stage_header=stage_since_vm=0;
    return DR_VU_OK;
}
int dr_vu1_unpack(uint32_t cmd, uint32_t num, uint32_t immediate, const void *bytes, size_t size) {
    if ((cmd & ~31u) != 0x60 || num > 256 || immediate > 0xffff || bytes == NULL ||
        ((cmd & 3) == 3 && ((cmd >> 2) & 3) != 3)) return DR_VU_INVALID;
    uint32_t words = legacy_unpackwords(cmd, num);
    if ((uint64_t)words * 4 > size) return DR_VU_TRUNCATED;
    legacy_unpack(cmd, num, immediate, bytes);
    return DR_VU_OK;
}
int dr_vu1_call(int entry, uint32_t budget) {
    if (entry < -1 || entry > 2047 || !budget || budget > 4000000) return DR_VU_INVALID;
    vu.top = vu.tops; vu.itop = vu.itops;
    if (vu.dbf) { vu.tops = vu.base; vu.dbf = 0; }
    else { vu.tops = vu.base + vu.ofst; vu.dbf = 1; }
    memset(sUnknown, 0, sizeof(sUnknown)); kick_error = 0;
    int model=model_renderer?model_program():0;
    if(entry>=0)model_active=model_since_vm=0;
    if(model && model_active && entry<0 && budget>=64){
        int e=model_batch(model);if(e)return e==1?DR_VU_OK:e;
        model_fallbacks++;
        if(model_since_vm){int e=run(0,budget);if(e)return e;vu.pc=model==1?36:model==2?15:16;model_since_vm=0;}
    }
    int known=stage_renderer && stage_program();
    if(entry>=0)stage_active=stage_header=stage_since_vm=0;
    if(known && stage_active && entry<0 && stage_header && budget>=64){
        int e=stage_batch();if(e)return e==1?DR_VU_OK:e;
        stage_fallbacks++;
        if(stage_since_vm){int e=run(0,budget);if(e)return e;vu.pc=21;stage_since_vm=0;}
    }
    int status = run(entry < 0 ? vu.pc : (uint32_t)entry, budget);
    for (unsigned i = 0; i < 2; i++) for (unsigned j = 0; j < 128; j++)
        if (sUnknown[i][j]) return DR_VU_UNKNOWN;
    if (kick_error) return DR_VU_TRUNCATED;
    if(!status && model && entry==0)model_active=1;
    if(!status && known){if(entry==0)stage_active=1;else if(entry<0 && stage_active)stage_header=1;}
    return status;
}
int dr_vu1_read(uint32_t offset, void *out, size_t bytes) {
    if (offset > sizeof(vu.mem) || bytes > sizeof(vu.mem) - offset || (out == NULL && bytes)) return DR_VU_INVALID;
    if (bytes) memcpy(out, vu.mem + offset, bytes);
    return DR_VU_OK;
}
