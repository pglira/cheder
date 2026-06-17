#!/usr/bin/env bash
# Stop hook: rebuild and install cheder so the installed binary always
# reflects the current source. On a build or install failure it asks Claude to
# keep working, so a broken tree is fixed before the turn ends; it backs off
# after a stop-hook-triggered retry to avoid an endless loop.
set -uo pipefail

input=$(cat)

root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
cd "$root" || exit 0

# Nothing to build until the tree has been configured with CMake.
[ -d build ] || exit 0

out=$(cmake --build build 2>&1 && cmake --install build --prefix "$HOME/.local" 2>&1)
status=$?
[ "$status" -eq 0 ] && exit 0

# Build or install failed. Re-engage Claude once so the break gets fixed, but
# stay quiet when already inside a stop-hook continuation to avoid looping.
active=$(printf '%s' "$input" | jq -r '.stop_hook_active // false')
if [ "$active" = "true" ]; then
    printf '%s\n' "$out" >&2
    exit 0
fi
jq -nc --arg r "cheder build/install failed:
$out" '{decision: "block", reason: $r}'
