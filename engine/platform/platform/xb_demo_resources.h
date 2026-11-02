/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_DEMO_RESOURCES_H
#define XB_DEMO_RESOURCES_H
/* Register only independently identified whole-file formats. A skeleton
 * embedded in a model needs that model's own converter, not this adapter. */
int XbRes_RegisterAnimation(int first, int last, int packed);
int XbRes_RegisterSkeleton(int first, int last, int packed);
/* Original loading sheets; conversion runs after BPE, before relocation. */
int XbRes_RegisterLoadingScreens(void);
int XbRes_RegisterInitialMembers(void);
int XbRes_LoadMap(const char *path);
#endif
