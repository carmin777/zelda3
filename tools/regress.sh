#!/usr/bin/env bash
# Headless regression bench.
#
# Replays every reference save through the side-by-side ROM comparison and
# compares the frame hashes against tests/golden/ref<n>.txt.
#
# Environment:
#   REGRESS_FRAMES     number of frames per replay (default: the whole replay)
#   REGRESS_HASH_EVERY frame interval for "frame" lines (default: 1000)
#   REGRESS_JOBS       number of parallel processes (default: 2, max: 2)
#
# Usage:
#   tools/regress.sh            run and compare
#   tools/regress.sh --update   rewrite the golden files
#   tools/regress.sh --trace    run with --mod-trace, compare hashes and save
#                               tests/trace/ref<n>.txt
set -u

cd "$(dirname "$0")/.." || exit 2

# Must match kReferenceSaves[] in src/zelda_rtl.c.
replays=(
  "Chapter 1 - Zelda's Rescue.sav"
  "Chapter 2 - After Eastern Palace.sav"
  "Chapter 3 - After Desert Palace.sav"
  "Chapter 4 - After Tower of Hera.sav"
  "Chapter 5 - After Hyrule Castle Tower.sav"
  "Chapter 6 - After Dark Palace.sav"
  "Chapter 7 - After Swamp Palace.sav"
  "Chapter 8 - After Skull Woods.sav"
  "Chapter 9 - After Gargoyle's Domain.sav"
  "Chapter 10 - After Ice Palace.sav"
  "Chapter 11 - After Misery Mire.sav"
  "Chapter 12 - After Turtle Rock.sav"
  "Chapter 13 - After Ganon's Tower.sav"
)

# Extra replays (e.g. attached to upstream issues) live in tests/replays/*.sav
# (no spaces in the name) and are checked against tests/golden/<name>.txt.
labels=()
cargs=()
saves=()
for i in "${!replays[@]}"; do
  labels+=("ref$i")
  cargs+=("--replay-ref $i")
  saves+=("saves/ref/${replays[$i]}")
