#!/usr/bin/env python3

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def render_lessons(lessons_path: Path, markdown_path: Path) -> None:
    items = []
    if lessons_path.is_file():
        with lessons_path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    items.append(json.loads(line))

    lines = ["# Semantic Lessons", ""]
    if not items:
        lines.append("尚未有已 review 的 repo-level lessons。")
    else:
        for item in items:
            title = item.get("title") or "Untitled lesson"
            lesson = item.get("lesson") or ""
            source_commit = item.get("source_commit") or ""
            lines.append(f"## {title}")
            if source_commit:
                lines.append(f"- Source commit: `{source_commit}`")
            lines.append(f"- Lesson: {lesson}")
            lines.append("")

    markdown_path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate")
    parser.add_argument("--title", default="")
    parser.add_argument("--lesson", required=True)
    args = parser.parse_args()

    candidate_path = Path(args.candidate)
    candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
    agent_root = candidate_path.parents[2]
    lessons_path = agent_root / "memory" / "semantic" / "lessons.jsonl"
    markdown_path = agent_root / "memory" / "semantic" / "LESSONS.md"

    title = args.title or candidate.get("subject") or f"Lesson from {candidate.get('commit', '')[:12]}"
    record = {
        "title": title,
        "lesson": args.lesson,
        "source_commit": candidate.get("commit", ""),
        "promoted_at": datetime.now(timezone.utc).isoformat(),
    }

    lessons_path.parent.mkdir(parents=True, exist_ok=True)
    with lessons_path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")

    candidate["review"] = {
        "title": title,
        "lesson": args.lesson,
        "promoted_at": record["promoted_at"],
    }
    candidate_path.write_text(json.dumps(candidate, ensure_ascii=False, indent=2), encoding="utf-8")

    render_lessons(lessons_path, markdown_path)
    print(markdown_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
