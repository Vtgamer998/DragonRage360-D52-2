/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Typed native ABI for libm; bit helpers are explicit integer interfaces.
 * Single arithmetic comes from the upstream PS2 model. Double arithmetic
 * uses IEEE hardware as upstream plat_libm.c does, with ordered comparisons.
 */
#include <stdint.h>
#include <string.h>
#include <math.h>
static double value(uint64_t x){double d;memcpy(&d,&x,8);return d;}
static uint64_t bits(double d){uint64_t x;memcpy(&x,&d,8);return x;}
static float fvalue(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static uint32_t fbits(float f){uint32_t x;memcpy(&x,&f,4);return x;}
float Port_ceilf(float x){return ceilf(x);}
float Port_expf(float x){return expf(x);}
float Port_logf(float x){return logf(x);}
float Port_fmodf(float x,float y){return fmodf(x,y);}
double Port_pow(double x,double y){return pow(x,y);}
#define BINARY(name,op) uint64_t dr_ps2_##name##df_bits(uint64_t a,uint64_t b){return bits(value(a) op value(b));}
BINARY(add,+) BINARY(sub,-) BINARY(mul,*) BINARY(div,/)
#define CMP(name,op) int dr_ps2_##name##df_bits(uint64_t a,uint64_t b){return value(a) op value(b);}
CMP(eq,==) CMP(ne,!=) CMP(lt,<) CMP(le,<=) CMP(gt,>) CMP(ge,>=)
int dr_ps2_unorddf_bits(uint64_t a,uint64_t b){return isnan(value(a))||isnan(value(b));}
int dr_ps2_orddf_bits(uint64_t a,uint64_t b){return !dr_ps2_unorddf_bits(a,b);}
uint64_t dr_ps2_extendsfdf_bits(uint32_t a){return bits((double)fvalue(a));}
uint32_t dr_ps2_truncdfsf_bits(uint64_t a){return fbits((float)value(a));}
int32_t dr_ps2_fixdf_bits(uint64_t a){return (int32_t)value(a);}
uint32_t dr_ps2_fixunsdf_bits(uint64_t a){return (uint32_t)value(a);}
uint64_t dr_ps2_floatdf_bits(int32_t a){return bits((double)a);}
uint64_t dr_ps2_floatunsdf_bits(uint32_t a){return bits((double)a);}
