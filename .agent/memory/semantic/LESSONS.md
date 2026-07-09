# Semantic Lessons

## LVGL 影像功能黑畫面:先查 lv_malloc 的預設 64KB pool,不要先怪解碼器

- 發生了什麼:lightbox 的 PNG/GIF 實機顯示純黑,memefs 找得到檔案、畫面切得過去。root cause 是 `LV_CONF_SKIP` 之下 LVGL 預設 `LV_USE_STDLIB_MALLOC=LV_STDLIB_BUILTIN` + `LV_MEM_SIZE=64KB`,而 lodepng/gifdec 的所有配置(含解碼輸出 buffer)都走 `lv_malloc`——480×480 RGBA 解碼後要 900KB,必然失敗;`LV_USE_LOG=0` 讓失敗零診斷,看到的「黑」其實是容器底色。
- 正確做法:啟用任何會做大配置的 LVGL 功能(影像解碼、snapshot、canvas、大字型)前,先確認該功能的記憶體走哪條路。修法是該板 env 加 `-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB`,Arduino core 的 `CONFIG_SPIRAM_USE_MALLOC=1`(`ALWAYSINTERNAL=4096`)會把 >4KB 的配置自動送進 PSRAM;順手開 `-DLV_CACHE_DEF_SIZE`(本 repo 用 2MB)避免每次重繪重新解碼。除錯技巧:黑畫面 + 無 log 時,直接靜態讀 `.pio/libdeps/<env>/lvgl/src/lv_conf_internal.h` 的預設值與解碼器原始碼的配置路徑,比開 `LV_USE_LOG` 重燒一輪更快、更確定。

## ARDUINO_USB_MODE=0 的 S3 env 必須配 1200bps touch,否則 macOS 插著燒錄會隨機失敗

- 發生了什麼:改 TinyUSB OTG(`ARDUINO_USB_MODE=0`,USB HID 需要)後,插著燒錄時 esptool 一直 `Connecting...` 到超時。原因:OTG 模式下進 download mode 要靠「活著的韌體」配合——esptool 的 classic DTR/RTS reset 需要韌體端 `USBCDC::_onLineState` 走完精確四步狀態機,macOS 的 CDC 驅動送出的線路事件順序不穩,狀態機常走不完。改模式前(硬體 USB-Serial-JTAG)是晶片硬體處理,永遠成功,所以「最初可以插著燒」。
- 正確做法:每個設 `ARDUINO_USB_MODE=0` 的 env 一律加 `board_upload.use_1200bps_touch = yes` + `board_upload.wait_for_upload_port = yes`,改走 `_onLineCoding` 的 1200bps 單一訊號(可靠)。注意三件事:(1) touch 後裝置會短暫以 ROM 的 USB-JTAG 身分重新列舉(埠名改變,如 usbmodem101),偶發 timing race 造成單次失敗,直接重試即可;(2) 此法仍依賴韌體活著——韌體當機時唯一途徑是按住 BOOT 上電(硬體 strap,永遠有效),這是 OTG 模式的固有限制,不是 bug;(3) daemon 常駐佔埠,燒錄/截圖前要 `launchctl unload`(flash-mac.sh 已內建,手動跑 pio 時要自己處理)。

## 互斥顯示的疊層 widget:每個分支都要顯式藏掉所有其他 sibling;lv_gif 要 auto-pause

- 發生了什麼:lightbox 從 GIF 切到 PNG 後畫面不變。PNG 有畫,但 GIF widget 是後建立的 sibling(z-order 在上層)、顯示 PNG 的分支沒把它藏掉,整張蓋住 PNG。另外兩個 GIF 代價:隱藏的 GIF timer 預設繼續逐幀解碼(吃 CPU、拖垮其他畫面與 serial);升格放大的 GIF 每幀全螢幕 bilinear transform,PARTIAL render 下看得到由上往下逐條刷新。
- 正確做法:(1) N 個互斥顯示的 sibling,每個顯示分支都顯式 hide 其餘 N-1 個,不要只 hide「印象中在顯示的那個」;(2) 隱藏的動畫 widget 必須停掉逐幀工作——本 repo 現行做法是 gif_player 的 timer callback 檢查 `lv_obj_is_visible` 自動暫停(lv_gif 已於 2026-07-09 汰換,見下方 lv_gif 條目);(3) 會被放大的 GIF 關 `lv_image_set_antialias(obj, false)`(nearest-neighbor,梗圖畫質無感、速度差數倍);(4) 滿版裁切用 `LV_IMAGE_ALIGN_COVER` 一個屬性搞定,縮放整數截斷會留最多 1px 縫,可接受;(5) 這類切換流程用 `lightbox_next` serial 指令 + `screenshot.sh` 遠端重現與驗證,不需要人在裝置旁按鍵。注意 visibility 防護的開機路徑:widget 通常在 open 成功「之後」才被 unhide,若首幀渲染走 timer callback 的 visibility 檢查,timer 會被暫停且無人喚醒——首幀要在 open 內直接播,防護只留給穩態。