done
shopt -s nullglob
for f in tests/replays/*.sav; do
  case "$f" in *' '*) echo "regress: no spaces allowed in $f" >&2; exit 2 ;; esac
  labels+=("$(basename "$f" .sav)")
  cargs+=("--replay $f")
  saves+=("$f")
done
shopt -u nullglob

update=0
trace=0
for arg in "$@"; do
  case "$arg" in
    --update) update=1 ;;
    --trace) trace=1 ;;
    *)
      echo "usage: $0 [--update] [--trace]" >&2
      exit 2
      ;;
  esac
done

for f in zelda3 zelda3.sfc zelda3_assets.dat tests/regress.ini; do
  if [ ! -e "$f" ]; then
    echo "regress: missing $f" >&2
    exit 2
  fi
done

hash_every=${REGRESS_HASH_EVERY:-1000}
jobs=${REGRESS_JOBS:-2}
case "$hash_every" in
  ''|*[!0-9]*|0*) echo "regress: invalid REGRESS_HASH_EVERY='$hash_every'" >&2; exit 2 ;;
esac
case "$jobs" in
  ''|*[!0-9]*) echo "regress: invalid REGRESS_JOBS='$jobs'" >&2; exit 2 ;;
esac
[ "$jobs" -gt 2 ] && jobs=2
[ "$jobs" -lt 1 ] && jobs=1

if [ "$update" = 1 ]; then
  mkdir -p tests/golden || exit 2
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/zelda3-regress.XXXXXX") || exit 2
trap 'rm -rf "$tmpdir"' EXIT

# StateRecorder file header: uint32 version, total_frames, log_size, ...
read_total_frames() {
  od -An -tu4 -j4 -N4 "$1" | tr -d '[:space:]'
}

frames_list=()
for i in "${!labels[@]}"; do
  save="${saves[$i]}"
  if [ ! -f "$save" ]; then
    echo "regress: missing $save" >&2
    exit 2
  fi
  total=$(read_total_frames "$save")
  case "$total" in
    ''|*[!0-9]*) echo "regress: cannot read frame count from $save" >&2; exit 2 ;;
  esac
  frames=$total
  if [ -n "${REGRESS_FRAMES:-}" ]; then
    case "$REGRESS_FRAMES" in
      ''|*[!0-9]*) echo "regress: invalid REGRESS_FRAMES='$REGRESS_FRAMES'" >&2; exit 2 ;;
    esac
    frames=$REGRESS_FRAMES
    [ "$frames" -gt "$total" ] && frames=$total
  fi
  frames_list[$i]=$frames
done

run_one() {
  local idx=$1 frames=$2
  local start end rc trace_arg
  start=$(date +%s%N)
  trace_arg=""
  if [ "$trace" = 1 ]; then
    trace_arg="--mod-trace"
  fi
  ./zelda3 --config tests/regress.ini --headless --no-mods ${cargs[$idx]} \
    --frames "$frames" --hash-every "$hash_every" $trace_arg zelda3.sfc \
    > "$tmpdir/out.$idx" 2> "$tmpdir/err.$idx"
  rc=$?
  end=$(date +%s%N)
  printf '%s\n' "$rc" > "$tmpdir/status.$idx"
  printf '%s\n' "$(( (end - start) / 1000000 ))" > "$tmpdir/time.$idx"
}

running=0
for i in "${!labels[@]}"; do
  run_one "$i" "${frames_list[$i]}" &
  running=$((running + 1))
  if [ "$running" -ge "$jobs" ]; then
    wait -n
    running=$((running - 1))
  fi
done
wait

if [ "${#labels[@]}" -lt "${#replays[@]}" ]; then
  echo "regress: only ${#labels[@]} cases found, expected at least ${#replays[@]}" >&2
  exit 2
fi

fail=0
checked=0
for i in "${!labels[@]}"; do
  name=${saves[$i]##*/}
  label=${labels[$i]}
  out="$tmpdir/out.$i"
  err="$tmpdir/err.$i"
  rc=$(cat "$tmpdir/status.$i" 2>/dev/null || echo 127)
  ms=$(cat "$tmpdir/time.$i" 2>/dev/null || echo '?')

  if [ "$rc" != 0 ]; then
    echo "FAIL $label ($name): exit code $rc"
    if [ -s "$err" ]; then
      echo "--- stderr (first 40 lines) ---"
      sed -n '1,40p' "$err"
    fi
    fail=1
    continue
  fi

  if ! grep -qx 'compare: on' "$out"; then
    echo "FAIL $label ($name): side-by-side comparison is not active"
    grep '^compare:' "$out" || true
    fail=1
    continue
  fi

  golden="tests/golden/$label.txt"
  actual="$out"
  if [ "$trace" = 1 ]; then
    mkdir -p tests/trace || { fail=1; continue; }
    grep '^ev ' "$out" > "tests/trace/$label.txt"
    actual="$tmpdir/hashes.$i"
    grep -v '^ev ' "$out" > "$actual"
  fi
  if [ "$update" = 1 ]; then
    cp "$actual" "$golden" || { fail=1; continue; }
    echo "updated $label ($name) ${frames_list[$i]} frames ${ms}ms"
  else
    if [ ! -f "$golden" ]; then
      echo "FAIL $label ($name): missing $golden (run with --update)"
      fail=1
      continue
    fi
    if ! diff -u "$golden" "$actual" > "$tmpdir/diff.$i"; then
      echo "FAIL $label ($name): output differs from $golden"
      sed -n '1,40p' "$tmpdir/diff.$i"
      lines=$(wc -l < "$tmpdir/diff.$i")
      if [ "$lines" -gt 40 ]; then
        echo "  ... ($lines diff lines total)"
      fi
      fail=1
      continue
    fi
    echo "ok $label ($name) ${frames_list[$i]} frames ${ms}ms"
    checked=$((checked + 1))
  fi
done

