#!/usr/bin/env bash
# Lança run.sh desacoplado da sessão (sobrevive a reinício do Claude).
#   tools/agents/launch.sh <tarefa> [branch-base]
set -euo pipefail
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
LOGS="$ROOT/.agents/logs"; mkdir -p "$LOGS"
setsid nohup "$ROOT/tools/agents/run.sh" "$@" > "$LOGS/$1.run.out" 2>&1 < /dev/null &
echo $! > "$LOGS/$1.pid"
echo "[launch] $1 pid $(cat "$LOGS/$1.pid"), saída em .agents/logs/$1.run.out"
