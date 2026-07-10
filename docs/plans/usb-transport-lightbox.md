# USB 傳輸與 Lightbox 遷移計畫

日期:2026-07-05(2026-07-09 更新 Phase 4 梗圖來源決策)
對象板子:Waveshare ESP32-S3-Touch-AMOLED-2.16(USB 供電、無電池)
決策記錄:BLE 整個拆除;鍵盤功能改走 USB HID;梗圖來源原規劃用 TF 卡,2026-07-09 改為內建 flash SPIFFS 分區(理由見 Phase 4)。

## 背景與動機

目前資料通道走 BLE GATT(daemon 透過藍牙把用量 JSON 寫進裝置),側鍵透過 BLE HID 送快捷鍵。BLE 帶來大量維運複雜度:macOS bonding 自我修復、owner-lock 機制、藍牙權限 priming、配對手勢。本裝置無電池、常駐 USB 供電,改走 USB 有線傳輸可移除整類問題。

使用者需求:
1. 改用 USB 連線,消除藍牙配對問題
2. 側鍵重新配置(語音模式左鍵不使用、中鍵長按三秒配對手勢移除)
3. 新增 lightbox 畫面:顯示梗圖(PNG/GIF),來源為裝置內建 flash 的 SPIFFS 分區(原規劃 TF 卡,2026-07-09 改為此案,見 Phase 4)

## 已查證的技術前提

- Type-C 是 ESP32-S3 原生 USB(非 UART bridge),repo 已設 `ARDUINO_USB_CDC_ON_BOOT=1`
- 目前 S3 env 未設 `ARDUINO_USB_MODE`,預設 HW CDC/JTAG 模式,該模式無法做 USB HID;必須改 `ARDUINO_USB_MODE=0`(TinyUSB OTG)才能 CDC+HID composite。切換後燒錄時序列埠會短暫重新列舉,需實測
- LVGL 9 內建 lodepng(PNG)與 gifdec(GIF)解碼器,開 `LV_USE_LODEPNG=1`、`LV_USE_GIF=1` build flag 即可,無需第三方庫
- **已查證:2.16 板的 TF 卡腳位(2026-07-05,官方 schematic + docs.waveshare.com wiki 雙來源一致)**:走 **SPI**——MOSI=GPIO1、SCK=GPIO2、MISO=GPIO3、CS=GPIO41。卡座 D1/D2 未接線,硬體上無法 4-bit SDMMC。來源:`files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.16/ESP32-S3-Touch-AMOLED-2.16-Schematic.pdf`(SD-CARD 區塊 netlist)與 wiki 的 GPIO pin assignments 表
- **風險:GPIO2 與 board.h 的 reset 定義衝突**。repo 的 `boards/waveshare_amoled_216/board.h` 寫 `LCD_RESET 2`、`TP_RST 2`,但 schematic netlist 顯示 LCD_RESET 實接 **GPIO39**、TP_RESET 實接 **GPIO40**;`2` 疑似沿用 Waveshare 範例的佔位值。SD 卡 SCK 就是 GPIO2,若韌體同時把 GPIO2 當 reset 拉,SD 時脈會被干擾。2026-07-09 用 docs.waveshare.com 官方 wiki 二次查證,腳位數字與上述一致。**此風險項目僅在走 TF 卡方案時才是 Phase 4 前置阻塞項;2026-07-09 決定改走內建 flash SPIFFS 方案後,不再需要處理此項**(見 Phase 4)
- **已查證:`default_8MB.csv` 既有 SPIFFS 分區**(2026-07-09):`firmware/platformio.ini` 的 `waveshare_amoled_216` env 未覆寫 `board_build.partitions`,沿用 ESP32-S3 預設 8MB flash 分區表 `default_8MB.csv`——`app0`/`app1` 各 3,276,800 bytes(~3.19MB,現有韌體實測約 1.43MB,留白充足),`spiffs` 分區 1,572,864 bytes(~1.5MB,位於 `0x670000`)目前完全未使用。幾張幾 KB 的梗圖圖片放進這 1.5MB 完全足夠,不需修改 partition table

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

