#include "Player/PlayerMessages.hpp"

#include "Core/State.hpp"
#include "Utils/Text.hpp"

void setPlayerMessage(const char* message) {
    lockScan();
    copyText(gPlayer.message, sizeof(gPlayer.message), message);
    unlockScan();
}

void setPlayerDetail(const char* detail) {
    lockScan();
    copyText(gPlayer.detail, sizeof(gPlayer.detail), detail);
    unlockScan();
}
