# vita-smb-player

PS Vita 用的 SMB / 本機媒體播放器。目標是用簡單的介面瀏覽 SMB 分享或 Vita 本機資料夾，播放影片、查看圖片，並提供基本的複製與隱藏項目管理。

目前版本是第一版 MVP，重點放在可用的 SMB 瀏覽、影片播放、圖片檢視與可重現建置流程。

## 功能

- 連線到 SMB 分享並瀏覽資料夾。
- 瀏覽 Vita 本機路徑，預設入口是 `ux0:`，可在啟動頁修改。
- 播放常見影片檔案。
- 檢視圖片，支援平移、縮放、旋轉、上下張與 HUD 開關。
- 從 SMB 複製檔案到本機，並顯示進度條。
- 複製目標會依類型自動分流：
  - 影片：`ux0:/video`
  - 圖片：`ux0:/picture`
- 可隱藏不想顯示的檔案或資料夾，並可在隱藏項目管理頁取消隱藏。
- 啟動頁會記住上次輸入的 SMB 連線資訊與本機入口路徑。
- 內建繁體中文與英文 i18n 資源。

## 下載與安裝

第一版 VPK 會輸出在：

```text
releases/vita_smb_player_v0.03.vpk
```

把 VPK 安裝到已可執行自製程式的 PS Vita 上即可啟動。

執行時資料會儲存在：

```text
ux0:/data/vita-smb-player/
```

主要設定檔：

- `connection.txt`：SMB 連線資訊與本機入口路徑。
- `hidden.json`：使用者隱藏的檔案與資料夾。

## 使用方式

### 啟動頁

啟動後會進入連線頁，可設定：

- SMB 伺服器
- 分享名稱
- SMB 路徑
- 使用者
- 密碼
- 網域
- 本機路徑

操作：

| 按鍵 | 功能 |
| --- | --- |
| 方向鍵上下 | 移動欄位焦點 |
| 方向鍵左右 | 在底部三個按鈕間移動 |
| `○` | 編輯欄位，或執行目前選到的按鈕 |
| `START` | 連線到 SMB |
| `SELECT` | 進入本機 |
| `L` | 管理隱藏項目 |
| `×` | 離開程式 |

### 檔案瀏覽

操作：

| 按鍵 | 功能 |
| --- | --- |
| 方向鍵上下 | 移動清單焦點，長按會連續移動 |
| `○` | 進入資料夾、播放影片或檢視圖片 |
| `×` | 回上一層；在根目錄時回啟動頁 |
| `△` | 重新整理 |
| `SELECT` | 隱藏目前項目 |
| `L` | 管理隱藏項目 |
| `□` | 從 SMB 複製目前檔案到本機 |

觸控操作：

- 點選清單項目可開啟。
- 上下滑動可捲動清單。

### 影片播放

操作：

| 按鍵 | 功能 |
| --- | --- |
| `○` | 暫停 / 繼續 |
| `×` | 停止播放並返回瀏覽器 |
| `△` | 旋轉播放畫面 |
| 方向鍵上 | 顯示播放器 HUD |
| 方向鍵下 | 隱藏播放器 HUD |
| 方向鍵左 / 右 | 往前 / 往後跳 10 秒 |

觸控操作：

- 點一下顯示或隱藏操作介面。
- 可點擊播放 / 暫停、快轉、倒轉與進度列。

### 圖片檢視

操作：

| 按鍵 | 功能 |
| --- | --- |
| 左搖桿 | 移動圖片 |
| 方向鍵上 / 下 | 放大 / 縮小 |
| 右搖桿 | 放大 / 縮小 |
| `○` | 重置縮放與位置 |
| `△` | 旋轉圖片 |
| `□` | 開關 HUD |
| `L` / `R` | 上一張 / 下一張 |
| `×` | 返回瀏覽器 |

觸控操作：

- 單指拖曳移動圖片。
- 雙指縮放圖片。
- 點左側 / 右側切換上一張 / 下一張。
- 點中間重置檢視。
- 點左上角返回。

## 支援格式與限制

目前支援瀏覽的副檔名：

- 影片：`mp4`, `m4v`, `mkv`, `avi`, `mov`, `webm`, `ts`
- 圖片：`jpg`, `jpeg`, `png`, `bmp`, `gif`, `tga`

已知限制：

- 影片解碼能力受目前 Vita 版 mpv / FFmpeg / GXM backend 限制。
- 建議影片格式是 H.264 8-bit + AAC stereo，解析度不超過 1080p。
- VP9 / Opus 這類格式目前不列為支援目標。
- 超過 1080p 的橫式影片會顯示錯誤；直式影片需維持短邊 1080、長邊 1920 以下。
- 圖片檔案目前限制為 32 MB 以下。
- 第一版仍以可用性為主，部分高位元率或 1080p 60fps 影片可能無法穩定滿速播放。

## 建置

建置流程使用可重用的 Docker builder image：`vita-builder-gxm`。

### 需求

- Docker
- 可執行 `linux/amd64` container 的環境
- 本 repo 內建的 `third_party/nanovg-gxm`

### 建立 Builder Image

```sh
./scripts/build-builder-image.sh
```

預設會使用：

```text
xfangfang/wiliwili_psv_builder:latest-gxm
```

並在 image 內建置 `libsmb2`，安裝到 VitaSDK 的 arm-vita-eabi prefix。

可以指定 libsmb2 ref：

```sh
LIBSMB2_REF=libsmb2-6.2 ./scripts/build-builder-image.sh
```

### 驗證 Builder Image

```sh
./scripts/verify-builder.sh
```

這會檢查：

- `pkg-config --modversion libsmb2`
- `pkg-config --libs --cflags libsmb2`
- Vita toolchain 可 include 並 link `libsmb2`

### 建置 VPK

```sh
./scripts/build-vita-docker.sh
```

輸出：

```text
build/vita/vita_smb_player.self
build/vita/vita_smb_player_v0.03.vpk
releases/vita_smb_player_v0.03.vpk
```

可用環境變數覆蓋預設 SMB 設定：

```sh
SMB_SERVER=192.168.1.50 \
SMB_SHARE=Media \
SMB_PATH=Movies \
SMB_USER=myuser \
SMB_PASSWORD=mypassword \
SMB_DOMAIN= \
./scripts/build-vita-docker.sh
```

這些只是首次啟動或設定不存在時的預設值；App 內修改後會儲存在 `connection.txt`。

## 專案結構

```text
assets/                  字型與 i18n JSON
docker/                  Vita builder Dockerfile
scripts/                 建置與驗證腳本
src/vita/                Vita App 原始碼
third_party/nanovg-gxm/  NanoVG GXM renderer
releases/                VPK 輸出
```

## 第三方元件

- VitaSDK
- libmpv / FFmpeg GXM builder environment from `xfangfang/wiliwili_psv_builder`
- libsmb2
- NanoVG GXM
- Roboto font

## 授權

本專案原始碼以 GNU General Public License v3.0 授權，詳見 [LICENSE](LICENSE)。

SPDX identifier：

```text
GPL-3.0-only
```

第三方元件仍保留各自的授權條款。發布 VPK 或二進位檔時，請一併確認 libmpv、FFmpeg、libsmb2、NanoVG GXM、字型與 VitaSDK 相關元件的授權要求。
