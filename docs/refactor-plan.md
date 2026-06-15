# vita-smb-player Refactor Plan

本文件記錄 `src/vita/main.cpp` 的拆分策略。重構目標是改善模組邊界，但不改變既有功能與行為。

## 原則

- 每次只搬一個清楚邊界的職責。
- 優先搬移原有程式碼，不在第一輪重寫邏輯。
- 每個搬移批次都必須能通過 Vita Docker build。
- 不把無關功能、UI 改版或行為調整混入重構 commit。
- 若遇到難以拆分的全域狀態，先集中到 shared state，再於後續批次逐步封裝。

## 現況

`src/vita/main.cpp` 原本約 4500 行；截至本文件最新更新，已降到約 630 行。仍包含：

- Vita app 初始化與主迴圈
- GXM / NanoVG 初始化與 rendering
- screen 狀態切換與主流程控制
- background work / shutdown 協調

已拆出：

- Core constants/types/state
- Utils text/json/file type/math
- Config settings/i18n/hidden item JSON
- Browser path/history、SMB/local scanner、SMB copy manager、browser actions/list navigation
- Network runtime init/shutdown and linked-library probe
- Player message helpers、SMB stream callback、mpv init/shutdown/control/event/render、player overlay
- Image loading/view state helpers、image viewer rendering、image interaction helpers
- UI IME input helpers、basic widgets / rows / footer hints、screens、general input helpers

## 目標結構

```text
src/vita/
  main.cpp
  App.hpp
  App.cpp

  Core/
    Constants.hpp
    Types.hpp
    State.hpp
    State.cpp

  Utils/
    Text.hpp
    Text.cpp
    Json.hpp
    Json.cpp
    FileTypes.hpp
    FileTypes.cpp
    Math.hpp
    Math.cpp

  Config/
    Settings.hpp
    Settings.cpp
    I18n.hpp
    I18n.cpp
    HiddenItems.hpp
    HiddenItems.cpp

  Browser/
    BrowserState.hpp
    BrowserState.cpp
    Scanner.hpp
    Scanner.cpp
    CopyManager.hpp
    CopyManager.cpp

  Network/
    Runtime.hpp
    Runtime.cpp
    SmbClient.hpp
    SmbClient.cpp

  Player/
    PlayerMessages.hpp
    PlayerMessages.cpp
    PlayerOverlay.hpp
    PlayerOverlay.cpp
    VideoPlayer.hpp
    VideoPlayer.cpp
    SmbStream.hpp
    SmbStream.cpp

  Image/
    ImageViewer.hpp
    ImageViewer.cpp

  UI/
    ImeInput.hpp
    ImeInput.cpp
    UiRenderer.hpp
    UiRenderer.cpp
    Screens.hpp
    Screens.cpp
    Widgets.hpp
    Widgets.cpp
    Input.hpp
    Input.cpp
```

最終 `main.cpp` 只保留 Vita entry point、建立 App、啟動主流程。

## 分階段計畫

### Phase 0：Baseline

- [x] 確認目前 git 狀態。
- [x] 執行 `./scripts/build-vita-docker.sh`。
- [x] 記錄 build 是否通過。

### Phase 1：Core 常數與型別

- [x] 新增 `src/vita/Core/Constants.hpp`。
- [x] 新增 `src/vita/Core/Types.hpp`。
- [x] 搬移：
  - app / browser / scan enum
  - shared constants
  - shared state structs
- [x] 不改邏輯、不新增 class。
- [x] Build 驗證。

### Phase 2：Utils

- [x] 搬移純工具：
  - text copy / trim / case-insensitive matching
  - JSON parser helper
  - file extension and hidden/system-name detection
  - math clamp
- [x] 每個工具群組搬完就 build。

### Phase 3：Config

- [x] 拆分：
  - `I18n`
  - `Settings`
  - `HiddenItems`
- [x] 第一輪仍可共用現有全域狀態。
- [x] Build 驗證。

### Phase 4：Browser / Scanner / Copy

- [x] 拆分：
  - browser path and focus history
  - SMB/local scan thread
  - SMB copy manager
  - network init/shutdown
- [x] Build 驗證。

### Phase 5：Player

- [x] 先拆 `SmbStream` callback。
- [x] 再拆 mpv init/shutdown/control/event/render。
- [x] 拆播放器 overlay 繪製。
- [x] 保持錯誤畫面與 codec/resolution 判斷行為不變。
- [x] 每個小批次 build。

### Phase 6：Image

- [x] 拆圖片讀取、NanoVG image lifecycle、zoom/pan/rotate/HUD helper。
- [x] 拆圖片畫面繪製與相鄰圖片搜尋。
- [x] 保持目前觸控與按鍵行為。
- [x] Build 驗證。

### Phase 7：UI / Input

- [x] 先拆 IME input helpers。
- [x] 拆 basic widgets、rows、footer hints。
- [x] 拆 screens、renderer、一般 input helpers。
- [x] 不重做 UI，不改 layout。
- [x] Build 驗證。

### Phase 8：Application

- 建立 `App` 類別。
- 將初始化、主迴圈、shutdown 移到 `App.cpp`。
- `main.cpp` 只保留 entry point。
- 最終 build。

## 驗證規則

每個批次至少執行：

```sh
./scripts/build-vita-docker.sh
```

如有 i18n key 變動，額外執行 i18n key 對齊檢查。

## 注意事項

- 不修改 `TODO.md`，除非使用者明確要求。
- 不提交 `build/`、`releases/`、`.DS_Store`。
- 第一次重構以可編譯為優先，不急著把所有全域狀態 class 化。