## Phase 4 — Lightbox(內建 flash SPIFFS 梗圖)

**決策變更(2026-07-09)**:原規劃走 TF 卡,前置需先解 GPIO2 reset 衝突(`board.h` 的 `LCD_RESET`/`TP_RST` 改 39/40 並實機驗證),屬於不可逆的硬體腳位改動,且需另購 TF 卡與讀卡機。使用者評估梗圖數量不多、單張僅數 KB,改用裝置內建 flash 的既有 **SPIFFS 分區(1.5MB,`0x670000`,見「已查證的技術前提」)**,理由:
1. 完全不碰 GPIO2,免除 reset 衝突與實機驗證風險
2. 不需額外硬體(TF 卡、SPI 走線)
3. 容量對「圖片不多、幾 KB 等級」的需求綽綽有餘

取捨:換圖流程從「插電腦拖檔案到 TF 卡」變成「改本機圖片檔案 → `pio run -t uploadfs` 重新燒錄 SPIFFS 分區」,不再是隨插即用,需要接上 USB 線並跑一次燒錄指令。

**按鍵功能**(沿用 Phase 3 既有配置,不新增按鍵):
- 左鍵:循環切畫面 splash → usage → lightbox → splash
- 中鍵短按:lightbox 畫面下顯示下一張圖,到最後一張循環回第一張

**圖片存入與更新機制**:
- 素材原始檔案(PNG/GIF)存放於 repo 內新增的 `firmware/data/memes/` 目錄(PlatformIO SPIFFS 慣例路徑),依檔名排序決定播放順序,建議用數字前綴命名(如 `01_xxx.png`、`02_xxx.gif`)避免字母序造成非預期順序
- 更新圖片:在 `firmware/data/memes/` 增刪/替換檔案後,執行 `pio run -d firmware -e waveshare_amoled_216 -t uploadfs` 只燒錄 SPIFFS 分區(不需重新編譯/燒錄 app 分區,較全量燒錄快);韌體端進入 lightbox 畫面時重新掃描目錄,反映最新檔案列表
- 空目錄或 SPIFFS 掛載失敗:顯示明確空狀態文字(如「No memes found」),不當機、不黑屏

**存入方式**:
- 新增 `firmware/src/memefs.{h,cpp}`(取代原規劃的 `sdcard.{h,cpp}`):掛載 SPIFFS,掃描 `/memes/` 下 `.png`/`.gif` 檔名排序
- 註冊 LVGL `lv_fs` driver 對接 Arduino `SPIFFS` API;開 `LV_USE_LODEPNG=1`、`LV_USE_GIF=1`(目前 platformio.ini 尚未設定這兩個 flag,需新增)
- `ui.cpp` 新增 `SCREEN_LIGHTBOX`:全螢幕 `lv_image`(PNG)/`lv_gif`(GIF),中鍵短按換下一張;進入畫面時重新掃描目錄
- `platformio.ini`:`waveshare_amoled_216` env 需確認/設定 SPIFFS 相關 build 設定以支援 `uploadfs` target(沿用 `default_8MB.csv` 既有 spiffs 分區,不需改 partition table)

**圖片格式、尺寸建議**:
- 格式:PNG(靜態)、GIF(動態),不支援 JPEG(LVGL 9 內建解碼器只含 lodepng/gifdec)
- 尺寸:建議 ≤480×480(對應面板實際解析度),避免韌體端額外做縮放運算;**(2026-07-09 更新)韌體端已改為 `LV_IMAGE_ALIGN_COVER` 滿版裁切**——非正方形圖會保持比例放大到填滿螢幕、超出部分置中裁掉。GIF 放大走 nearest-neighbor(關 antialias),仍建議 GIF 預先縮到接近 480 寬以減少每幀轉換成本
- 檔案大小:單張建議控制在數十 KB 內,SPIFFS 分區總容量 1.5MB,需為多張圖片預留空間(例如 10 張圖各 100KB 內即可安全落在容量內,含檔案系統 overhead)
- GIF:為 CPU 逐幀解碼,幀數多或解析度大會掉幀;建議預先縮至 ≤480px、精簡幀數與色板,以維持流暢度且降低檔案大小

