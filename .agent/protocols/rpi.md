# RPI Protocol

RPI = Research -> Plan -> Implement

## 流程說明

每個 session 開始時，RPI 狀態重置為 Research。完成各階段後，呼叫以下指令明確寫入 receipt：

```bash
python3 .agent/scripts/hooks/git-dev-hook.py write-receipt Research "研究摘要"
python3 .agent/scripts/hooks/git-dev-hook.py write-receipt Plan "計畫摘要"
```

`completed_stages` 會在 `rpi.json` 中累積追蹤。進入 Implement 時，若 `missing_receipts` 非空，post-edit hook 會提示補齊並附上可直接執行的指令。

Research receipt 應回答：目前已知事實、未解歧義、既有模式或風險。
Plan receipt 應回答：成功條件、預計動哪些檔案、如何驗證、若遇到衝突採哪套模式。

## Phase 與 RPI 強制程度

| Phase     | RPI 強制程度  | 說明                          |
|-----------|-------------|-------------------------------|
| spec      | inform-only | 需求撰寫階段，不強制 R/P        |
| design    | inform-only | 架構設計階段，不強制 R/P        |
| dev       | complex-only | 複雜任務強制（預設）            |
| qa        | complex-only | 複雜任務強制                   |
| deploy    | always      | 任何改動均強制 R/P              |
| iteration | complex-only | 複雜任務強制                   |

Phase 在 `.agent/project.toml` 的 `[project] phase` 欄位設定，可隨開發進度切換。

## 複雜任務判斷（complex-only 的觸發條件）

以下情況視為複雜任務，必須先有 Research 與 Plan receipt，再進 Implement：

- 變更跨多個檔案
- 命中 `project.toml` 的 `paths.high_risk`
- 涉及 shared protocol、shared hook、adapter 接線
- 涉及資料、部署、依賴或 lockfile 風險
- 需求本身不清楚，需要先釐清再實作

## 單點任務（不強制 R/P）

以下情況可直接實作，不強制先做 R/P：

- 單檔、低風險、局部修補
- 純查詢、解釋、或不改 repo-tracked 檔案的探索

即使不強制 R/P，若任務中途失去脈絡，也應停下來重述現況與剩餘風險，再繼續。

## Checkpoint 原則

- 多步驟任務每完成一段，都應能用一句話說明：做了什麼、驗了什麼、還差什麼。
- 如果無法描述目前狀態，就不要在那個狀態上繼續疊改動。
- receipt 與 stop 摘要的目的是保留這些 checkpoint，而不是產生形式化文件。

## Receipt 路徑

| 用途              | 路徑                                  |
|------------------|---------------------------------------|
| RPI 狀態          | `.agent/state/rpi.json`              |
| 最近 receipt      | `.agent/state/rpi-latest.md`         |
| Research receipt  | `.agent/state/receipt-research.md`   |
| Plan receipt      | `.agent/state/receipt-plan.md`       |
