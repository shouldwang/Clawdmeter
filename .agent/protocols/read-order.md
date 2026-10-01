# Core Read Order

這份由 dotfiles `sync-agent-projects` 管理，會被覆寫，不要直接改。
此 repo 專有的 workflow、偏好或規則，另開檔放 `.agent/context/local/` 或 `.agent/protocols/local/`，並登記在 `.agent/AGENTS.md` 的「Project-Specific Extensions」。

## Read Order
1. `.agent/project.toml` — phase, commands, paths (machine-readable source of truth)
2. `.agent/AGENTS.md` — 此 repo 專有檔案的登記與讀取時機
3. `.agent/protocols/repo-rules.md` — repo constraints and editing rules
4. `.agent/protocols/rpi.md` — Research / Plan / Implement phase gates
5. `.agent/protocols/skill-routing.md` — skill routing
6. `.agent/protocols/local/*.md`、`.agent/context/local/*.md` — 依 `.agent/AGENTS.md` 登記的時機讀
7. `.agent/memory/semantic/LESSONS.md` — distilled patterns；做過被糾正的決定前先查
8. `.agent/agents/` — subagent role specs，依觸發條件載入

## Rules
1. Read `project.toml` first — `phase` determines RPI mode and valid verify commands.
2. Check `LESSONS.md` before decisions you have been corrected on before.
3. Session receipts and runtime state go under `.agent/state/`, not repo root.
4. Distill lessons into `LESSONS.md` via the `extract-approach` skill, not ad-hoc edits.
5. If `completed_stages` in RPI state is incomplete, do not skip to Implement.
6. Long output and temp logs go to `.agent/logs/` or `.agent/state/`, not main conversation.
