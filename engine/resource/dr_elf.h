/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_ELF_H
#define DR_ELF_H
#include "dr_disc.h"
/* Map a byte range in a MIPS ELF32 LE PT_LOAD to a bounded source-file span.
 * Does not execute MIPS, relocate code or infer pointers outside file-backed data. */
int dr_elf_mips_span(const dr_disc *,const dr_span *,uint32_t virtual_address,size_t bytes,dr_span *);
#endif
