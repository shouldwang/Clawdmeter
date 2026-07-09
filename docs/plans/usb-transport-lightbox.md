# USB 傳輸與 Lightbox 遷移計畫

日期:2026-07-05
對象板子:Waveshare ESP32-S3-Touch-AMOLED-2.16(USB 供電、無電池)
決策記錄:BLE 整個拆除;鍵盤功能改走 USB HID;梗圖來源用 TF 卡。

## 背景與動機

目前資料通道走 BLE GATT(daemon 透過藍牙把用量 JSON 寫進裝置),側鍵透過 BLE HID 送快捷鍵。BLE 帶來大量維運複雜度:macOS bonding 自我修復、owner-lock 機制、藍牙權限 priming、配對手勢。本裝置無電池、常駐 USB 供電,改走 USB 有線傳輸可移除整類問題。

使用者需求:
1. 改用 USB 連線,消除藍牙配對問題
2. 側鍵重新配置(語音模式左鍵不使用、中鍵長按三秒配對手勢移除)
3. 新增 lightbox 畫面:顯示 TF 卡上的梗圖(PNG/GIF)

## 已查證的技術前提

- Type-C 是 ESP32-S3 原生 USB(非 UART bridge),repo 已設 `ARDUINO_USB_CDC_ON_BOOT=1`
- 目前 S3 env 未設 `ARDUINO_USB_MODE`,預設 HW CDC/JTAG 模式,該模式無法做 USB HID;必須改 `ARDUINO_USB_MODE=0`(TinyUSB OTG)才能 CDC+HID composite。切換後燒錄時序列埠會短暫重新列舉,需實測
- LVGL 9 內建 lodepng(PNG)與 gifdec(GIF)解碼器,開 `LV_USE_LODEPNG=1`、`LV_USE_GIF=1` build flag 即可,無需第三方庫
- **已查證:2.16 板的 TF 卡腳位(2026-07-05,官方 schematic + docs.waveshare.com wiki 雙來源一致)**:走 **SPI**——MOSI=GPIO1、SCK=GPIO2、MISO=GPIO3、CS=GPIO41。卡座 D1/D2 未接線,硬體上無法 4-bit SDMMC。來源:`files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.16/ESP32-S3-Touch-AMOLED-2.16-Schematic.pdf`(SD-CARD 區塊 netlist)與 wiki 的 GPIO pin assignments 表
- **風險:GPIO2 與 board.h 的 reset 定義衝突**。repo 的 `boards/waveshare_amoled_216/board.h` 寫 `LCD_RESET 2`、`TP_RST 2`,但 schematic netlist 顯示 LCD_RESET 實接 **GPIO39**、TP_RESET 實接 **GPIO40**;`2` 疑似沿用 Waveshare 範例的佔位值。SD 卡 SCK 就是 GPIO2,若韌體同時把 GPIO2 當 reset 拉,SD 時脈會被干擾。**Phase 4 前置**:先把 reset 定義改為 39/40,燒錄實測顯示與觸控正常,才能把 GPIO2 交給 SD(此為 schematic 反推,必須實機驗證)

## Phase 1 — USB 序列資料通道(先加後減,BLE 暫時保留)

韌體:
- `firmware/src/main.cpp` 的 `check_serial_cmd()`(約 170 行處)目前只認 `screenshot`/`buzz`。擴充:收到以 `{` 開頭的行,餵給既有 `parse_json()`(該函式只吃字串,不需修改),成功後以 `Serial.println` 回一行 ack JSON
- 資料處理路徑與 `ble_has_data()` 並存,兩通道同時有效

Daemon:
- 新增 `daemon/claude_usage_daemon_usb.py`:token 讀取(Keychain/credentials.json)、`poll_api()`、`PlanSelector` 原樣沿用;BLE 連線邏輯(discover/reconnect/unpair/backoff,約 300 行)換成 pyserial:用 `serial.tools.list_ports` 以 Espressif VID `0x303A` 自動找埠 → 開埠 → 每 60 秒寫一行 JSON;埠消失則等待重連
- `install-mac.sh`:pip 依賴 bleak 換 pyserial;blueutil 安裝與藍牙權限 priming 區塊(約 159-250 行)整段移除;LaunchAgent 指向新 daemon

驗收:關閉 Mac 藍牙,螢幕用量數字仍每 60 秒更新。

