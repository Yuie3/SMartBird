#include "Browser/BrowserActions.hpp"

#include "Browser/BrowserState.hpp"
#include "Browser/Scanner.hpp"
#include "Core/Constants.hpp"
#include "Image/ImageViewer.hpp"
#include "Player/VideoPlayer.hpp"
#include "Utils/FileTypes.hpp"

void openBrowserEntryAtIndex(const ScanState& snapshot, int index, int* selected, int* listTop,
                             AppMode* mode, NVGcontext* vg,
                             SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
                             SceGxmMultisampleMode msaa) {
    if (!selected || !listTop || !mode) return;
    if (snapshot.phase != ScanReady || index < 0 || index >= snapshot.count) return;

    *selected = index;
    keepSelectedVisible(*selected, snapshot.count, listTop);
    const SmbEntry& entry = snapshot.entries[index];
    if (entry.directory) {
        enterDirectory(entry.name, *selected, *listTop);
        *selected = 0;
        *listTop = 0;
        startScan();
    } else if (isImageFile(entry.name)) {
        openImageEntry(entry, snapshot.source, vg);
        *mode = ModeImage;
    } else {
        playEntry(entry, snapshot.source, gxmCtx, patcher, msaa, vg);
        *mode = ModePlayer;
    }
}

void keepSelectedVisible(int selected, int count, int* listTop) {
    if (!listTop) return;
    if (count <= kVisibleEntries) {
        *listTop = 0;
        return;
    }
    if (selected < *listTop) *listTop = selected;
    if (selected >= *listTop + kVisibleEntries) *listTop = selected - kVisibleEntries + 1;
    const int maxTop = count - kVisibleEntries;
    if (*listTop > maxTop) *listTop = maxTop;
    if (*listTop < 0) *listTop = 0;
}

void scrollListByRows(int rows, int count, int* selected, int* listTop) {
    if (!selected || !listTop || rows == 0 || count <= 0) return;
    const int maxTop = count > kVisibleEntries ? count - kVisibleEntries : 0;
    *listTop += rows;
    if (*listTop < 0) *listTop = 0;
    if (*listTop > maxTop) *listTop = maxTop;
    if (*selected < *listTop) *selected = *listTop;
    if (*selected >= *listTop + kVisibleEntries) *selected = *listTop + kVisibleEntries - 1;
    if (*selected >= count) *selected = count - 1;
    if (*selected < 0) *selected = 0;
}
