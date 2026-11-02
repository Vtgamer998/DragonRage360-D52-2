/* Checked interface. Drawing state only; not a combat snapshot/rollback API. */
#include "dr_vu1_stage.inc"
void dr_vu1_reset(DrVuGif callback, void *opaque) {
    memset(&vu, 0, sizeof(vu));
    memset(sUnknown, 0, sizeof(sUnknown));
    memset(sProg, 0, sizeof(sProg));
    sStatRuns = sStatInstr = sStatKicks = sProgSize = 0;
    stage_active=stage_header=stage_since_vm=0;stage_draws=stage_fallbacks=0;stage_dirty=1;
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
    stage_dirty=1;stage_active=stage_header=stage_since_vm=0;
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
    if(!status && known){if(entry==0)stage_active=1;else if(entry<0 && stage_active)stage_header=1;}
    return status;
}
int dr_vu1_read(uint32_t offset, void *out, size_t bytes) {
    if (offset > sizeof(vu.mem) || bytes > sizeof(vu.mem) - offset || (out == NULL && bytes)) return DR_VU_INVALID;
    if (bytes) memcpy(out, vu.mem + offset, bytes);
    return DR_VU_OK;
}
