/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Explicit fixed-battle build policy, enabled only by --fixed-battle.
 * Query meanings: pinned port/src/{plat_stages.c,plat_songs.c,gs/net.c}.
 * No transport, session or lobby can be created by this translation unit.
 * These are configuration answers, not implementations of missing services.
 */
int Port_TestScreen(void) {return 0;} /* one full-screen view */
int Port_TestStage(void) {return 0;} /* original disc stage index 0 */
int Port_TestBgm(void) {return -1;} /* no BGM override; does not implement/mute audio */
int Port_NetActive(void) {return 0;}
int Port_NetSession(void) {return 0;}
int Port_NetMe(void) {return 0;} /* local side */
int Port_NetView(void) {return 0;} /* fixed full-screen view of side 0 */
int Port_NetSideOptions(int *camera,int *shake) {
    /* No negotiated remote options. As in the upstream offline path, leave
     * the caller's local settings intact; zero denotes no override. */
    (void)camera;(void)shake;return 0;
}
/* PC-only optional preferences disabled in the original local-match profile. */
int Port_HudSwap(void) {return 0;} /* keep side 0 on its original HUD side */
int Port_RumbleTest(void) {return 0;} /* do not force vibration during replay */
int Port_ClashSquare(void) {return 0;} /* do not replace the original clash prompt */
/* This build has no lobby creation path. Empty code and no retained room are
 * its actual offline state; neither query reports a successful connection. */
const char *Port_LobbyCode(void) {static const char no_room[]="";return no_room;}
int Port_LobbyInBackground(void) {return 0;}