## LVGL 的 lv_gif 會弄壞透明差分 GIF:直接驅動內建 AnimatedGIF 引擎的 COOKED 模式

- 發生了什麼:ffmpeg 預設輸出的 GIF(`transdiff` 幀間差分,沒變的像素標透明)在 lv_gif 上首幀正常、之後全黑。root cause 在 `lv_gif.c` 的 `gif_blend_to_argb8888`:disposal 0/1/3 的透明像素被寫成 `dst[3]=0`(把畫布 alpha 挖洞),但 GIF 規範語意是「保留上一幀像素」;lv_gif 預設 `color_format=ARGB8888`,必走這條壞路。另注意:LVGL 9.5 內建的 GIF 解碼器是 bitbank2 AnimatedGIF fork,不是舊文件寫的 gifdec——查行為一律以 `.pio/libdeps/<env>/lvgl/src/libs/gif/` 的實際原始碼為準。
- 正確做法:不用 lv_gif widget,直接以 C API 驅動同一顆引擎(`GIF_begin(RGB565_LE)` → `GIF_openFile` → `ucDrawType=GIF_DRAW_COOKED` + `pFrameBuffer`、draw callback 傳 NULL):引擎在 8-bit 索引層正確處理透明/disposal/local palette,並在 `pFrameBuffer + W*H` 維護完整 RGB565 幀,用 `lv_image_dsc_t` 零拷貝指進去餵普通 `lv_image`,每幀 `lv_image_cache_drop(&dsc)` + `lv_obj_invalidate`。緩衝配置 W×H×3 bytes;`GIFIMAGE` 約 24KB 用 heap 不進 BSS。本 repo 實作在 `firmware/src/gif_player.{h,cpp}`,架構參考 github.com/Glebka/esp32-animated-badge。幀率不足時引擎還有 Turbo 模式可開(`pTurboBuffer`)。

## 韌體解碼問題先在 host 重演:把 vendored 函式庫連同 stub 標頭搬到 Mac 編譯

- 發生了什麼:GIF 在裝置上黑屏/顏色錯亂,serial 無 log、每輪重燒要幾分鐘,遠端猜測燒掉大量時間。決定性的工具是把 `.pio/libdeps/<env>/lvgl/src/libs/gif/{gif.c,AnimatedGIF.h}` 原封複製到暫存目錄,寫四個 stub 標頭(`lv_conf_internal.h`=定義 `LV_USE_GIF`、`misc/lv_fs.h`=stdio 包裝、`misc/lv_log.h`=printf、`stdlib/lv_string.h`=memcpy 巨集)後用 clang 編譯,逐幀輸出 PPM 目視+統計。一小時內排除了解碼器、blend 邏輯、串流 IO、短讀、EOF-seek 五類假設,把嫌疑收斂到裝置端環境。
- 正確做法:(1) 裝置上要驗證「同一份程式碼、同一個檔案」的行為時,優先建 host harness,重演要抄 LVGL 呼叫端的確切參數(palette 型別、draw 模式、buffer 佈局),不要憑印象簡化;(2) stub 的保真度決定結論效力——stub 掉的層(本次:widget 可見性、LVGL timer 生命週期)等於沒測到,host 全綠而裝置仍壞時,bug 必在被 stub 掉的層裡,直接往那裡找;(3) 裝置端儀器要標注「量到的是哪個階段的狀態」——本次最後靠認出診斷值(`iPos=0`、`local_pal=0`)是 GIFInit 剛結束的殘留狀態、而非播放後狀態,才鎖定「首幀從未播放」。
