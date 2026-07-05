#!/bin/bash

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
DOTFILES_DIR="$(awk -F'"' '/^dotfiles_dir/{print $2}' "$REPO_ROOT/.agent/project.toml")"

exec bash "$DOTFILES_DIR/agent/scripts/project-types/git-dev/session-stop.sh"
