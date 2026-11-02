/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_BT3XBX_ADAPTER_SELFTEST_H
#define DR_BT3XBX_ADAPTER_SELFTEST_H
/* D53-A: host test of the bt3xbx bridge (src/bt3xbx_adapter.c) against the
 * stub engine (d50/bt3xbx_stub.c). Returns 0 on success, negative on failure
 * (the code identifies the check that failed). Host only; the build must
 * define DR_HAVE_BT3XBX so the bridge compiles in. */
int D53_Bt3xbxAdapterSelfTest(void);
#endif
