#include "Network/Runtime.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

extern "C" {
#include <mpv/client.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include <cstdlib>

RuntimeStatus probeLinkedLibraries() {
    RuntimeStatus status;

    struct smb2_context* smb = smb2_init_context();
    status.smb2ContextOk = smb != nullptr;
    if (smb) {
        smb2_destroy_context(smb);
    }

    status.mpvClientApi = mpv_client_api_version();
    return status;
}

bool initNetwork() {
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

    gNetMem = std::malloc(kNetMemSize);
    if (!gNetMem) return false;

    SceNetInitParam netParam = {};
    netParam.memory = gNetMem;
    netParam.size = kNetMemSize;
    netParam.flags = 0;

    if (sceNetInit(&netParam) < 0) {
        std::free(gNetMem);
        gNetMem = nullptr;
        return false;
    }

    if (sceNetCtlInit() < 0) {
        sceNetTerm();
        std::free(gNetMem);
        gNetMem = nullptr;
        return false;
    }
    return true;
}

void shutdownNetwork() {
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    if (gNetMem) {
        std::free(gNetMem);
        gNetMem = nullptr;
    }
}
