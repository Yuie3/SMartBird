#pragma once

#include <psp2/kernel/threadmgr.h>

class Application {
public:
    int run(SceSize args, void* arg);
};

int appMain(SceSize args, void* arg);
