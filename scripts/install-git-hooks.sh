#!/bin/sh
set -eu

repo_root="$(git rev-parse --show-toplevel)"
hook_dir="$repo_root/.git/hooks"

install -m 755 "$repo_root/scripts/hooks/pre-commit" "$hook_dir/pre-commit"
echo "Installed pre-commit hook: $hook_dir/pre-commit"
