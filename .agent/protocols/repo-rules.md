# Repo Rules

- `./.agent/project.toml` 是這個 repo 的 machine-readable source of truth。
- `./.agent/scripts/hooks/` 只做 inform、verify 建議、receipt、memory capture；不自動 commit、push、deploy。
- repo-local `.agent/` 是 overlay：bootstrap 之後由 repo 自行維護，不與 dotfiles 自動同步。
- repo-local 新增的 context / rules，預設放到固定 extension folders：`.agent/context/local/`、`.agent/protocols/local/`。durable memory 只分兩層：事件紀錄進 `.agent/memory/episodic/`，已蒸餾規則進 `.agent/memory/semantic/LESSONS.md`。
- 長輸出與暫存資訊寫到 `./.agent/logs/` 或 `./.agent/state/`，不要把大段 log 貼回主對話。
- semantic memory 只接受 review 過的內容；`memory/episodic/` 只是候選與審計資料。
- 預設使用繁體中文（台灣）。
- 預設先研究再下手，除非是明顯單點修補。
- git push 一律走 gh：`gh auth setup-git && git push`，不改 remote URL 為 SSH。
- 分支命名：`feat/<描述>` / `fix/<描述>` / `chore/<描述>`；功能開發、bug fix、實驗性修改都在獨立分支，不直接在 main commit。若 repo 例外（如 dotfiles），在 `project.toml` 設 `branch_required = false`。

## 變更範圍

- 預設採外科手術式修改：只動必要檔案與必要區塊，不順手清掃相鄰 code、註解、格式。
- 新增或改寫程式前，至少先讀目前檔案、主要 caller、相關 export、明顯共用 utility。
- 如果看見兩套衝突慣例，不要混成第三套；選一套較新、較常被驗證或較接近當前模組的寫法，並把另一套標成 cleanup 候選。
- repo 內一致性優先於個人口味；除非有明確風險，否則先貼齊既有慣例再說。

## 實作邊界

- 模型只處理判斷型工作；routing、retry、狀態碼判斷、確定性資料轉換應交給一般程式碼。
- 預設採最小可行實作，不加需求外功能，不為單次使用情境抽抽象。

## 驗證與回報

- 開始實作前要能說明成功條件；如果成功條件仍模糊，先補研究或計畫，不直接寫碼。
- 測試不只要過，還要能保護意圖；若驗證只覆蓋表面行為，不能把它算成充分驗證。
- 任何 skipped check、未跑測試、部分成功、edge case 未驗，都要直接揭露，不能用「完成」包掉。
