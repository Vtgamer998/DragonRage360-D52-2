/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_ENGINE_ADAPTER_SELFTEST_H
#define DR_ENGINE_ADAPTER_SELFTEST_H
/* D53-A: engine adapter self-test. Returns 0 on success, negative on failure
 * (the code identifies the check that failed). Host only. */
int D53_EngineAdapterSelfTest(void);
#endif
