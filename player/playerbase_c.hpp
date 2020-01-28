#ifndef PLAYERBASE_TYPE
#error define the player type (DROPlayer,S98Player,VGMPlayer) before including
#endif

#define PASTE(a,b,c) a ## b ## c
#define FUNC(a,b) PASTE(a,_,b)

UINT8 FUNC(PLAYERBASE_TYPE,IsMyFile)(DATA_LOADER *loader) {
    return PLAYERBASE_TYPE::IsMyFile(loader);
}

UINT32 FUNC(PLAYERBASE_TYPE,GetPlayerType)(PLAYERBASE_TYPE *player) {
    return player->GetPlayerType();
}

const char *FUNC(PLAYERBASE_TYPE,GetPlayerName)(PLAYERBASE_TYPE *player) {
    return player->GetPlayerName();
}

PLAYERBASE_TYPE *FUNC(PLAYERBASE_TYPE,New)(void) {
    return new PLAYERBASE_TYPE();
}

void FUNC(PLAYERBASE_TYPE,Delete)(PLAYERBASE_TYPE *player) {
    delete player;
}

UINT8 FUNC(PLAYERBASE_TYPE,LoadFile)(PLAYERBASE_TYPE *player, DATA_LOADER *loader) {
    return player->LoadFile(loader);
}

UINT8 FUNC(PLAYERBASE_TYPE,UnloadFile)(PLAYERBASE_TYPE *player) {
    return player->UnloadFile();
}

const char * const * FUNC(PLAYERBASE_TYPE,GetTags)(PLAYERBASE_TYPE *player) {
    return player->GetTags();
}

UINT8 FUNC(PLAYERBASE_TYPE,GetSongInfo)(PLAYERBASE_TYPE *player, PLR_SONG_INFO *songInf) {
    return player->GetSongInfo(*songInf);
}

UINT8 FUNC(PLAYERBASE_TYPE,GetSongDeviceInfo)(PLAYERBASE_TYPE *player, PLR_DEV_INFO **devInfList, UINT32 *devInfLen) {
    UINT32 i;
    std::vector<PLR_DEV_INFO> tmpDevInfList;
    PLR_DEV_INFO *mDevInfList;
    UINT8 ret;
    ret = player->GetSongDeviceInfo(tmpDevInfList);
    if(ret == 0xFF) return ret;

    mDevInfList = (PLR_DEV_INFO *)malloc(sizeof(PLR_DEV_INFO) * tmpDevInfList.size());

    if(mDevInfList == NULL) {
        tmpDevInfList.clear();
        return 0xFF;
    }

    /*
     *
     * c++03 states that date should be contiguous, not sure what
     * c++ version this needs to target - play it safe by copying
     * every element
     *
    */
    for(i=0;i<tmpDevInfList.size();i++) {
        memcpy(&mDevInfList[i],&tmpDevInfList[i],sizeof(PLR_DEV_INFO));
    }

    *devInfList = mDevInfList;
    *devInfLen = tmpDevInfList.size();

    tmpDevInfList.clear();
    return ret;
}

UINT8 FUNC(PLAYERBASE_TYPE,InitDeviceOptions)(PLR_DEV_OPTS *devOpts) {
    return PLAYERBASE_TYPE::InitDeviceOptions(*devOpts);
}

UINT8 FUNC(PLAYERBASE_TYPE,SetDeviceOptions)(PLAYERBASE_TYPE *player, UINT32 id, const PLR_DEV_OPTS *devOpts) {
    return player->SetDeviceOptions(id,*devOpts);
}

UINT8 FUNC(PLAYERBASE_TYPE,GetDeviceOptions)(PLAYERBASE_TYPE *player, UINT32 id, PLR_DEV_OPTS *devOpts) {
    return player->GetDeviceOptions(id,*devOpts);
}

UINT8 FUNC(PLAYERBASE_TYPE,SetDeviceMuting)(PLAYERBASE_TYPE *player, UINT32 id, const PLR_MUTE_OPTS *muteOpts) {
    return player->SetDeviceMuting(id,*muteOpts);
}

UINT8 FUNC(PLAYERBASE_TYPE,GetDeviceMuting)(PLAYERBASE_TYPE *player, UINT32 id, PLR_MUTE_OPTS *muteOpts) {
    return player->GetDeviceMuting(id,*muteOpts);
}

