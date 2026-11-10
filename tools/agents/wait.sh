#!/usr/bin/env bash
# Espera a tarefa lançada por launch.sh terminar e mostra o resultado.
#   tools/agents/wait.sh <tarefa>
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
PID="$(cat "$ROOT/.agents/logs/$1.pid")"
while kill -0 "$PID" 2>/dev/null; do sleep 15; done
tail -n 60 "$ROOT/.agents/logs/$1.run.out"
