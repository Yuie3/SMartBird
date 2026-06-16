#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "App.hpp"

unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;

int main() {
    SceUID thread = sceKernelCreateThread("vita_smb_player_main", appMain, 0x40, 8 * 1024 * 1024, 0, 0, nullptr);
    if (thread < 0) {
        sceClibPrintf("[vita-smb-player] sceKernelCreateThread failed: 0x%08x\n", thread);
        return 1;
    }

    sceKernelStartThread(thread, 0, nullptr);
    sceKernelWaitThreadEnd(thread, nullptr, nullptr);
    sceKernelExitProcess(0);
    return 0;
}
