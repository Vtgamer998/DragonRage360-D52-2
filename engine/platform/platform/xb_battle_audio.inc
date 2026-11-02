/* SPDX-License-Identifier: AGPL-3.0-or-later */
int XbBattleRuntime_BeginAudio(XbBattleRuntime *r,const void *const packs[4],const size_t bytes[4]){
 if(!r || active!=r || !r->managers || r->audio || gSndMgr || gSndRpc || !packs || !bytes)return -891;
 static const unsigned masks[4]={4,8,16,32};int e=0;
 for(unsigned i=0;i<4;i++){e=XbSfx_PrepareBank(packs[i],bytes[i],masks[i],&r->banks[i]);if(e)goto fail;}
 Snd_Init();r->audio=1;e=XbAdx_LastError();if(!e)e=XbSfx_LastError();if(e)goto fail;
 for(unsigned i=0;i<4;i++){Snd_LoadBank(masks[i],XbSfx_BankData(r->banks[i]),0);e=XbSfx_LastError();if(e)goto fail;}
 for(unsigned frame=0;frame<16 && !Snd_IsUploadDone();frame++){Snd_Update();e=XbSfx_LastError();if(e)goto fail;}
 if(!Snd_IsUploadDone()){e=-893;goto fail;}return 0;
fail:
 if(r->audio)XbBattleRuntime_EndAudio(r);
 else for(unsigned i=0;i<4;i++){XbSfx_FreeBank(r->banks[i]);r->banks[i]=NULL;}
 return e;
}
int XbBattleRuntime_ServiceAudio(XbBattleRuntime *r){
 if(!r || active!=r || !r->audio)return -891;
 Snd_Update();int e=XbSfx_LastError();if(e)return e;XbAdx_Tick();return XbAdx_LastError();
}
int XbBattleRuntime_EndAudio(XbBattleRuntime *r){
 if(!r || active!=r || !r->audio)return -891;
 if(r->stage_geometry){int e=XbBattleRuntime_EndStage(r);if(e)return e;}
 Snd_Term();r->audio=0;
 for(unsigned i=0;i<4;i++){XbSfx_FreeBank(r->banks[i]);r->banks[i]=NULL;}
 return XbAdx_Close();
}