## Phase 2 — 拆 BLE、鍵盤改 USB HID

- `firmware/platformio.ini` S3 env:加 `-DARDUINO_USB_MODE=0`;移除 NimBLE lib_deps 與五個 `CONFIG_BT_NIMBLE_*` flag
- 新增 `firmware/src/usb_hid.{h,cpp}`:以 arduino-esp32 內建 `USBHIDKeyboard` 實作 press(Space)/press(Shift+Tab)/releaseAll
- 刪除 `firmware/src/ble.{h,cpp}` 全部(owner-lock、GATT service、HID report map、advertising 的存在理由隨 BLE 消失)
- `main.cpp`:刪 `pair_tick()` 狀態機(約 242-286 行)、`ble_init`/`ble_tick` 呼叫、BLE 狀態 UI 更新
- `ui.cpp`:藍牙狀態畫面/圖示改為 USB 連線狀態(以 TinyUSB CDC 的 DTR 判斷 host 是否開埠)
- `flash-mac.sh`:燒錄前自動 `launchctl unload` daemon、燒錄後 load 回來(序列埠獨占,daemon 與 esptool/screenshot.sh 會互搶)

驗收:系統藍牙移除 Clawdmeter 後,數字照常更新;右鍵按下,Claude Code 模式照常切換。

## Phase 3 — 按鍵重新配置(可與 Phase 2 合併實作)

| 按鍵 | 新行為 |
|---|---|
| 左鍵(GPIO 0) | 循環切畫面:splash → usage → lightbox → splash |
| 中鍵短按 | splash:切動畫/usage:切亮度/lightbox:下一張圖 |
| 中鍵長按 3 秒 | 移除(原配對手勢) |
| 中鍵按住 8 秒 | 硬體關機(AXP2101 晶片行為,保留) |
| 右鍵(GPIO 18) | Shift+Tab,走 USB HID |

- 改動集中在 `main.cpp` 約 302-352 行的按鍵區塊
- 「睡眠中第一擊只喚醒不觸發」的既有邏輯(idle_consume_wake_press)保留

## Phase 4 — Lightbox(TF 卡梗圖)

前置(腳位已查證,見「已查證的技術前提」):走 SPI,MOSI=1/SCK=2/MISO=3/CS=41。動工前必須先解 GPIO2 reset 衝突——`board.h` 的 `LCD_RESET`/`TP_RST` 改為 39/40 並實機驗證顯示與觸控正常。

- 新增 `firmware/src/sdcard.{h,cpp}`:掛載 TF 卡(FAT32),掃描 `/memes/` 下 `.png`/`.gif` 檔名排序
- 註冊 LVGL `lv_fs` driver 對接 Arduino SD API;開 `LV_USE_LODEPNG=1`、`LV_USE_GIF=1`
- `ui.cpp` 新增 `SCREEN_LIGHTBOX`:全螢幕 `lv_image`(PNG)/`lv_gif`(GIF),中鍵短按換下一張;進入畫面時重新掃描目錄(換圖 = 拔卡改檔再插回,不碰電腦)
- 記憶體:480×480 PNG 解碼後約 900KB,8MB PSRAM 足夠,但需確認 LVGL 影像快取配置走 PSRAM(現有 draw buffer 已有前例)

已知限制:GIF 為 CPU 逐幀解碼,480×480 大 GIF 會掉幀;梗圖建議預先縮至 ≤480px、幀數精簡。

## 執行順序

```
Phase 1(USB 資料通道)→ 驗收 → Phase 2+3(拆 BLE、HID、按鍵)→ 驗收
                                        ↓
                          Phase 4(lightbox)← 先查 TF 腳位
```

Phase 1 是加法,失敗可退回。Phase 2 起為不可逆分岔點:`ble.cpp`、`daemon/`、`install-mac.sh` 從此與 upstream 分道,後續 upstream 更新以 cherry-pick 揀選(避開改動這三處的 commit)。

## 驗證原則

- 每個 phase 完成後所有受影響的 PlatformIO env 必須編譯通過(`pio run -d firmware -e waveshare_amoled_216`;其他板子 env 不得被破壞)
- daemon 既有 pytest 測試(conftest.py、daemon/tests/)必須維持通過
- 實機驗證(燒錄、按鍵、TF 卡)由使用者執行,agent 不得宣稱已做實機驗證

---

## 執行現況(2026-07-06 更新)

