#!/usr/bin/env bash
# Despacha um agente opencode (DeepSeek v4.1 flash) ou Codex
# (AGENT_CLI=codex) para uma tarefa.
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
# AGENT_CLI=codex runs the task with the Codex CLI (model from
# ~/.codex/config.toml unless CODEX_MODEL is set) instead of opencode.
AGENT_CLI="${AGENT_CLI:-opencode}"
CODEX="${CODEX:-$HOME/.local/bin/codex}"
WT="$ROOT/.agents/$TASK"
LOGS="$ROOT/.agents/logs"
BRIEF="$ROOT/tools/agents/briefs/$TASK.md"
mkdir -p "$LOGS"
# Each CLI keeps its own session id, so switching never resumes the wrong one.
if [ "$AGENT_CLI" = codex ]; then SESSION_FILE="$LOGS/$TASK.codex-session"
else SESSION_FILE="$LOGS/$TASK.session"; fi

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
  # Without a saved session (for example after a session that ran out of
  # context was set aside), the follow-up starts a fresh session.
  if [ -f "$SESSION_FILE" ]; then
    SID="$(cat "$SESSION_FILE")"
    SESSION_ARGS=(--session "$SID")
  else
    SESSION_ARGS=()
  fi
fi

N=$(ls "$LOGS" | grep -c "^$TASK\.[0-9]*\.jsonl$" || true)
LOG="$LOGS/$TASK.$N.jsonl"
PROMPT="$(printf 'Leia AGENTS.md antes de tudo.\n\n%s' "$MSG")"

cd "$WT"
set +e
if [ "$AGENT_CLI" = codex ]; then
  # Codex: its workspace-write sandbox keeps every .git read-only, and the
  # agents must commit each cycle, so it runs without the sandbox, like the
  # opencode agents (--auto) always have.
  echo "[run.sh] $TASK rodada $N em $WT (codex ${CODEX_MODEL:-default})"
  CODEX_ARGS=(--json -o "$LOGS/$TASK.report.md"
    --dangerously-bypass-approvals-and-sandbox)
  [ -n "${CODEX_MODEL:-}" ] && CODEX_ARGS+=(-m "$CODEX_MODEL")
  [ -n "${CODEX_EFFORT:-}" ] && CODEX_ARGS+=(-c "model_reasoning_effort=\"$CODEX_EFFORT\"")
  if [ "${#SESSION_ARGS[@]}" -gt 0 ]; then
    "$CODEX" exec resume "${CODEX_ARGS[@]}" "$SID" "$PROMPT" < /dev/null > "$LOG" 2> "$LOGS/$TASK.$N.err"
  else
    "$CODEX" exec "${CODEX_ARGS[@]}" "$PROMPT" < /dev/null > "$LOG" 2> "$LOGS/$TASK.$N.err"
  fi
else
  echo "[run.sh] $TASK rodada $N em $WT ($MODEL)"
  "$OPENCODE" run -m "$MODEL" --auto --format json --title "zelda3:$TASK" \
    "${SESSION_ARGS[@]}" "$PROMPT" < /dev/null > "$LOG" 2> "$LOGS/$TASK.$N.err"
fi
RC=$?
set -e

python3 - "$LOG" "$LOGS/$TASK" "$AGENT_CLI" "$SESSION_FILE" <<'PY'
import json, sys
log, base, cli, sfile = sys.argv[1:5]
sid, texts, tools = None, [], 0
for line in open(log, encoding="utf-8", errors="replace"):
    try: d = json.loads(line)
    except Exception: continue
    if cli == "codex":
        if d.get("type") == "thread.started": sid = d.get("thread_id") or sid
        item = d.get("item") or {}
        if d.get("type") == "item.completed":
            if item.get("type") == "agent_message" and (item.get("text") or "").strip():
                texts.append(item["text"])
            elif item.get("type") not in ("reasoning", "agent_message"):
                tools += 1
    else:
        sid = d.get("sessionID") or sid
        if d.get("type") == "tool_use": tools += 1
        if d.get("type") == "text":
            t = (d.get("part") or {}).get("text", "")
            if t.strip(): texts.append(t)
if sid: open(sfile, "w").write(sid)
rep = texts[-1] if texts else "(sem texto final)"
open(base + ".report.md", "w").write(rep)
print(f"[run.sh] sessão {sid}, {tools} chamadas de ferramenta")
print(rep)
PY
echo "[run.sh] $AGENT_CLI saiu com $RC"
git -C "$WT" log --oneline "$BASE"..HEAD