# Feature replays: run with a feature enabled (compare is off), so they are
# checked against their event trace and final hash in tests/golden-features/.
feat_total=0
feat_checked=0
shopt -s nullglob
for f in tests/replays-features/*.sav; do
  case "$f" in *' '*) echo "regress: no spaces allowed in $f" >&2; exit 2 ;; esac
  feat_total=$((feat_total + 1))
  label=$(basename "$f" .sav)
  frames=$(read_total_frames "$f")
  golden="tests/golden-features/$label.txt"
  # Optional per-replay config next to the .sav; default enables ItemSwitchLR.
  fconfig="tests/replays-features/$label.ini"
  [ -f "$fconfig" ] || fconfig=tests/regress-switchlr.ini
  # Optional <label>.mod holds the path to one mod directory. It is copied to
  # a temporary mods root so these replays never depend on the user's mods/.
  mod_path=""
  mod_file="tests/replays-features/$label.mod"
  [ -f "$mod_file" ] && mod_path=$(cat "$mod_file")
  zargs=(--config "$fconfig" --headless)
  # Optional player-2 script for SharedWorld / Local co-op features.
  script2_file="tests/replays-features/$label.script2"
  if [ -f "$script2_file" ]; then
    zargs+=(--input-script2 "$script2_file")
  fi
  if [ -n "$mod_path" ]; then
    mods_root="$tmpdir/mods.$label"
    mkdir -p "$mods_root" || { fail=1; continue; }
    cp -a "$mod_path" "$mods_root/" || { fail=1; continue; }
    zargs+=(--mods-dir "$mods_root")
  else
    zargs+=(--no-mods)
  fi
  ./zelda3 "${zargs[@]}" --replay "$f" \
    --frames "$frames" --mod-trace zelda3.sfc 2> "$tmpdir/ferr.$label" \
    | grep -E '^(compare|ev |hook |override |final)' > "$tmpdir/fout.$label"
  rc=${PIPESTATUS[0]}
  if [ "$rc" != 0 ]; then
    echo "FAIL feature $label: exit code $rc"
    sed -n '1,20p' "$tmpdir/ferr.$label"
    fail=1
    continue
  fi
  if [ "$update" = 1 ]; then
    mkdir -p tests/golden-features && cp "$tmpdir/fout.$label" "$golden"
    echo "updated feature $label $frames frames"
  elif [ ! -f "$golden" ]; then
    echo "FAIL feature $label: missing $golden (run with --update)"
    fail=1
  elif ! diff -u "$golden" "$tmpdir/fout.$label" > "$tmpdir/fdiff.$label"; then
    echo "FAIL feature $label: output differs from $golden"
    sed -n '1,20p' "$tmpdir/fdiff.$label"
    fail=1
  else
    echo "ok feature $label $frames frames"
    feat_checked=$((feat_checked + 1))
  fi
done
shopt -u nullglob

# --load + --save-replay round trip: loading a state and recording from it must
# start a new replay at that state, not append to the loaded file's history.
# Replaying the saved file has to reach the same hash as the loaded run.
loadsave_frames=90
loadsave="$tmpdir/load-save-replay.sav"
./zelda3 --config tests/regress.ini --headless --no-mods \
  --load "saves/ref/Chapter 1 - Zelda's Rescue.sav" \
  --input-script tests/load-save-replay.input --frames "$loadsave_frames" \
  --save-replay "$loadsave" zelda3.sfc \
  > "$tmpdir/loadsave.out" 2> "$tmpdir/loadsave.err"
rc=$?
if [ "$rc" != 0 ]; then
  echo "FAIL load-save-replay: --load exit code $rc"
  sed -n '1,20p' "$tmpdir/loadsave.err"
  fail=1
else
  load_hash=$(grep '^final ' "$tmpdir/loadsave.out" | tail -n 1 | awk '{print $3}')
  total=$(read_total_frames "$loadsave")
  if [ "$total" != "$loadsave_frames" ]; then
    echo "FAIL load-save-replay: saved replay has $total frames, expected $loadsave_frames (old log was not rebased)"
    fail=1
  elif [ -z "$load_hash" ]; then
    echo "FAIL load-save-replay: no final hash from --load"
    fail=1
  else
    ./zelda3 --config tests/regress.ini --headless --no-mods \
      --replay "$loadsave" --frames "$total" zelda3.sfc \
      > "$tmpdir/loadsave-replay.out" 2> "$tmpdir/loadsave-replay.err"
    rc=$?
    replay_hash=$(grep '^final ' "$tmpdir/loadsave-replay.out" | tail -n 1 | awk '{print $3}')
    if [ "$rc" != 0 ]; then
      echo "FAIL load-save-replay: --replay exit code $rc"
      sed -n '1,20p' "$tmpdir/loadsave-replay.err"
      fail=1
    elif [ "$load_hash" != "$replay_hash" ]; then
      echo "FAIL load-save-replay: loaded run hash $load_hash != replay hash $replay_hash"
      fail=1
    else
      echo "ok load-save-replay $total frames"
    fi
  fi
fi

# Seed oracle replay. The JP 1.0 source ROM and the ALttPR seed ROM are
# user-provided files outside git, so this case is skipped with a warning when
# they are not available. The worktree fallback looks next to the main .git.
seed_script=tests/replays-seed/seed-xvJo08oxyJ.txt
if [ -f "$seed_script" ]; then
  common_root=$(cd "$(git rev-parse --git-common-dir 2>/dev/null || echo .git)/.." && pwd)
  find_user_file() {
    local rel=$1
    if [ -f "$rel" ]; then
      printf '%s\n' "$rel"
    elif [ -f "$common_root/$rel" ]; then
      printf '%s\n' "$common_root/$rel"
    else
      return 1
    fi
  }
  jp_rom=$(find_user_file japanese.sfc 2>/dev/null || true)
  seed_rom=$(find_user_file .agents/alttpr/download/seed-xvJo08oxyJ.sfc 2>/dev/null || true)
  if [ -z "$jp_rom" ] || [ -z "$seed_rom" ]; then
    echo "skip seed-oracle: japanese.sfc and/or .agents/alttpr/download/seed-xvJo08oxyJ.sfc not found"
  else
    seed_label=seed-xvJo08oxyJ
    seed_golden=tests/golden-seed/$seed_label.txt
    ./zelda3 --config tests/regress.ini --headless --no-mods \
      --oracle-rom "$seed_rom" \
      --input-script "$seed_script" --frames 11200 --hash-every 1 \
      > "$tmpdir/seed.out" 2> "$tmpdir/seed.err"
    rc=$?
    if [ "$rc" != 0 ]; then
      echo "FAIL seed-oracle ($seed_label): exit code $rc"
      sed -n '1,40p' "$tmpdir/seed.err"
      fail=1
    elif [ "$update" = 1 ]; then
      mkdir -p tests/golden-seed && cp "$tmpdir/seed.out" "$seed_golden"
      echo "updated seed-oracle ($seed_label) 11200 frames"
    elif [ ! -f "$seed_golden" ]; then
      echo "FAIL seed-oracle ($seed_label): missing $seed_golden (run with --update)"
      fail=1
    elif ! diff -u "$seed_golden" "$tmpdir/seed.out" > "$tmpdir/seed.diff"; then
      echo "FAIL seed-oracle ($seed_label): output differs from $seed_golden"
      sed -n '1,40p' "$tmpdir/seed.diff"
      fail=1
    else
      echo "ok seed-oracle ($seed_label) 11200 frames"
    fi

    # Seed importer: build a mod from the same user-provided ROM and prove
    # that the native engine uses the seed's Link's House chest and uncle item.
    # The generated mod stays in the temporary directory; no seed data enters
    # the repository.
    seed_mods="$tmpdir/seed-mods"
    seed_mod="$seed_mods/$seed_label"
    mkdir -p "$seed_mods"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" -o "$seed_mod" \
      > "$tmpdir/seed-import.log" 2>&1
    rc=$?
    if [ "$rc" != 0 ]; then
      echo "FAIL seed-import ($seed_label): mod generation failed"
      sed -n '1,40p' "$tmpdir/seed-import.log"
      fail=1
    elif ! python3 tools/alttpr_seed.py coverage --rom "$seed_rom" \
        --mod "$seed_mod" --summary > "$tmpdir/seed-coverage.log" 2>&1; then
      echo "FAIL seed-import ($seed_label): coverage check failed"
      sed -n '1,40p' "$tmpdir/seed-coverage.log"
      fail=1
    else
      grep '^seed-import:' "$tmpdir/seed-coverage.log"
      ./zelda3 --config tests/regress.ini --headless --mods-dir "$seed_mods" \
        --replay tests/replays-features/f4e-item-novo.sav --frames 3750 \
        --mod-trace zelda3.sfc \
        > "$tmpdir/seed-chest.out" 2> "$tmpdir/seed-chest.err"
      chest_rc=$?
      ./zelda3 --config tests/regress.ini --headless --mods-dir "$seed_mods" \
        --replay tests/replays-seed/seed-uncle.sav --frames 20 \
        --mod-trace zelda3.sfc \
        > "$tmpdir/seed-uncle.out" 2> "$tmpdir/seed-uncle.err"
      uncle_rc=$?
      if [ "$chest_rc" != 0 ] || [ "$uncle_rc" != 0 ]; then
        echo "FAIL seed-import ($seed_label): replay exit $chest_rc/$uncle_rc"
        sed -n '1,20p' "$tmpdir/seed-chest.err"
        sed -n '1,20p' "$tmpdir/seed-uncle.err"
        fail=1
      else
        grep -hE '^(compare|ev |hook |override |final)' \
          "$tmpdir/seed-chest.out" "$tmpdir/seed-uncle.out" \
          > "$tmpdir/seed-import-trace.txt"
        if python3 tools/alttpr_seed.py verify-replay --rom "$seed_rom" \
            --trace "$tmpdir/seed-import-trace.txt"; then
          echo "ok seed-import ($seed_label) chest+uncle"
        else
          echo "FAIL seed-import ($seed_label): replay items differ from the ROM"
          fail=1
        fi
      fi
    fi

    # Targeted feature proofs. Each one builds a small mod from the same seed
    # ROM (filtered to the locations under test) so the reference replay keeps
    # its original route; Z3rRules stays off when the route depends on vanilla
    # movement.
    seed_proofs="$tmpdir/seed-proofs"
    mkdir -p "$seed_proofs"

    # NPC fixed item: MushroomItem is 0x2A in this seed (vanilla 0x29).
    fixed_root="$seed_proofs/fixed"
    mkdir -p "$fixed_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only fixed \
      --no-z3r-rules -o "$fixed_root/$seed_label" \
      > "$tmpdir/seed-proof-fixed-import.log" 2>&1
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$fixed_root" \
      --replay-ref 1 --frames 4400 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-fixed.out" 2> "$tmpdir/seed-proof-fixed.err"
    fixed_rc=$?
    if [ "$fixed_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof npc --trace "$tmpdir/seed-proof-fixed.out"; then
      echo "FAIL seed-proof npc ($seed_label): exit $fixed_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-fixed.err"
      fail=1
    fi

    # Dungeon prize: Eastern Palace is a crystal in this seed (vanilla pendant).
    prize_root="$seed_proofs/prize"
    mkdir -p "$prize_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only prize \
      --no-z3r-rules -o "$prize_root/$seed_label" \
      > "$tmpdir/seed-proof-prize-import.log" 2>&1
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$prize_root" \
      --replay-ref 1 --frames 101600 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-prize.out" 2> "$tmpdir/seed-proof-prize.err"
    prize_rc=$?
    if [ "$prize_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof prize --trace "$tmpdir/seed-proof-prize.out"; then
      echo "FAIL seed-proof prize ($seed_label): exit $prize_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-prize.err"
      fail=1
    fi

    # Shop: Kakariko room 0x1f, middle item from the seed shop table.
    shop_root="$seed_proofs/shop"
    mkdir -p "$shop_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only shop \
      --no-z3r-rules -o "$shop_root/$seed_label" \
      > "$tmpdir/seed-proof-shop-import.log" 2>&1
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$shop_root" \
      --replay tests/replays-features/f4e3-shop.sav --frames 250 \
      --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-shop.out" 2> "$tmpdir/seed-proof-shop.err"
    shop_rc=$?
    if [ "$shop_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof shop --trace "$tmpdir/seed-proof-shop.out"; then
      echo "FAIL seed-proof shop ($seed_label): exit $shop_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-shop.err"
      fail=1
    fi

    # Dungeon key: room 0xAA (Eastern Palace) holds a key for Misery Mire.
    key_root="$seed_proofs/key"
    mkdir -p "$key_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --room 170 --no-z3r-rules -o "$key_root/$seed_label" \
      > "$tmpdir/seed-proof-key-import.log" 2>&1
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$key_root" \
      --replay tests/replays-seed/seed-key.sav --frames 40 --dump-state zelda3.sfc \
      > "$tmpdir/seed-proof-key.out" 2> "$tmpdir/seed-proof-key.err"
    key_rc=$?
    if [ "$key_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof key --stderr "$tmpdir/seed-proof-key.err" \
        --state "$tmpdir/seed-proof-key.out"; then
      echo "FAIL seed-proof key ($seed_label): exit $key_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-key.err"
      fail=1
    fi

    # New file: the SRAM template must be applied (progress flags, capacities).
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$seed_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
      --dump-state zelda3.sfc \
      > "$tmpdir/seed-proof-start.out" 2> "$tmpdir/seed-proof-start.err"
    start_rc=$?
    if [ "$start_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof start --state "$tmpdir/seed-proof-start.out"; then
      echo "FAIL seed-proof start ($seed_label): exit $start_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-start.err"
      fail=1
    fi
  fi
fi

if [ "$fail" = 0 ] && [ "$update" != 1 ] && [ "$feat_checked" != "$feat_total" ]; then
  echo "regress: checked $feat_checked of $feat_total feature cases" >&2
  fail=1
fi
if [ "$fail" = 0 ] && [ "$update" != 1 ] && [ "$checked" != "${#labels[@]}" ]; then
  echo "regress: checked $checked of ${#labels[@]} cases" >&2
  fail=1
fi
if [ "$fail" != 0 ]; then
  echo "regress: FAILED"
  exit 1
fi
echo "regress: all ${#labels[@]} replays and $feat_total feature replays passed"