#if PLAYERBASE_FCC != FCC_S98
UINT8 FUNC(PLAYERBASE_TYPE,SetPlayerOptions)(PLAYERBASE_TYPE *player, const PLAYERTYPE_PLAY_OPTIONS *playOpts) {
    return player->SetPlayerOptions(*playOpts);
}

UINT8 FUNC(PLAYERBASE_TYPE,GetPlayerOptions)(PLAYERBASE_TYPE *player, PLAYERTYPE_PLAY_OPTIONS *playOpts) {
    return player->GetPlayerOptions(*playOpts);
}
#endif

UINT32 FUNC(PLAYERBASE_TYPE,GetSampleRate)(PLAYERBASE_TYPE *player) {
    return player->GetSampleRate();
}

UINT8 FUNC(PLAYERBASE_TYPE,SetSampleRate)(PLAYERBASE_TYPE *player, UINT32 sampleRate) {
    return player->SetSampleRate(sampleRate);
}

UINT8 FUNC(PLAYERBASE_TYPE,SetPlaybackSpeed)(PLAYERBASE_TYPE *player, double speed) {
    return player->SetPlaybackSpeed(speed);
}

void FUNC(PLAYERBASE_TYPE,SetCallback)(PLAYERBASE_TYPE *player, PLAYERTYPE_EVENT_CB cbFunc, void *cbParam) {
    player->SetCallback((PLAYER_EVENT_CB)cbFunc,cbParam);
}

UINT32 FUNC(PLAYERBASE_TYPE,Tick2Sample)(PLAYERBASE_TYPE *player, UINT32 ticks) {
    return player->Tick2Sample(ticks);
}

UINT32 FUNC(PLAYERBASE_TYPE,Sample2Tick)(PLAYERBASE_TYPE *player, UINT32 samples) {
    return player->Sample2Tick(samples);
}

double FUNC(PLAYERBASE_TYPE,Tick2Second)(PLAYERBASE_TYPE *player, UINT32 ticks) {
    return player->Tick2Second(ticks);
}

double FUNC(PLAYERBASE_TYPE,Sample2Second)(PLAYERBASE_TYPE *player, UINT32 samples) {
    return player->Sample2Second(samples);
}

UINT8 FUNC(PLAYERBASE_TYPE,GetState)(PLAYERBASE_TYPE *player) {
    return player->GetState();
}

UINT32 FUNC(PLAYERBASE_TYPE,GetCurPos)(PLAYERBASE_TYPE *player, UINT8 unit) {
    return player->GetCurPos(unit);
}

UINT32 FUNC(PLAYERBASE_TYPE,GetCurLoop)(PLAYERBASE_TYPE *player) {
    return player->GetCurLoop();
}

UINT32 FUNC(PLAYERBASE_TYPE,GetTotalTicks)(PLAYERBASE_TYPE *player) {
    return player->GetTotalTicks();
}

UINT32 FUNC(PLAYERBASE_TYPE,GetLoopTicks)(PLAYERBASE_TYPE *player) {
    return player->GetLoopTicks();
}

UINT32 FUNC(PLAYERBASE_TYPE,GetTotalPlayTicks)(PLAYERBASE_TYPE *player, UINT32 numLoops) {
    return player->GetTotalPlayTicks(numLoops);
}

UINT8 FUNC(PLAYERBASE_TYPE,Start)(PLAYERBASE_TYPE *player) {
    return player->Start();
}

UINT8 FUNC(PLAYERBASE_TYPE,Stop)(PLAYERBASE_TYPE *player) {
    return player->Stop();
}

UINT8 FUNC(PLAYERBASE_TYPE,Reset)(PLAYERBASE_TYPE *player) {
    return player->Reset();
}

UINT8 FUNC(PLAYERBASE_TYPE,Seek)(PLAYERBASE_TYPE *player, UINT8 unit, UINT32 pos) {
    return player->Seek(unit,pos);
}

UINT8 FUNC(PLAYERBASE_TYPE,Render)(PLAYERBASE_TYPE *player, UINT32 smplCnt, WAVE_32BS *data) {
    return player->Render(smplCnt,data);
}
