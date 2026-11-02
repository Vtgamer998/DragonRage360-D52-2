/* SPDX-License-Identifier: MIT
 * Actual limits of the supported native profile: original disc stages and
 * original 4:3 camera composition (dr_capture_platform.c already selects it).
 * These are configuration values, not success replies for missing services.
 */
int Port_StageBufSize(void) {return 0x6cb800;}
float Port_WideFactor(void) {return 1.0f;}
