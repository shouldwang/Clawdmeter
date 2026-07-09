#!/usr/bin/env python3

import os
import re
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


def compatible_python(dotfiles: Path) -> str:
    if sys.version_info >= MIN_PYTHON:
        return sys.executable

    # Delegate the compatible-Python search to run-python.sh so the version
    # list (3.13/3.12/3.11) lives in exactly one place.
    run_python = dotfiles / "harness-core" / "scripts" / "shared" / "run-python.sh"
    result = subprocess.run(
        ["/bin/bash", str(run_python), "--print-python"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        candidate = result.stdout.strip()
        if candidate:
            return candidate

    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit(result.returncode or 78)


def main() -> int:
    root = repo_root()
    dotfiles = Path(dotfiles_dir(root))
    shared = dotfiles / "harness-core" / "scripts" / "project-types" / "git-dev" / "git-dev-hook.py"
    python = compatible_python(dotfiles)
    os.execv(python, [python, str(shared), *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())