| Phase | 狀態 | Commit |
|---|---|---|
| Phase 1(USB 序列資料通道) | ✅ 完成,fresh-context 驗收 PASS with findings(全 low) | `4eea756` |
| Phase 2+3(拆 BLE、USB HID、按鍵重配) | ✅ 完成,驗收 PASS with findings(HID 符號已用 nm 在 ELF 驗證) | `615f2a6`、`05309ee` |
| 實機驗證 | ✅ 2026-07-06 完成:燒錄、TinyUSB composite 列舉、HID 介面(hidutil 確認)、JSON→ack、真實 daemon 端到端、LaunchAgent 常駐 | — |
| Phase 4(lightbox) | ⏸ 擱置(nice to have),TF 腳位已查證(見前文) | — |

尚待使用者人工驗證:左鍵切畫面、右鍵 Shift+Tab、中鍵短按行為、長按 3 秒無反應(負向測試)。

相關文件:`docs/moving-to-another-mac.md`(換電腦使用手冊,commit `7660527`)。

## Phase 4 動工前的 action items(依序處理)

1. **GPIO2 衝突驗證燒錄**:`boards/waveshare_amoled_216/board.h` 的 `LCD_RESET`/`TP_RST` 從 2 改為 39/40,燒錄實測顯示與觸控正常,確認 GPIO2 釋出給 SD_SCK(細節見「已查證的技術前提」風險段)
2. ✅ **統一三處重複的畫面切換邏輯**:`global_click_cb` 與 `ui_cycle_screen` 統一呼叫 `ui_toggle_splash`,避免後續畫面切換邏輯分岔
3. ✅ **修 screenshot.sh 在 TinyUSB CDC 下的截斷**:`send_screenshot()` 改為 4096-byte 分塊寫入並逐塊 flush,避免一次寫入 350–460 KB 截斷。2026-07-06 已在 AMOLED-2.16 S3 實機連續擷取兩張完整 480×480(460800 bytes)螢幕截圖
4. ✅ **清理死碼**:移除 `power_hal_pwr_long_pressed`/`power_hal_pwr_released` HAL、四個板型與 template 的無 caller 實作及相關 IRQ/軟體狀態;保留 PMU 的 8 秒硬體關機設定

## Daemon action items

1. ✅ **暫時抓取失敗時保留最後用量**:韌體收到第一筆有效資料後,持續顯示最後一次成功取得的用量數值;Anthropic API 回傳 HTTP 429 或發生其他暫時性錯誤時不再切換成 `No data`。開機後從未收到有效資料時才顯示 `No data`。此行為由共用 UI 實作,適用所有傳輸與 host daemon。2026-07-06 已在 AMOLED-2.16 S3 實機注入 101%/14%,讓真實 daemon 連續收到 HTTP 429 超過原 90 秒門檻,螢幕仍保留 101%/14%

## 其他已知限制(記錄,不排程)

- serial JSON 通道對任何合法 JSON 照單全收(`{}` 會把用量歸零),USB serial 比 BLE 更容易被人為誤觸;若困擾再加欄位驗證
- daemon 以 VID 0x303A 找埠、first match wins,同時插多片 ESP32 可能選錯;若困擾再鎖 PID/序號
- `usage_core.py` 與 `claude_usage_daemon.py` 的三函式同步複本已加交叉註解防漂移;BLE daemon 檔案保留給 Linux,macOS 已不使用

## Phase 5 — 移除 Linux 平台支援(未開始)

決策記錄:使用者不使用 Linux,整支移除而非跟進修復。2026-07-09 profile-rotation-badge 功能收尾時,最終跨分支審查發現 `daemon/claude-usage-daemon.sh`(Linux BLE bash daemon)仍帶著舊版不可靠的 `PlanSelector` 啟發式邏輯、從未跟進 `who` 欄位/輪播。使用者決定另開獨立 branch/PR 處理,不與該次 badge 功能混合。

待移除:
- `daemon/claude-usage-daemon.sh`
- `daemon/claude-usage-daemon.service`
- `install.sh` 內的 Linux 安裝分支
- `README.md` 的 Linux 段落(bluetoothctl 配對步驟、systemctl 指令、BlueZ 依賴說明)

尚待評估:是否需要同步刪除 daemon 端與 Linux 專屬相關的 config/文件交叉引用;動工時先 grep 一輪確認完整檔案清單。