記憶體:PNG/GIF 解碼緩衝區使用同前規劃,480×480 全彩解碼後約 900KB,8MB PSRAM 足夠,但需確認 LVGL 影像快取配置走 PSRAM(現有 draw buffer 已有前例)。此部分與存放媒介(SD 卡或 SPIFFS)無關,不受本次決策變更影響。

## 執行順序

```
Phase 1(USB 資料通道)→ 驗收 → Phase 2+3(拆 BLE、HID、按鍵)→ 驗收
                                        ↓
                          Phase 4(lightbox,內建 SPIFFS,不再需要先查 TF 腳位)
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
| Phase 4(lightbox) | ✅ 完成:黑畫面 bug 已修(root cause:LVGL 內建 64KB pool 放不下解碼緩衝;改 `LV_STDLIB_CLIB` 走系統 malloc → PSRAM)。2026-07-09 實機驗證 PNG(480×480)與 GIF(266×266)均正常渲染——見下方「Phase 4 已知問題(已解決)」 | — |

尚待使用者人工驗證:左鍵切畫面、右鍵 Shift+Tab、中鍵短按行為、長按 3 秒無反應(負向測試)。

相關文件:`docs/moving-to-another-mac.md`(換電腦使用手冊,commit `7660527`)。

## Phase 4 已知問題(已解決,2026-07-09)

**Root cause(已確認並修復)**:`LV_CONF_SKIP` 之下 LVGL 預設 `LV_USE_STDLIB_MALLOC=LV_STDLIB_BUILTIN`、`LV_MEM_SIZE=64KB`。lodepng 與 gifdec 的所有配置(含解碼輸出的 `lv_draw_buf`)都走 `lv_malloc`,而 480×480 RGBA PNG 解碼後要 921,600 bytes、266×266 GIF canvas 要約 283KB,64KB pool 必然配置失敗;`LV_USE_LOG=0` 讓失敗完全靜默,看到的黑色是 `lightbox_container` 的 `COL_BG` 底色。下方原假設 1(黑色是底色透出)與假設 2(解碼緩衝記憶體不足)都命中,只是層級在 lv_malloc 的 pool,不在 image cache。

**修復**:`waveshare_amoled_216` env 加 `-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB`,讓 `lv_malloc` 走系統 `malloc`;Arduino core 的 `CONFIG_SPIRAM_USE_MALLOC=1`(`ALWAYSINTERNAL=4096`)會把 >4KB 的配置自動放進 PSRAM。2026-07-09 實機驗證:PNG 與 GIF 均正常渲染(screenshot 確認)。

## Phase 4 後續:lv_gif 汰換為自製 COOKED player(2026-07-09 深夜)

**症狀**:換入 ffmpeg 轉出的 240×240 GIF 後 lightbox 黑屏;繞過後又出現整張顏色錯亂(橘變綠)。

**根因(兩層,均已實機+host harness 驗證)**:
1. **LVGL 9.5 `lv_gif` widget 的 blend 層 bug**:`gif_blend_to_argb8888` 對 disposal 0/1/3 的透明像素做 `dst[3]=0`(把畫布 alpha 挖洞),但 GIF 規範的語意是「保留上一幀像素」。ffmpeg 預設 `transdiff`(幀間差分,不變的像素標透明)的 GIF 首幀正常、之後 85%+ 像素被挖洞 → 容器黑底透出。lv_gif 預設 `color_format=ARGB8888`,一定走這條路。
2. 換自製 player 初版還有一個自造 bug:timer callback 的 visibility 防護在「widget 尚未 unhide 就 open」時把 timer 永久暫停(從 PNG 切到 GIF 必中,從 GIF 切 GIF 不中——因為 widget 已可見)。修法:首幀在 open() 內直接 `GIF_playFrame`,不經過 timer callback。

**解法**(參考 github.com/Glebka/esp32-animated-badge 的架構):新增 `firmware/src/gif_player.{h,cpp}`,直接以 C API 驅動 LVGL 內建的 AnimatedGIF 引擎(bitbank2 fork,`GIF_DRAW_COOKED` 模式 + `pFrameBuffer`、無 draw callback):引擎自行做幀合成(透明、disposal、local palette 全在索引層正確處理),每幀產出完整 RGB565 畫布,以 `lv_image_dsc_t` 零拷貝餵給普通 `lv_image` widget。`ui.cpp` 的 lightbox 不再使用 `lv_gif`。無 decoder 的板子 stub 化(同 memefs 模式),四個 env 編譯均通過。

**成果**:
- 透明差分 GIF 直接可播——**轉檔不再需要 `-gifflags -transdiff` 特規**,網路抓的 GIF 原樣丟進 `data/memes/` 即可
- 顏色正確(RGB565 palette 直轉,不經 ARGB8888)
- 2026-07-09 實機驗證:PNG×2、透明差分 GIF(162KB)、綠幕貓 GIF(120KB)全部正常渲染與動畫,含「PNG→GIF 切換」原死亡路徑
- 記憶體:每支 GIF 開啟時配置 W×H×3 bytes(240×240 約 173KB,PSRAM)+ GIFIMAGE 約 24KB;關閉/切圖即釋放
- 效能備考(2026-07-10 更新,turbo 已啟用並實機驗證):實機加計時 log 量測,`03_its_fine.gif` 非 turbo 穩定態解碼耗時穩定重現 45ms/90ms(約半個幀預算),`04_oiiai.gif` 遠低於半個預算。試開引擎的 Turbo 模式(`pTurboBuffer`)確實把前者的耗時壓到量不到,但當時素材尚未 coalesce,實機播放**立刻出現透明區域花屏**——`DecodeLZWTurbo` 的 COOKED 合成把每條掃描線寫進 `pTurboBuffer` 內部一塊逐行覆寫的 scratch 區、再靠 `pfnDraw` callback 交出,disposal 0/1/3「保留原樣」的透明像素在這條路徑下讀到的是殘留 LZW 字典垃圾,不是真正的前一幀像素(非 turbo 路徑因為直寫持久的 `pFrameBuffer` 才沒有這個問題)。當時已還原 turbo 相關程式碼,只保留計時 log。**結案(2026-07-10 稍晚)**:素材 coalesce 完成後重開 turbo,改用正確的 `pfnDraw` scanline-relay callback(把引擎交出的 cooked RGB565 掃描線 memcpy 進持久 `framebuf + w*h` 的對應行,而不是直接沿用 scratch buffer 的內容),`pTurboBuffer` 依 `gif.c` 實際佈局配置為 `W×H + TURBO_BUFFER_SIZE`。實機重新驗證:`03_its_fine.gif` 首幀解碼降到 18ms/90ms 預算,穩定態在兩段 8 秒監測(涵蓋 2+ 輪循環)內都沒有再觸發「耗時≥半預算」的計時警告,相對 45ms/90ms 基準線是明確改善。`04_oiiai.gif` 首幀 15ms/30ms 預算,但穩定態每輪固定會出現約一次 32ms 尖峰(超出 30ms 預算),推測與 GIF 內部週期性 LZW 字典重置幀有關;由於沒有「coalesce 素材 + 非 turbo」的對照組數字,這支素材 turbo 前後是否真的變快仍是**待釐清的 open question**,不算 clean win,留給下一次要動這支素材時再查。全程 `pDraw->ucHasTransparency` 警告防護零觸發,確認 coalesce 素材確實不再依賴幀間透明差分。細節見 `.agent/memory/semantic/LESSONS.md`「AnimatedGIF 的 Turbo 模式跟『保留前一幀透明像素』互斥」條目
- 素材 coalesce 實驗(2026-07-10):兩支 GIF 都是 ezgif.com/optimize 輸出、`disposal asis` + per-frame 透明子矩形的高度差分優化格式,直接 `gifsicle --unoptimize` 會報 "too complex to unoptimize"(local color table/transparency 太複雜)。兩段式才成功:`gifsicle --colors=255` 先把 local palette 併成單一 global palette,再 `gifsicle --unoptimize --lossy=80` 才能把每幀變成完整獨立畫面(`gifsicle --info` 確認每幀都是滿版 240×240、無 transparent/disposal 標記)。代價:檔案暴增到原本的 3.7-4.4 倍(`03_its_fine.gif` 163KB→608KB,`04_oiiai.gif` 120KB→532KB);**更正(2026-07-10 稍晚查證)**:此處原記載 SPIFFS partition 3.375MB(`default_16MB.csv`)有誤——`waveshare_amoled_216` env 並未覆寫 `board_build.partitions`(對照 platformio.ini 其他三個 env 都有設 `default_16MB.csv`,唯獨此 env 沒設),實際 build 出的 `partitions.bin` 解析結果沿用 ESP32-S3 預設 `default_8MB.csv`:**spiffs 1.5MB @ 0x670000**。**結案(2026-07-10 稍晚)**:coalesce 素材已實際燒錄上裝置(`pio run -d firmware -e waveshare_amoled_216 -t uploadfs`),turbo 模式重開並通過上方效能備考的實機驗證。SPIFFS 實測用量 1,086,278 bytes(~1.086MB)/1.5MB,headroom 剩約 400KB——後續要再加新梗圖前得先評估這個 headroom 夠不夠,選項是把此 env 也對齊其他三個 env 的 `default_16MB.csv`(代價:全量重燒、SPIFFS/NVS 清空,需先 `esptool.py flash_id` 唯讀查證實體 flash 大小)。同時新增一個 QA 用 serial 指令 `screen_next`(`firmware/src/main.cpp`,等同實體 PRIMARY 鍵呼叫 `ui_cycle_screen()`),讓 `lightbox_next` 驗證能在不派人到裝置旁的情況下,遠端把畫面切到 lightbox 再走 PNG→PNG→GIF→GIF→PNG 全循環。
- 排查副產品(2026-07-10):量測過程中一度以為「畫面由上而下逐條刷新」是 coalesce 造成的新問題,經過「還原素材、現象仍在」的對照實驗排除——這是放大 GIF 在 LVGL PARTIAL render 下的既有特性(見上方「互斥顯示的疊層 widget」lesson 條目),跟這次改動無關,不用重複調查。

**燒錄週邊發現(同日)**:`ARDUINO_USB_MODE=0`(TinyUSB OTG)之後,esptool 的 classic DTR/RTS reset 需要韌體端 `USBCDC::_onLineState` 走完四步狀態機,macOS CDC 驅動下不可靠(實測失敗一次),導致「插著無法燒錄、要拔掉按 BOOT」。已在該 env 加 `board_upload.use_1200bps_touch = yes` + `board_upload.wait_for_upload_port = yes`,改走 `_onLineCoding` 的 1200bps 單一訊號;實測連續三次插著直接燒錄成功。註:此法仍依賴韌體活著——若韌體當機,唯一途徑仍是按住 BOOT 進 ROM download mode(硬體 strap,永遠有效)。

## Phase 4 後續:GIF player 效能調查(比對 AnimatedGIF 生態系四個 repo,2026-07-10)

**動機**:`gif_player.cpp` 換成 upstream bitbank2/AnimatedGIF(`0e832fd`)後想知道還有沒有明顯漏掉的效能/記憶體做法,對照四個參考 repo:`bitbank2/AnimatedGIF`(本體,含官方 examples)、`thelastoutpostworkshop/AnimatedGIF340_240`、`thelastoutpostworkshop/animated_gif_memory`、`maker-community/esp32-emoji-gif-component`。前三個已 clone 在 `.agent/state/gif-review/`,第四個用 `gh api` 抓 README/原始碼。

**現況基準**(比對用):兩支 GIF 素材皆 240×240、已 coalesce(見上方 turbo 段落);`framebuf`(w×h×3,持久,給 LVGL 當 image source)與 `turbobuf`(turbo 解碼暫存)目前都經 `psram_calloc()` 配置在 PSRAM;逐幀播放走 SPIFFS `File` 的 open/read/seek callback,不是記憶體常駐;`pfnDraw` 傳 `NULL`,靠 library 內部預設寫入 framebuffer。

**結論(採納,待實作)**:
1. **`turbobuf` 應搬到 internal SRAM,不該留在 PSRAM**——upstream `gif_benchmark.ino` 註解明講 turbo scratch buffer 落在 PSRAM 可能比不開 turbo 還慢,要用 `heap_caps_malloc(..., MALLOC_CAP_8BIT)`(不帶 `MALLOC_CAP_SPIRAM`)逼進 internal SRAM。以我們 240×240 素材算,`turbobuf` 只有 240×240 + `TURBO_BUFFER_SIZE`(`0x6100`)≈82KB,裝置 internal SRAM 目前用量僅 18.8%(61,476/327,680 bytes),餘裕充足,裝得下。`framebuf` 因為要跨幀持久且較大(240×240×3≈173KB),留在 PSRAM 不動。
2. **`gif_player_open()` 應把 SPIFFS 檔案整支讀進 PSRAM buffer 後再 `gif.open(pData, iDataSize, ...)`,取代現在的逐幀 File read/seek callback**——四個 repo 裡「快」的路徑全部是記憶體常駐(compile-time C array 或執行期 buffer),不是逐幀走檔案系統。板子有 PSRAM(`BOARD_HAS_PSRAM` + `qio_opi`),兩支素材各 500–600KB,PSRAM 塞得下,同時保留 memefs 目錄動態選片的彈性(不像 thelastoutpostworkshop 兩個 repo 那樣把 GIF 編死進 `.h` 陣列、換片要重燒)。預期是四項裡效益最明確的一項。

**驗證方式**:兩項改完後用既有的 `gif_player: ... first frame decode Xms` / `decode Xms (budget Yms)` log 量測前後差異,不用另外搭測試環境;`03_its_fine.gif`(90ms 預算)與 `04_oiiai.gif`(30ms 預算,目前穩定態每輪有一次 ~32ms 尖峰超預算)都要各看幾輪穩定態,不能只看首幀。

**結論(暫緩,列為 open question,不在這輪動)**:
3. **ESP32-S3 的 SIMD 加速 `cookPixels()`(`src/s3_transparent.S`)可能沒被吃到**——官方 `esp32s3_simd_demo` 是在自訂 GIFDraw callback 裡手動呼叫 `cookPixels()` 才走這條路,我們現在 `pfnDraw=NULL` 靠 library 內部預設寫入,內部預設路徑有沒有自動用 SIMD 沒查證過,要先讀 `AnimatedGIF.cpp` 裡 `pfnDraw==NULL` 的分支實作才能判斷值不值得做。現在兩支素材首幀解碼都在 13–17ms,離預算還有餘裕,優先度低於上面兩項,留到之後解析度變大或要同時播多支才回頭查。
4. **雙緩衝 DMA push(`animated_gif_memory` 的 ping-pong 手法)不適用**——那是繞過高階 UI 框架直接控 `bb_spi_lcd`,我們的顯示走 LVGL flush pipeline,DMA 與否是 LVGL 顯示驅動層的事,`gif_player.cpp` 管不到,除非日後決定為 lightbox 繞開 LVGL。
5. **多實例並行解碼(`two_instances` 的雙 `AnimatedGIF` 物件模式)不適用**——目前單張全螢幕播放用不到,除非之後要做 crossfade 或並排顯示。

**GIF 前處理**:四個 repo 都沒有給出具體的 palette/尺寸/幀數/disposal/dithering 編碼建議,我們自己在 LESSONS.md 記的 `gifsicle --colors=255` 併 palette 再 `--unoptimize --lossy=80` coalesce 流程已經是目前找得到最具體的做法,沒有東西可以從這幾個 repo 再吸收進來。唯一旁證:`esp32-emoji-gif-component` 的六支表情 GIF 全部裁到 240×240,跟我們現有素材尺寸一致,印證現在的來源尺寸選擇合理,不用因為這次調查改動素材規格。

---

以下為當時的原始記錄(保留供追溯):

### 原始記錄(2026-07-09 實機回報,待下個 session 修復)

**現象**:app + SPIFFS 都已燒錄成功,`memefs_rescan()` 確認找到 3 個檔案(`memefs: found 3 meme(s) in /memes` 已在 serial log 驗證),左鍵能切到 lightbox 畫面,但畫面顯示黑色,梗圖沒有正常畫出來。

**已排除的假設**:曾懷疑是 `lv_lodepng_init()` 沒被呼叫(decoder 沒註冊),但讀 `lv_init.c:387` 確認 `LV_USE_LODEPNG=1` 時 `lv_init()` 會自動呼叫 `lv_lodepng_init()`,不需要手動註冊——這條路排除。

**目前最吻合的假設(尚未驗證,下個 session 先查這個)**:
1. `theme.h` 的 `COL_BG`(= `THEME_BG`)是純黑 `0x000000`,而目前 build flag 設了 `LV_USE_LOG=0`——代表 LVGL 內部任何解碼失敗都完全沒有診斷輸出。「黑色」很可能就是 `lv_image`/`lv_gif` 解碼失敗、widget 沒畫出東西,使用者看到的其實是 `lightbox_container` 的純黑背景透出來,不是梗圖本身變黑。
2. 對應到本文件更早就寫過的一個未解問題(「Phase 4」段落原文):「PNG/GIF 解碼緩衝區使用同前規劃,480×480 全彩解碼後約 900KB,8MB PSRAM 足夠,但需確認 LVGL 影像快取配置走 PSRAM」——這件事當時就標記為待確認,現在很可能就是命中點:LVGL 9 預設的 image cache 大小可能不足以放下 ~900KB 的解碼緩衝(RGBA8888,480×480),導致 decode 靜默失敗或被 cache 立刻驅逐。

**下個 session 建議的除錯順序**:
1. 先暫時把 `LV_USE_LOG=1`(或直接加 `lv_image_decoder_open` 附近的 Serial 診斷)重新燒一次,確認解碼是否真的報錯、報什麼錯
2. 查 LVGL 9 的 image cache 設定(`LV_CACHE_DEF_SIZE` 或等效 build flag)目前預設值,以及是否有走 PSRAM;必要時顯式配置 image cache 用 PSRAM 配置器
3. 若懷疑檔案本身有問題,先用一張很小(例如 32×32)的純色 PNG 測試,排除是「檔案太大 vs 解碼邏輯本身錯」的變因

不要跳過第 1 步直接猜著改——目前完全沒有解碼錯誤訊息可看,先把診斷打開再動手。

## Phase 4 動工前的 action items(依序處理)

1. ~~GPIO2 衝突驗證燒錄~~:僅適用於原 TF 卡方案。2026-07-09 決策改走內建 flash SPIFFS 後,不需要修改 `board.h` 的 `LCD_RESET`/`TP_RST`,此項目不再是 Phase 4 前置阻塞項(細節見「已查證的技術前提」風險段)
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
