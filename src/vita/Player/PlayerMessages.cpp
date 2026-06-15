#include "Player/PlayerMessages.hpp"

#include "Core/State.hpp"
#include "Utils/Text.hpp"

#include <psp2/kernel/clib.h>

void setPlayerMessage(const char* message) {
    lockScan();
    copyText(gPlayer.message, sizeof(gPlayer.message), message);
    unlockScan();
    sceClibPrintf("[vita-smb-player] player: %s\n", gPlayer.message);
}

void setPlayerDetail(const char* detail) {
    lockScan();
    copyText(gPlayer.detail, sizeof(gPlayer.detail), detail);
    unlockScan();
    sceClibPrintf("[vita-smb-player] detail: %s\n", gPlayer.detail);
}
