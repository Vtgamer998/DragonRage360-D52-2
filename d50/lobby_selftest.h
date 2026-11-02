/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef D52_LOBBY_SELFTEST_H
#define D52_LOBBY_SELFTEST_H
/* D52-1 loopback UDP lobby self-test: one host and one client on 127.0.0.1,
 * pairing, explicit leave, host timeout and client search timeout. It logs
 * PASS/FAIL lines through D50_Log and returns 0 on success, a negative code
 * on the first failure. Loopback only: a two-console session still needs a
 * physical test on the RGH consoles. */
int D52_LobbySelfTest(void);
#endif
