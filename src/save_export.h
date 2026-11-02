/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef DR_SAVE_EXPORT_H
#define DR_SAVE_EXPORT_H
#include <stddef.h>
/* root normally game:; tests use a private temporary directory.
 * import_existing reads root/save_import/BASLUS-21678DBZT3. Never overwrites inputs. */
int dr_save_export(const char *root,int import_existing,char *message,size_t capacity);
#endif
