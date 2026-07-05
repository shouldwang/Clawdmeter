#!/usr/bin/env python3

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


MIN_PYTHON = (3, 11)


def repo_root() -> Path:
    output = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"],
        text=True,
        stderr=subprocess.DEVNULL,
    )
    return Path(output.strip())


def dotfiles_dir(root: Path) -> str:
    manifest = root / ".agent" / "project.toml"
    match = re.search(r'^dotfiles_dir\s*=\s*"([^"]+)"', manifest.read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit(f"Missing dotfiles_dir in {manifest}")
    return match.group(1)


def compatible_python() -> str:
    if sys.version_info >= MIN_PYTHON:
        return sys.executable

    for name in ("python3.13", "python3.12", "python3.11"):
        candidate = shutil.which(name)
        if candidate and subprocess.run(
            [candidate, "-c", "import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode == 0:
            return candidate

    print(
        '{"systemMessage":"[agent] hook 無法執行：需要 Python 3.11 以上版本，'
        '但目前 PATH 找不到相容的 Python。"}'
    )
    raise SystemExit(78)


def main() -> int:
    root = repo_root()
    shared = Path(dotfiles_dir(root)) / "agent" / "scripts" / "project-types" / "git-dev" / "git-dev-hook.py"
    python = compatible_python()
    os.execv(python, [python, str(shared), *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())
