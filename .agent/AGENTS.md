# Repo Agent Entry

先讀 `.agent/protocols/read-order.md`（核心讀取順序與通用規則，由 dotfiles sync 管理，會隨核心更新）。
本檔只放此 repo 專有的內容，deployed on bootstrap and is not overwritten by sync.

## Memory
- `.agent/memory/local/*.md` — optional repo-local durable memory; inspect filenames or index first when present

## Project-Specific Extensions
專案特有的 workflow、偏好或規則，不要改會被 sync 覆蓋的檔案，另開新檔，並在這裡登記讀取順序：
1. 新檔放 `.agent/context/local/<name>.md`（背景、流程、整合限制）或 `.agent/protocols/local/<name>.md`（規則、偏好）。
2. 在本節加一行 `- <path> — <何時讀>`，沒登記的檔案不保證會被讀到。
- `.agent/context/local/project-context.md` — 韌體架構、板子目錄結構、新增板子流程；動 firmware 前讀
