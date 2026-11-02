/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_INITIAL_LOAD_H
#define XB_INITIAL_LOAD_H
/* Close active IO, drop pending requests, keep every returned buffer owned by
 * its caller. Only use when all those destinations have retained owners. */
void XbFile_AbortRequestsRetainBuffers(void);
/* No-script initial load BEFORE fighter creation. Stops jobs/audio uploads,
 * releases direct initial-job buffers and resets their typed owner fields.
 * Returns 0 without mutation for script jobs or diagnostic/live fighter state.
 * BtlObj/GSC initialization must
 * already have completed; the caller separately terminates those owners.
 * The original error remains latched. A controller must inspect it and start
 * a fresh checked-read scope before retrying; no automatic retry is added. */
int XbBtlLoad_AbortInitial(void);
#endif
