#!/bin/sh
"exec" "$(command -v python3.13 || command -v python3.12 || command -v python3.11 || command -v python3)" "$0" "$@"

import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True).strip()


def repo_root() -> Path:
    return Path(git("rev-parse", "--show-toplevel"))


def load_manifest(path: Path) -> dict:
    try:
        import tomllib
    except ImportError:
        return {}

    with path.open("rb") as f:
        return tomllib.load(f)


def main() -> int:
    root          = repo_root()
    agent_root    = root / ".agent"
    manifest_path = agent_root / "project.toml"
    if not manifest_path.is_file():
        return 0

    manifest = load_manifest(manifest_path)
    features = manifest.get("features") or {}
    if features.get("memory_capture", True) is False:
        return 0

    episodic_dir = agent_root / "memory" / "episodic"
    state_dir    = agent_root / "state"
    episodic_dir.mkdir(parents=True, exist_ok=True)
    state_dir.mkdir(parents=True, exist_ok=True)

    commit        = git("rev-parse", "HEAD")
    short_commit  = commit[:12]
    subject       = git("log", "-1", "--pretty=%s")
    body          = git("log", "-1", "--pretty=%b")
    changed_files = subprocess.check_output(
        ["git", "show", "--name-only", "--format=", "--no-renames", "HEAD"],
        text=True,
    ).splitlines()
    diffstat = subprocess.check_output(
        ["git", "show", "--stat", "--format=", "--no-renames", "HEAD"],
        text=True,
    ).strip()

    rpi_path = state_dir / "rpi.json"
    rpi_state = json.loads(rpi_path.read_text(encoding="utf-8")) if rpi_path.is_file() else {}

    phase_raw = (manifest.get("project") or {}).get("phase", "dev")
    valid_phases = {"spec", "design", "dev", "qa", "deploy", "iteration"}
    phase = phase_raw if phase_raw in valid_phases else "dev"

    payload = {
        "captured_at":   datetime.now(timezone.utc).isoformat(),
        "commit":        commit,
        "subject":       subject,
        "body":          body,
        "changed_files": changed_files,
        "diffstat":      diffstat,
        "phase":         phase,
        "rpi_state":     rpi_state,
    }

    candidate_path = episodic_dir / f"{datetime.now().strftime('%Y%m%d-%H%M%S')}-{short_commit}.json"
    candidate_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    latest_path = state_dir / "latest-candidate.json"
    latest_path.write_text(
        json.dumps({"path": str(candidate_path)}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    print(candidate_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
