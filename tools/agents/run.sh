#!/usr/bin/env bash
# Despacha um agente opencode (DeepSeek v4.1 flash) para uma tarefa.
#   tools/agents/run.sh <tarefa> [branch-base]
# Lê tools/agents/briefs/<tarefa>.md, cria o worktree .agents/<tarefa> na
# branch agent/<tarefa>, roda o agente lá e salva log + relatório em
# .agents/logs/. Se o worktree já existir, continua a mesma sessão com o
# conteúdo de tools/agents/briefs/<tarefa>.followup.md.
set -euo pipefail

TASK="$1"
BASE="${2:-engine/modloader}"
ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
MODEL="${AGENT_MODEL:-opencode-go/deepseek-v4.1-flash#max}"
OPENCODE="${OPENCODE:-$HOME/.opencode/bin/opencode}"
WT="$ROOT/.agents/$TASK"
LOGS="$ROOT/.agents/logs"
BRIEF="$ROOT/tools/agents/briefs/$TASK.md"
mkdir -p "$LOGS"

if [ ! -d "$WT" ]; then
  [ -f "$BRIEF" ] || { echo "sem briefing: $BRIEF" >&2; exit 2; }
  git -C "$ROOT" worktree add -q "$WT" -b "agent/$TASK" "$BASE"
  # ROM e assets do usuário: só links, nunca entram no git
  for f in zelda3.sfc zelda3_assets.dat portuguese.sfc; do
    [ -e "$ROOT/$f" ] && ln -sf "$ROOT/$f" "$WT/$f"
  done
  MSG="$(cat "$BRIEF")"
  SESSION_ARGS=()
else
  FOLLOW="$ROOT/tools/agents/briefs/$TASK.followup.md"
  [ -f "$FOLLOW" ] || { echo "worktree existe e não há $FOLLOW" >&2; exit 2; }
  MSG="$(cat "$FOLLOW")"
  SID="$(cat "$LOGS/$TASK.session")"
  SESSION_ARGS=(--session "$SID")
fi

N=$(ls "$LOGS" | grep -c "^$TASK\.[0-9]*\.jsonl$" || true)
LOG="$LOGS/$TASK.$N.jsonl"
echo "[run.sh] $TASK rodada $N em $WT ($MODEL)"

cd "$WT"
set +e
"$OPENCODE" run -m "$MODEL" --auto --format json --title "zelda3:$TASK" \
  "${SESSION_ARGS[@]}" \
  "$(printf 'Leia AGENTS.md antes de tudo.\n\n%s' "$MSG")" > "$LOG" 2> "$LOGS/$TASK.$N.err"
RC=$?
set -e

python3 - "$LOG" "$LOGS/$TASK" <<'PY'
import json, sys
log, base = sys.argv[1], sys.argv[2]
sid, texts, tools = None, [], 0
for line in open(log, encoding="utf-8", errors="replace"):
    try: d = json.loads(line)
    except Exception: continue
    sid = d.get("sessionID") or sid
    if d.get("type") == "tool_use": tools += 1
    if d.get("type") == "text":
        t = (d.get("part") or {}).get("text", "")
        if t.strip(): texts.append(t)
if sid: open(base + ".session", "w").write(sid)
rep = texts[-1] if texts else "(sem texto final)"
open(base + ".report.md", "w").write(rep)
print(f"[run.sh] sessão {sid}, {tools} chamadas de ferramenta")
print(rep)
PY
echo "[run.sh] opencode saiu com $RC"
git -C "$WT" log --oneline "$BASE"..HEAD
