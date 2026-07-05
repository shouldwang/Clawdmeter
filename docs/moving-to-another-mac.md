# 換電腦使用手冊(USB 版)

適用情境:把 Clawdmeter 裝置從一台 Mac 拔下,改接到另一台 Mac(例如公司電腦)使用。

## 先理解兩件事

1. **韌體住在裝置上,不用重燒。** 換電腦只需要在新電腦上裝 host 端的 daemon,裝置本身什麼都不用動。
2. **螢幕顯示的是「接著的那台電腦」登入的 Claude 帳號用量。** daemon 從本機 Keychain(service `Claude Code-credentials`)讀 token,所以接公司電腦就顯示公司帳號的額度,接私人電腦就顯示私人帳號,跟隨 USB 線走,不需要任何切換設定。

## 新電腦的前置需求

- macOS,Python 3.10 以上(檢查:`python3 --version`;不夠新就 `brew install python`)
- Claude Code 已登入(`claude` 指令登入過即可,token 會存進 Keychain)
- 能 clone 這個 repo(git + GitHub 權限)

不需要的東西(BLE 版的遺產,現在都免了):Homebrew 的 blueutil、藍牙權限、系統設定配對。

## 安裝步驟(約五分鐘)

```bash
# 1. 取得 repo
git clone https://github.com/shouldwang/Clawdmeter.git
cd Clawdmeter

# 2. 執行安裝(建 venv、裝 pyserial/httpx、設定 LaunchAgent 並啟動)
./install-mac.sh

# 3. 用 USB-C 接上裝置

# 4. 確認運作
launchctl list | grep claude-usage          # 有一行就是在跑
tail -F ~/Library/Logs/claude-usage-daemon.out.log   # 應看到 Connected 與 Sending
```

log 裡看到 `Device: {"ack":true}` 就代表整條鏈路通了,螢幕約五秒內開始顯示這台電腦帳號的用量。

## 側鍵行為

按鍵是標準 USB HID 鍵盤,**插上任何電腦都直接可用**,不需要裝任何東西:

- 左鍵:切換畫面(splash ↔ usage)
- 中鍵短按:splash 切動畫/usage 切亮度;按住 8 秒硬體關機
- 右鍵:送出 Shift+Tab(Claude Code 模式切換,對聚焦視窗生效)

## 原電腦要處理什麼

**什麼都不用。** 裝置拔走後,原電腦的 daemon 會進入「找不到埠、每 5 秒重試」的等待狀態,不耗什麼資源;裝置插回去會自動恢復。想徹底停掉:

```bash
launchctl unload ~/Library/LaunchAgents/com.user.claude-usage-daemon.plist
```

裝置離線期間,螢幕停留在最後一次同步的數字。

## 疑難排解

| 症狀 | 處理 |
|---|---|
| log 顯示找不到裝置 | `ls /dev/cu.usbmodem*` 確認埠存在;換一條有資料線功能的 USB-C 線(純充電線不行) |
| `API HTTP 401` | 該電腦的 Claude Code 重新登入(`claude login` 或 `claude` 內重新認證),再重啟 daemon |
| `No token in ~/.claude` | 同上,Keychain 裡還沒有 token |
| 螢幕顯示 101%、status rejected | 正常,代表 session 額度已用爆,等重置 |
| 公司電腦 MDM 限制 | daemon 只是 user-level LaunchAgent + venv,一般不需要管理員權限;若 LaunchAgent 被政策擋下,可先手動前景跑 `daemon/.venv/bin/python3 daemon/claude_usage_daemon_usb.py` |

## 什麼時候才需要 PlatformIO

只有要**改韌體重燒**才需要(`./flash-mac.sh waveshare_amoled_216`)。日常換電腦使用完全不用裝。

## 已知限制

- 一條 USB 線同一時間只接一台電腦,顯示的帳號跟著線走
- 兩台電腦各自要跑一次 `./install-mac.sh`(各自的 venv 與 LaunchAgent 是本機的)
- daemon 用 Espressif VID(`0x303A`)自動找埠;如果同時插著其他 ESP32 開發板,可能選錯埠(拔掉其他板子即可)
