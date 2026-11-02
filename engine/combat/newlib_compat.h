/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Forced after upstream newlib_shim.h. Its macro-renamed functions need
 * explicit prototypes: implicit int declarations have the wrong return type.
 */
#ifndef DR_NEWLIB_COMPAT_H
#define DR_NEWLIB_COMPAT_H
float Port_scalbnf(float value, int exponent);
float Port_copysignf(float magnitude, float sign);
#endif
