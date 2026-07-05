# Repo Agent Entry

Customize this file to match the read order and rules for this repo.
This file is deployed on bootstrap and is not overwritten by sync.

## Config
- `.agent/project.toml` — phase, commands, paths (machine-readable source of truth)

## Shared Core
- `/Users/shouldwang/Documents/GitHub/dotfiles/agent/AGENTS.md` — shared persona, language, execution contract, and base protocols

## Memory (read in this order)
- `.agent/memory/personal/PREFERENCES.md` — stable user conventions
- `.agent/memory/semantic/LESSONS.md` — distilled patterns
- `.agent/memory/episodic/` — recent session captures (top few by recency)
- `.agent/memory/local/*.md` — optional repo-local durable memory; inspect filenames or index first when present

## Protocols
- `.agent/protocols/skill-routing.md` — repo-local skill routing
- `.agent/protocols/repo-rules.md` — repo constraints and editing rules
- `.agent/protocols/rpi.md` — Research / Plan / Implement phase gates
- `.agent/protocols/local/*.md` — optional repo-local rules; read relevant files when present

## Context Extensions
- `.agent/context/local/*.md` — optional repo-local context extensions; read relevant files when present

## Agents
- `.agent/agents/` — available subagent role specs (load on demand by trigger)

## Rules
1. Read `project.toml` first — `phase` determines RPI mode and valid verify commands.
2. Check `LESSONS.md` before decisions you have been corrected on before.
3. Session receipts and runtime state go under `.agent/state/`, not repo root.
4. Never hand-edit `LESSONS.md` — use episodic capture first; review before graduating.
5. If `completed_stages` in RPI state is incomplete, do not skip to Implement.
6. Long output and temp logs go to `.agent/logs/` or `.agent/state/`, not main conversation.
