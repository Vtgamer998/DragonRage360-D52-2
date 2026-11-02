/* SPDX-License-Identifier: MIT */
#ifndef XB_PLATFORM_H
#define XB_PLATFORM_H
void XbPlatform_Install(void);
/* Diagnostic: the first sector of the first file in each AFS through ADXF.
 * No conversion ids inferred; does not initialize memory or start a battle. */
int XbPlatform_ProbeFiles(const char *iso);
#endif
