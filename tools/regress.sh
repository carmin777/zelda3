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

# Copy a test fixture into a generated seed mod and make it the mod script.
set_mod_script() {
  local dir=$1 name=$2
  cp "tests/replays-seed/$name" "$dir/$name" || return 1
  python3 -c 'import json, sys
p, script = sys.argv[1], sys.argv[2]
m = json.load(open(p))
m["script"] = script
m["permissions"] = ["state_write"]
json.dump(m, open(p, "w"), indent=2)' "$dir/mod.json" "$name"
}

seed_set() {
  python3 -c 'import json, sys
p, key, value = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
d = json.load(open(p))
d["z3r_settings"][key] = value
json.dump(d, open(p, "w"), indent=2)' "$1" "$2" "$3"
}

seed_set_chest_item() {
  python3 -c 'import json, sys
p, item, room = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
d = json.load(open(p))
for loc in d["locations"]:
  if loc.get("type") == "chest" and loc.get("room") == room:
    loc["item"] = item
json.dump(d, open(p, "w"), indent=2)' "$1" "$2" "$3"
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
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --no-dialogue \
      -o "$seed_mod" > "$tmpdir/seed-import.log" 2>&1
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
      --no-z3r-rules --no-dialogue -o "$fixed_root/$seed_label" \
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
      --no-z3r-rules --no-dialogue -o "$prize_root/$seed_label" \
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
      --no-z3r-rules --no-dialogue -o "$shop_root/$seed_label" \
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

    # The same shop reached from the overworld must carry door 0x46. ref1
    # enters room 0x11F from the overworld at frame 48400.
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$shop_root" \
      --replay-ref 1 --frames 48600 --dump-state --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-shopdoor.out" 2> "$tmpdir/seed-proof-shopdoor.err"
    shopdoor_rc=$?
    if [ "$shopdoor_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof shopdoor --state "$tmpdir/seed-proof-shopdoor.out" \
        --trace "$tmpdir/seed-proof-shopdoor.out"; then
      echo "FAIL seed-proof shopdoor ($seed_label): exit $shopdoor_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-shopdoor.err"
      fail=1
    fi

    # Dungeon key: room 0xAA (Eastern Palace) holds a key for Misery Mire.
    key_root="$seed_proofs/key"
    mkdir -p "$key_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --room 170 --no-z3r-rules --no-dialogue -o "$key_root/$seed_label" \
      > "$tmpdir/seed-proof-key-import.log" 2>&1
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$key_root" \
      --replay tests/replays-seed/seed-key.sav --frames 40 --dump-state \
      --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-key.out" 2> "$tmpdir/seed-proof-key.err"
    key_rc=$?
    if [ "$key_rc" != 0 ] || ! python3 tools/alttpr_seed.py verify-proof \
        --rom "$seed_rom" --proof key --trace "$tmpdir/seed-proof-key.out" \
        --state "$tmpdir/seed-proof-key.out"; then
      echo "FAIL seed-proof key ($seed_label): exit $key_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-key.err"
      fail=1
    fi

    # Seed text: ref1 opens C message 369 (the Ether/Andy hint) at frame 3327.
    # The mod is built from the seed ROM's raw JP text and installed as a
    # kDialogue override; the DialogueText event hashes exactly those bytes.
    text_root="$seed_proofs/text"
    mkdir -p "$text_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only prize \
      --no-z3r-rules --build -o "$text_root/$seed_label" \
      > "$tmpdir/seed-proof-text-import.log" 2>&1
    text_import_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$text_root" \
      --replay-ref 1 --frames 3500 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-text.out" 2> "$tmpdir/seed-proof-text.err"
    text_rc=$?
    if [ "$text_import_rc" != 0 ] || [ "$text_rc" != 0 ] || \
       ! python3 tools/alttpr_seed.py verify-proof --rom "$seed_rom" \
         --proof text --message-id 369 --trace "$tmpdir/seed-proof-text.out"; then
      echo "FAIL seed-proof text ($seed_label): exit $text_import_rc/$text_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-text-import.log"
      sed -n '1,20p' "$tmpdir/seed-proof-text.err"
      fail=1
    else
      echo "ok seed-proof text ($seed_label) message 369"
    fi

    # Ganon's Tower: the seed asks for 4 crystals, while the vanilla check
    # uses 7. The Lua fixture forges the state (a Waterfall sprite on Link and
    # four crystal bits); no seed data enters the repository.
    gt_root="$seed_proofs/gt"
    gt_mod="$gt_root/$seed_label"
    gt_root7="$seed_proofs/gt-vanilla"
    gt_mod7="$gt_root7/$seed_label"
    mkdir -p "$gt_root" "$gt_root7"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only settings \
      --no-dialogue -o "$gt_mod" > "$tmpdir/seed-proof-gt-import.log" 2>&1
    cp tests/replays-seed/seed-gt.lua "$gt_mod/gt.lua"
    python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "gt.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
      "$gt_mod/mod.json"
    cp -a "$gt_mod" "$gt_mod7"
    python3 -c 'import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["z3r_settings"]["ganons_tower_open_target"] = 7; json.dump(s, open(p, "w"), indent=2)' \
      "$gt_mod7/seed.json"
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$gt_root" \
      --replay tests/replays-features/f4e-mirror.sav --frames 12 \
      --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-gt.out" 2> "$tmpdir/seed-proof-gt.err"
    gt_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$gt_root7" \
      --replay tests/replays-features/f4e-mirror.sav --frames 12 \
      --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-gt7.out" 2> "$tmpdir/seed-proof-gt7.err"
    gt7_rc=$?
    if [ "$gt_rc" != 0 ] || [ "$gt7_rc" != 0 ] || \
       ! grep -q '^ev .* GtOpen 4 4 1$' "$tmpdir/seed-proof-gt.out" || \
       grep -q ' GtOpen ' "$tmpdir/seed-proof-gt7.out"; then
      echo "FAIL seed-proof gt ($seed_label): exit $gt_rc/$gt7_rc"
      grep 'GtOpen' "$tmpdir/seed-proof-gt.out" "$tmpdir/seed-proof-gt7.out" || true
      sed -n '1,20p' "$tmpdir/seed-proof-gt.err"
      sed -n '1,20p' "$tmpdir/seed-proof-gt7.err"
      fail=1
    else
      echo "ok seed-proof gt ($seed_label) 4 crystals open, 7 crystals blocked"
    fi

    # Round 5: Ganon damage gate. Mode 4 of this seed opens at 7 crystals.
    ganon_root="$seed_proofs/ganon"
    ganon_block="$ganon_root/blocked/$seed_label"
    ganon_open="$ganon_root/open/$seed_label"
    mkdir -p "$ganon_root/blocked" "$ganon_root/open"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only settings \
      --no-dialogue -o "$ganon_block" > "$tmpdir/seed-proof-ganon-import.log" 2>&1
    ganon_import_rc=$?
    if [ "$ganon_import_rc" = 0 ]; then
      cp -a "$ganon_block" "$ganon_open"
      set_mod_script "$ganon_block" seed-ganon.lua
      set_mod_script "$ganon_open" seed-ganon.lua
      sed -i 's/local crystals = 0x0F/local crystals = 0x7F/' \
        "$ganon_open/seed-ganon.lua"
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$ganon_root/blocked" \
      --replay tests/replays-features/f4e-mirror.sav --frames 12 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-ganon4.out" 2> "$tmpdir/seed-proof-ganon4.err"
    ganon4_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$ganon_root/open" \
      --replay tests/replays-features/f4e-mirror.sav --frames 12 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-ganon7.out" 2> "$tmpdir/seed-proof-ganon7.err"
    ganon7_rc=$?
    if [ "$ganon_import_rc" != 0 ] || [ "$ganon4_rc" != 0 ] || [ "$ganon7_rc" != 0 ] || \
       ! grep -q 'GanonCheck 1 4 0' "$tmpdir/seed-proof-ganon4.out" || \
       ! grep -q 'GanonCheck 0 7 0' "$tmpdir/seed-proof-ganon7.out"; then
      echo "FAIL seed-proof ganon ($seed_label): exit $ganon_import_rc/$ganon4_rc/$ganon7_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-ganon-import.log"
      sed -n '1,20p' "$tmpdir/seed-proof-ganon4.err"
      sed -n '1,20p' "$tmpdir/seed-proof-ganon7.err"
      fail=1
    else
      echo "ok seed-proof ganon ($seed_label) 4 crystals blocked, 7 released"
    fi

    # Round 5: FakeBoots lets the dash action use the boots ability bit.
    boots_root="$seed_proofs/boots"
    boots_off="$boots_root/off/$seed_label"
    boots_on="$boots_root/on/$seed_label"
    mkdir -p "$boots_root/off" "$boots_root/on"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only settings \
      --no-dialogue -o "$boots_off" > "$tmpdir/seed-proof-boots-import.log" 2>&1
    boots_import_rc=$?
    if [ "$boots_import_rc" = 0 ]; then
      cp -a "$boots_off" "$boots_on"
      set_mod_script "$boots_off" seed-boots.lua
      set_mod_script "$boots_on" seed-boots.lua
      seed_set "$boots_on/seed.json" fake_boots 1
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$boots_root/off" \
      --load tests/replays-features/f4e-linkdamage.sav --frames 14 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-boots0.out" 2> "$tmpdir/seed-proof-boots0.err"
    boots0_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$boots_root/on" \
      --load tests/replays-features/f4e-linkdamage.sav --frames 14 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-boots1.out" 2> "$tmpdir/seed-proof-boots1.err"
    boots1_rc=$?
    if [ "$boots_import_rc" != 0 ] || [ "$boots0_rc" != 0 ] || [ "$boots1_rc" != 0 ] || \
       ! grep -q 'fakeboots run=0' "$tmpdir/seed-proof-boots0.err" || \
       ! grep -q 'fakeboots run=1' "$tmpdir/seed-proof-boots1.err"; then
      echo "FAIL seed-proof fakeboots ($seed_label): exit $boots_import_rc/$boots0_rc/$boots1_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-boots0.err"
      sed -n '1,20p' "$tmpdir/seed-proof-boots1.err"
      fail=1
    else
      echo "ok seed-proof fakeboots ($seed_label) off idle, on dash"
    fi

    # Round 5: QuickSwap cycles the selected item with L/R without SwitchLR.
    quick_root="$seed_proofs/quick"
    quick_off="$quick_root/off/$seed_label"
    quick_on="$quick_root/on/$seed_label"
    mkdir -p "$quick_root/off" "$quick_root/on"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only settings \
      --no-dialogue -o "$quick_off" > "$tmpdir/seed-proof-quick-import.log" 2>&1
    quick_import_rc=$?
    if [ "$quick_import_rc" = 0 ]; then
      cp -a "$quick_off" "$quick_on"
      seed_set "$quick_on/seed.json" quick_swap 1
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$quick_root/off" \
      --load tests/replays-features/f4e-linkdamage.sav \
      --input-script tests/replays-seed/seed-quick.txt --frames 120 \
      --dump-state zelda3.sfc > "$tmpdir/seed-proof-quick0.out" 2> "$tmpdir/seed-proof-quick0.err"
    quick0_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$quick_root/on" \
      --load tests/replays-features/f4e-linkdamage.sav \
      --input-script tests/replays-seed/seed-quick.txt --frames 120 \
      --dump-state zelda3.sfc > "$tmpdir/seed-proof-quick1.out" 2> "$tmpdir/seed-proof-quick1.err"
    quick1_rc=$?
    quick_off_item=$(grep '^state hud_cur_item ' "$tmpdir/seed-proof-quick0.out" | awk '{print $3}')
    quick_on_item=$(grep '^state hud_cur_item ' "$tmpdir/seed-proof-quick1.out" | awk '{print $3}')
    if [ "$quick_import_rc" != 0 ] || [ "$quick0_rc" != 0 ] || [ "$quick1_rc" != 0 ] || \
       [ "$quick_off_item" != 11 ] || [ "$quick_on_item" = 11 ]; then
      echo "FAIL seed-proof quickswap ($seed_label): exit $quick_import_rc/$quick0_rc/$quick1_rc item $quick_off_item/$quick_on_item"
      sed -n '1,20p' "$tmpdir/seed-proof-quick0.err"
      sed -n '1,20p' "$tmpdir/seed-proof-quick1.err"
      fail=1
    else
      echo "ok seed-proof quickswap ($seed_label) item $quick_off_item -> $quick_on_item"
    fi

    # Round 5: GenericKeys keeps one shared small-key counter.
    gk_root="$seed_proofs/generic-keys"
    gk_off="$gk_root/off/$seed_label"
    gk_on="$gk_root/on/$seed_label"
    mkdir -p "$gk_root/off" "$gk_root/on"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --room 170 --no-dialogue -o "$gk_off" > "$tmpdir/seed-proof-gk-import.log" 2>&1
    gk_import_rc=$?
    if [ "$gk_import_rc" = 0 ]; then
      cp -a "$gk_off" "$gk_on"
      seed_set_chest_item "$gk_off/seed.json" 0xA7 170
      seed_set_chest_item "$gk_on/seed.json" 0xA7 170
      set_mod_script "$gk_off" seed-generic-keys.lua
      set_mod_script "$gk_on" seed-generic-keys.lua
      seed_set "$gk_on/seed.json" generic_keys 1
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$gk_root/off" \
      --replay tests/replays-seed/seed-key.sav --frames 30 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-gk0.out" 2> "$tmpdir/seed-proof-gk0.err"
    gk0_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$gk_root/on" \
      --replay tests/replays-seed/seed-key.sav --frames 30 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-gk1.out" 2> "$tmpdir/seed-proof-gk1.err"
    gk1_rc=$?
    if [ "$gk_import_rc" != 0 ] || [ "$gk0_rc" != 0 ] || [ "$gk1_rc" != 0 ] || \
       ! grep -q 'generickeys keys=0 slot7=1' "$tmpdir/seed-proof-gk0.err" || \
       ! grep -q 'generickeys keys=1 slot7=0' "$tmpdir/seed-proof-gk1.err"; then
      echo "FAIL seed-proof generic-keys ($seed_label): exit $gk_import_rc/$gk0_rc/$gk1_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-gk0.err"
      sed -n '1,20p' "$tmpdir/seed-proof-gk1.err"
      fail=1
    else
      echo "ok seed-proof generic-keys ($seed_label) shared counter"
    fi

    # Round 5: NoBGM consumes music requests but keeps the mirror track.
    nobgm_root="$seed_proofs/nobgm"
    nobgm_off="$nobgm_root/off/$seed_label"
    nobgm_on="$nobgm_root/on/$seed_label"
    mkdir -p "$nobgm_root/off" "$nobgm_root/on"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only settings \
      --no-dialogue -o "$nobgm_off" > "$tmpdir/seed-proof-nobgm-import.log" 2>&1
    nobgm_import_rc=$?
    if [ "$nobgm_import_rc" = 0 ]; then
      cp -a "$nobgm_off" "$nobgm_on"
      set_mod_script "$nobgm_off" seed-nobgm.lua
      set_mod_script "$nobgm_on" seed-nobgm.lua
      seed_set "$nobgm_on/seed.json" no_bgm 1
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$nobgm_root/off" \
      --load tests/replays-features/f4e-linkdamage.sav --frames 12 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-nobgm0.out" 2> "$tmpdir/seed-proof-nobgm0.err"
    nobgm0_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$nobgm_root/on" \
      --load tests/replays-features/f4e-linkdamage.sav --frames 12 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-nobgm1.out" 2> "$tmpdir/seed-proof-nobgm1.err"
    nobgm1_rc=$?
    if [ "$nobgm_import_rc" != 0 ] || [ "$nobgm0_rc" != 0 ] || [ "$nobgm1_rc" != 0 ] || \
       ! grep -q 'nobgm f10 control=0 unk1=19' "$tmpdir/seed-proof-nobgm0.err" || \
       ! grep -q 'nobgm f10 control=0 unk1=20' "$tmpdir/seed-proof-nobgm1.err"; then
      echo "FAIL seed-proof nobgm ($seed_label): exit $nobgm_import_rc/$nobgm0_rc/$nobgm1_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-nobgm0.err"
      sed -n '1,20p' "$tmpdir/seed-proof-nobgm1.err"
      fail=1
    else
      echo "ok seed-proof nobgm ($seed_label) request consumed"
    fi

    # Round 5: FastFanfare skips the pendant wait while APUI00 stays busy.
    fan_root="$seed_proofs/fastfanfare"
    fan_off="$fan_root/off/$seed_label"
    fan_on="$fan_root/on/$seed_label"
    mkdir -p "$fan_root/off" "$fan_root/on"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only fixed \
      --no-dialogue -o "$fan_off" > "$tmpdir/seed-proof-fan-import.log" 2>&1
    fan_import_rc=$?
    if [ "$fan_import_rc" = 0 ]; then
      cp -a "$fan_off" "$fan_on"
      python3 -c 'import json, sys
for p in sys.argv[1:]:
  d = json.load(open(p))
  for loc in d["locations"]:
    if loc.get("id") == "fixed:SpriteItemValues:MushroomItem":
      loc["item"] = 0x39
  json.dump(d, open(p, "w"), indent=2)' "$fan_off/seed.json" "$fan_on/seed.json"
      set_mod_script "$fan_off" seed-fastfanfare.lua
      set_mod_script "$fan_on" seed-fastfanfare.lua
      seed_set "$fan_on/seed.json" fast_fanfare 1
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$fan_root/off" \
      --replay-ref 1 --frames 4700 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-fan0.out" 2> "$tmpdir/seed-proof-fan0.err"
    fan0_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$fan_root/on" \
      --replay-ref 1 --frames 4700 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-fan1.out" 2> "$tmpdir/seed-proof-fan1.err"
    fan1_rc=$?
    if [ "$fan_import_rc" != 0 ] || [ "$fan0_rc" != 0 ] || [ "$fan1_rc" != 0 ] || \
       grep -q 'fastfanfare f4566 item=0' "$tmpdir/seed-proof-fan0.err" || \
       ! grep -q 'fastfanfare f4566 item=0' "$tmpdir/seed-proof-fan1.err"; then
      echo "FAIL seed-proof fastfanfare ($seed_label): exit $fan_import_rc/$fan0_rc/$fan1_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-fan0.err"
      sed -n '1,20p' "$tmpdir/seed-proof-fan1.err"
      fail=1
    else
      echo "ok seed-proof fastfanfare ($seed_label) pendant wait skipped"
    fi

    # Round 5: ItemCounterHUD and MapHUDMode draw into the native HUD buffer.
    hud_root="$seed_proofs/hud"
    hud_mod="$hud_root/$seed_label"
    mkdir -p "$hud_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --no-dialogue -o "$hud_mod" > "$tmpdir/seed-proof-hud-import.log" 2>&1
    hud_import_rc=$?
    if [ "$hud_import_rc" = 0 ]; then
      set_mod_script "$hud_mod" seed-hud.lua
      seed_set "$hud_mod/seed.json" item_counter_hud 1
      seed_set "$hud_mod/seed.json" map_hud_mode 2
      seed_set "$hud_mod/seed.json" total_item_count 216
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$hud_root" \
      --load tests/replays-features/f4e-linkdamage.sav \
      --input-script tests/replays-seed/seed-hud.txt --frames 60 \
      --mod-trace zelda3.sfc > "$tmpdir/seed-proof-hud.out" 2> "$tmpdir/seed-proof-hud.err"
    hud_rc=$?
    if [ "$hud_import_rc" != 0 ] || [ "$hud_rc" != 0 ] || \
       ! grep -q 'hud counter 90 97 97 30 92 91 96 map 92 30 94' "$tmpdir/seed-proof-hud.err"; then
      echo "FAIL seed-proof hud ($seed_label): exit $hud_import_rc/$hud_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-hud-import.log"
      sed -n '1,20p' "$tmpdir/seed-proof-hud.err"
      fail=1
    else
      echo "ok seed-proof hud ($seed_label) item counter 077/216, map keys 2/4"
    fi

    # Round 5: Rupoor and the progressive sword use the seed tables.
    rupoor_root="$seed_proofs/rupoor"
    rupoor_mod="$rupoor_root/$seed_label"
    mkdir -p "$rupoor_root"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --room 170 --no-dialogue -o "$rupoor_mod" > "$tmpdir/seed-proof-rupoor-import.log" 2>&1
    rupoor_import_rc=$?
    if [ "$rupoor_import_rc" = 0 ]; then
      seed_set_chest_item "$rupoor_mod/seed.json" 0x59 170
      seed_set "$rupoor_mod/seed.json" rupoor_deduction 10
      set_mod_script "$rupoor_mod" seed-rupoor.lua
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$rupoor_root" \
      --replay tests/replays-seed/seed-key.sav --frames 30 --mod-trace zelda3.sfc \
      > "$tmpdir/seed-proof-rupoor.out" 2> "$tmpdir/seed-proof-rupoor.err"
    rupoor_rc=$?
    if [ "$rupoor_import_rc" != 0 ] || [ "$rupoor_rc" != 0 ] || \
       ! grep -q 'rupoor rupees=90' "$tmpdir/seed-proof-rupoor.err"; then
      echo "FAIL seed-proof rupoor ($seed_label): exit $rupoor_import_rc/$rupoor_rc"
      sed -n '1,20p' "$tmpdir/seed-proof-rupoor.err"
      fail=1
    else
      echo "ok seed-proof rupoor ($seed_label) 100 -> 90"
    fi

    prog_root="$seed_proofs/progressive"
    prog1="$prog_root/one/$seed_label"
    prog2="$prog_root/two/$seed_label"
    mkdir -p "$prog_root/one" "$prog_root/two"
    python3 tools/alttpr_seed.py import --rom "$seed_rom" --only chest \
      --room 170 --no-dialogue -o "$prog1" > "$tmpdir/seed-proof-prog-import.log" 2>&1
    prog_import_rc=$?
    if [ "$prog_import_rc" = 0 ]; then
      cp -a "$prog1" "$prog2"
      seed_set_chest_item "$prog1/seed.json" 0x5E 170
      seed_set_chest_item "$prog2/seed.json" 0x5E 170
      set_mod_script "$prog1" seed-progressive.lua
      set_mod_script "$prog2" seed-progressive.lua
      sed -i 's/local sword = 1/local sword = 2/' "$prog2/seed-progressive.lua"
    fi
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$prog_root/one" \
      --replay tests/replays-seed/seed-key.sav --frames 30 --dump-state zelda3.sfc \
      > "$tmpdir/seed-proof-prog1.out" 2> "$tmpdir/seed-proof-prog1.err"
    prog1_rc=$?
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$prog_root/two" \
      --replay tests/replays-seed/seed-key.sav --frames 30 --dump-state zelda3.sfc \
      > "$tmpdir/seed-proof-prog2.out" 2> "$tmpdir/seed-proof-prog2.err"
    prog2_rc=$?
    prog1_sword=$(grep '^state link_sword_type ' "$tmpdir/seed-proof-prog1.out" | awk '{print $3}')
    prog2_sword=$(grep '^state link_sword_type ' "$tmpdir/seed-proof-prog2.out" | awk '{print $3}')
    prog1_count=$(grep '^state total_item_counter ' "$tmpdir/seed-proof-prog1.out" | awk '{print $3}')
    if [ "$prog_import_rc" != 0 ] || [ "$prog1_rc" != 0 ] || [ "$prog2_rc" != 0 ] || \
       [ "$prog1_sword" != 2 ] || [ "$prog2_sword" != 3 ] || [ "$prog1_count" != 1 ]; then
      echo "FAIL seed-proof progressive ($seed_label): exit $prog_import_rc/$prog1_rc/$prog2_rc sword $prog1_sword/$prog2_sword count $prog1_count"
      sed -n '1,20p' "$tmpdir/seed-proof-prog1.err"
      sed -n '1,20p' "$tmpdir/seed-proof-prog2.err"
      fail=1
    else
      echo "ok seed-proof progressive ($seed_label) sword $prog1_sword -> $prog2_sword, item count $prog1_count"
    fi

    # Round 5: C-only seed chest smoke test. seed_route.py starts from the
    # base snapshot of the dungeon replay, searches a short input, opens a
    # real chest in Eastern Palace and compares the result with seed.json.
    if python3 tools/seed_route.py --c-only --mods-dir "$seed_mods" \
        --save tests/replays-seed/seed-key.sav --room 170 --chest-index 0 \
        --seed-json "$seed_mod/seed.json" --frames 40 \
        --out "$tmpdir/seed-chest-route.txt" \
        > "$tmpdir/seed-smoke.log" 2>&1; then
      grep '^seed-route:' "$tmpdir/seed-smoke.log"
      echo "ok seed-smoke ($seed_label) room 170 chest 0"
    else
      echo "FAIL seed-smoke ($seed_label): C-only chest route failed"
      sed -n '1,40p' "$tmpdir/seed-smoke.log"
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

# Real inverted seed. The ROM and spoiler are user-provided files outside git,
# so this case is skipped with a warning when they are not available. It
# imports the seed, checks the 251 ROM locations against the spoiler count,
# and proves the start room and the cave exit (0x112 -> 0x53) in the US
# engine. The map tile data is imported into seed.json by the same command;
# the side-by-side screenshots are produced outside the regress.
inv_common_root=$(cd "$(git rev-parse --git-common-dir 2>/dev/null || echo .git)/.." && pwd)
inv_rom="$inv_common_root/.agents/alttpr/download/seed-LyVe6KOAvb.sfc"
inv_json="$inv_common_root/.agents/alttpr/download/seed-LyVe6KOAvb.json"
if [ ! -f "$inv_rom" ]; then
  echo "skip inverted-seed: .agents/alttpr/download/seed-LyVe6KOAvb.sfc not found"
else
  inv_mods="$tmpdir/inverted-seed-mods"
  inv_mod="$inv_mods/seed-LyVe6KOAvb"
  mkdir -p "$inv_mods"
  inv_import_args=(--rom "$inv_rom" --no-dialogue -o "$inv_mod")
  if [ -f "$inv_json" ]; then
    inv_import_args+=(--json "$inv_json")
  fi
  if ! python3 tools/alttpr_seed.py import "${inv_import_args[@]}" \
      > "$tmpdir/inverted-import.log" 2>&1; then
    echo "FAIL inverted-seed: import"
    sed -n '1,30p' "$tmpdir/inverted-import.log"
    fail=1
  else
    if [ -f "$inv_json" ]; then
      inv_coverage_args=(--rom "$inv_rom" --mod "$inv_mod" --json "$inv_json" --summary)
    else
      inv_coverage_args=(--rom "$inv_rom" --mod "$inv_mod" --summary)
    fi
    if ! python3 tools/alttpr_seed.py coverage "${inv_coverage_args[@]}" \
        > "$tmpdir/inverted-coverage.log" 2>&1 || \
       ! grep -q '^seed-import: coverage: 251 of 251 locations applied' \
         "$tmpdir/inverted-coverage.log"; then
      echo "FAIL inverted-seed: coverage"
      sed -n '1,30p' "$tmpdir/inverted-coverage.log"
      fail=1
    else
      grep '^seed-import: coverage:' "$tmpdir/inverted-coverage.log"
      if [ -f "$inv_json" ]; then
        grep '^seed-import: spoiler:' "$tmpdir/inverted-coverage.log" || true
      fi
      echo "ok inverted-seed import+coverage 251/251"
    fi

    if python3 - "$inv_rom" <<'PY'
import sys
sys.path.insert(0, "tools")
import alttpr_seed
rom = open(sys.argv[1], "rb").read()
overlay = alttpr_seed.seed_dialogue_overlay(rom)
assert 60 in overlay and 62 in overlay
assert "crystals" in overlay[60]
assert "Ganon" in overlay[62]
PY
    then
      echo "ok inverted-proof hc-sign dialogue 0x3C/0x3E"
    else
      echo "FAIL inverted-proof hc-sign dialogue 0x3C/0x3E"
      fail=1
    fi

    if python3 - "$inv_rom" "$tmpdir/inverted-font.png" <<'PY'
import sys
sys.path.insert(0, "tools")
import alttpr_seed
from PIL import Image
rom = open(sys.argv[1], "rb").read()
out = sys.argv[2]
assert alttpr_seed.write_inverted_font(rom, out)
base = Image.open(".modkit/base/files/font.png").convert("P")
font = Image.open(out).convert("P")
assert font.size == base.size
assert font.tobytes() != base.tobytes()
PY
    then
      echo "ok inverted-proof font NewFontInverted"
    else
      echo "FAIL inverted-proof font NewFontInverted"
      fail=1
    fi

    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-start.out" 2> "$tmpdir/inverted-start.err"
    inv_start_rc=$?
    inv_start_ok=1
    grep -q '^state dungeon_room_index 274$' "$tmpdir/inverted-start.out" || inv_start_ok=0
    grep -q '^state savegame_is_darkworld 64$' "$tmpdir/inverted-start.out" || inv_start_ok=0
    grep -q '^state player_is_indoors 1$' "$tmpdir/inverted-start.out" || inv_start_ok=0
    grep -q '^state link_health_current 24$' "$tmpdir/inverted-start.out" || inv_start_ok=0
    if [ "$inv_start_rc" != 0 ] || [ "$inv_start_ok" != 1 ]; then
      echo "FAIL inverted-seed: start room 0x112"
      sed -n '1,20p' "$tmpdir/inverted-start.err"
      grep -E '^state (dungeon_room_index|savegame_is_darkworld|player_is_indoors|link_health_current) ' \
        "$tmpdir/inverted-start.out" || true
      fail=1
    else
      echo "ok inverted-seed start room 0x112 in the Dark World"
    fi

    cat > "$tmpdir/inverted-leave.txt" <<'TXT'
1400 Down
2400 -
TXT
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --stop-replay-at 1300 \
      --input-script "$tmpdir/inverted-leave.txt" --frames 1700 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-exit.out" 2> "$tmpdir/inverted-exit.err"
    inv_exit_rc=$?
    inv_exit_ok=1
    grep -q '^state overworld_screen_index 83$' "$tmpdir/inverted-exit.out" || inv_exit_ok=0
    grep -q '^state main_module_index 9$' "$tmpdir/inverted-exit.out" || inv_exit_ok=0
    grep -q '^state player_is_indoors 0$' "$tmpdir/inverted-exit.out" || inv_exit_ok=0
    if [ "$inv_exit_rc" != 0 ] || [ "$inv_exit_ok" != 1 ]; then
      echo "FAIL inverted-seed: cave exit 0x112 -> 0x53"
      sed -n '1,20p' "$tmpdir/inverted-exit.err"
      grep -E '^state (overworld_screen_index|main_module_index|player_is_indoors) ' \
        "$tmpdir/inverted-exit.out" || true
      fail=1
    else
      echo "ok inverted-seed cave exit 0x112 -> screen 0x53"
    fi

    # NPC item proof: force a Sahasrahla sprite in the grant state. The seed's
    # npc_items table must make him give 10 Arrows instead of the vanilla
    # Pegasus Boots.
    inv_npc_mods="$tmpdir/inverted-npc-mods"
    mkdir -p "$inv_npc_mods"
    cp -a "$inv_mod" "$inv_npc_mods/seed-LyVe6KOAvb"
    cat > "$inv_npc_mods/seed-LyVe6KOAvb/npc.lua" <<'LUA'
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame < 1302 or frame > 1400 then return end
  zelda.state.set("sprite_state", 0, 9)
  zelda.state.set("sprite_type", 0, 0x16)
  zelda.state.set("sprite_subtype2", 0, 0)
  zelda.state.set("sprite_ai_state", 0, 2)
  zelda.state.set("sprite_pause", 0, 0)
  zelda.state.set("sprite_defl_bits", 0, 0x80)
  zelda.state.set("link_num_arrows", 0)
  zelda.state.set("link_item_in_hand", 0)
end)
LUA
    python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "npc.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
      "$inv_npc_mods/seed-LyVe6KOAvb/mod.json"
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_npc_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1410 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-npc.out" 2> "$tmpdir/inverted-npc.err"
    inv_npc_rc=$?
    inv_npc_arrows=$(grep '^state link_num_arrows ' "$tmpdir/inverted-npc.out" | awk '{print $3}')
    inv_npc_boots=$(grep '^state link_item_boots ' "$tmpdir/inverted-npc.out" | awk '{print $3}')

    inv_npc_base_mods="$tmpdir/inverted-npc-base-mods"
    mkdir -p "$inv_npc_base_mods"
    cp -a "$inv_npc_mods/seed-LyVe6KOAvb" "$inv_npc_base_mods/seed-LyVe6KOAvb"
    python3 - "$inv_npc_base_mods/seed-LyVe6KOAvb/seed.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d.pop("npc_items", None)
json.dump(d, open(p, "w"), indent=2)
PY
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_npc_base_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1410 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-npc-base.out" 2> "$tmpdir/inverted-npc-base.err"
    inv_npc_base_rc=$?
    inv_npc_base_arrows=$(grep '^state link_num_arrows ' "$tmpdir/inverted-npc-base.out" | awk '{print $3}')
    inv_npc_base_boots=$(grep '^state link_item_boots ' "$tmpdir/inverted-npc-base.out" | awk '{print $3}')

    if [ "$inv_npc_rc" != 0 ] || [ "$inv_npc_base_rc" != 0 ] || \
       [ "$inv_npc_arrows" -le 0 ] || [ "$inv_npc_boots" != 0 ] || \
       [ "$inv_npc_base_arrows" != 0 ] || [ "$inv_npc_base_boots" != 1 ]; then
      echo "FAIL inverted-proof npc-item: arrows $inv_npc_arrows/$inv_npc_base_arrows boots $inv_npc_boots/$inv_npc_base_boots"
      sed -n '1,20p' "$tmpdir/inverted-npc.err"
      sed -n '1,20p' "$tmpdir/inverted-npc-base.err"
      fail=1
    else
      echo "ok inverted-proof npc-item sahasrahla arrows $inv_npc_arrows (vanilla boots)"
    fi

    # The other two direct NPC rewards: Old Man and Sick Kid. Both are
    # forced into their grant state; the seed values replace the vanilla
    # Magic Mirror and Bug Net.
    for npc in old_man sick_kid; do
      if [ "$npc" = old_man ]; then
        npc_type=0xAD; npc_sub=1; npc_ai=0
        npc_check="link_item_moon_pearl=1 link_item_mirror=0"
      else
        npc_type=0x1F; npc_sub=0; npc_ai=2
        npc_check="link_item_bug_net=0"
      fi
      inv_npc2_mods="$tmpdir/inverted-npc-$npc-mods"
      mkdir -p "$inv_npc2_mods"
      cp -a "$inv_mod" "$inv_npc2_mods/seed-LyVe6KOAvb"
      cat > "$inv_npc2_mods/seed-LyVe6KOAvb/npc.lua" <<LUA
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame < 1302 or frame > 1400 then return end
  zelda.state.set("sprite_state", 0, 9)
  zelda.state.set("sprite_type", 0, $npc_type)
  zelda.state.set("sprite_subtype2", 0, $npc_sub)
  zelda.state.set("sprite_ai_state", 0, $npc_ai)
  zelda.state.set("sprite_pause", 0, 0)
  zelda.state.set("sprite_defl_bits", 0, 0x80)
  zelda.state.set("link_item_in_hand", 0)
end)
LUA
      python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "npc.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
        "$inv_npc2_mods/seed-LyVe6KOAvb/mod.json"
      ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_npc2_mods" \
        --replay tests/replays-features/f4e-item-novo.sav --frames 1410 \
        --dump-state zelda3.sfc \
        > "$tmpdir/inverted-npc-$npc.out" 2> "$tmpdir/inverted-npc-$npc.err"
      rc=$?
      ok=1
      for check in $npc_check; do
        name=${check%%=*}; value=${check##*=}
        grep -q "^state $name $value\$" "$tmpdir/inverted-npc-$npc.out" || ok=0
      done
      if [ "$npc" = old_man ]; then
        npc_base_check="link_item_mirror=2 link_item_moon_pearl=0"
      else
        npc_base_check="link_item_bug_net=1"
      fi
      inv_npc2_base_mods="$tmpdir/inverted-npc-$npc-base-mods"
      mkdir -p "$inv_npc2_base_mods"
      cp -a "$inv_npc2_mods/seed-LyVe6KOAvb" "$inv_npc2_base_mods/seed-LyVe6KOAvb"
      python3 - "$inv_npc2_base_mods/seed-LyVe6KOAvb/seed.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d.pop("npc_items", None)
json.dump(d, open(p, "w"), indent=2)
PY
      ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_npc2_base_mods"         --replay tests/replays-features/f4e-item-novo.sav --frames 1410         --dump-state zelda3.sfc         > "$tmpdir/inverted-npc-$npc-base.out" 2> "$tmpdir/inverted-npc-$npc-base.err"
      base_rc=$?
      base_ok=1
      for check in $npc_base_check; do
        name=${check%%=*}; value=${check##*=}
        grep -q "^state $name $value\$" "$tmpdir/inverted-npc-$npc-base.out" || base_ok=0
      done
      if [ "$rc" != 0 ] || [ "$ok" != 1 ] || \
         [ "$base_rc" != 0 ] || [ "$base_ok" != 1 ]; then
        echo "FAIL inverted-proof npc-item $npc"
        sed -n '1,20p' "$tmpdir/inverted-npc-$npc.err"
        sed -n '1,20p' "$tmpdir/inverted-npc-$npc-base.err"
        fail=1
      else
        echo "ok inverted-proof npc-item $npc"
      fi
    done

    # MirrorBonk on the real mirror-use path: equip the mirror, put Link
    # inside the seed rectangle on screen 0x43 and press Y. Inside must enter
    # RecoilWall; outside must start the normal warping submodule instead.
    inv_mirror_use_mods="$tmpdir/inverted-mirror-use-mods"
    mkdir -p "$inv_mirror_use_mods"
    cp -a "$inv_mod" "$inv_mirror_use_mods/seed-LyVe6KOAvb"
    cat > "$inv_mirror_use_mods/seed-LyVe6KOAvb/use.lua" <<'LUA'
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame < 1300 or frame > 1401 then return end
  zelda.state.set("overworld_screen_index", 0x43)
  zelda.state.set("savegame_is_darkworld", 0x40)
  zelda.state.set("player_is_indoors", 0)
  zelda.state.set("link_x_coord", 0x0600)
  zelda.state.set("link_y_coord", 0x0610)
  zelda.state.set("link_player_handler_state", 0)
  zelda.state.set("main_module_index", 9)
  zelda.state.set("submodule_index", 0)
  zelda.state.set("subsubmodule_index", 0)
  zelda.state.set("link_item_mirror", 2)
  zelda.state.set("current_item_y", 20)
  zelda.state.set("current_item_active", 20)
  zelda.state.set("link_item_in_hand", 0)
  zelda.state.set("link_position_mode", 0)
  zelda.state.set("link_is_bunny_mirror", 0)
end)
LUA
    python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "use.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
      "$inv_mirror_use_mods/seed-LyVe6KOAvb/mod.json"
    cat > "$tmpdir/inverted-mirror-use.txt" <<'TXT'
1400 Y
1410 -
TXT
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_mirror_use_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --stop-replay-at 1300 \
      --input-script "$tmpdir/inverted-mirror-use.txt" --frames 1401 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-mirror-use.out" 2> "$tmpdir/inverted-mirror-use.err"
    inv_mirror_use_rc=$?
    inv_mirror_use_ok=1
    grep -q '^state link_player_handler_state 2$' "$tmpdir/inverted-mirror-use.out" || inv_mirror_use_ok=0
    grep -q '^state submodule_index 0$' "$tmpdir/inverted-mirror-use.out" || inv_mirror_use_ok=0

    inv_mirror_use_off_mods="$tmpdir/inverted-mirror-use-off-mods"
    mkdir -p "$inv_mirror_use_off_mods"
    cp -a "$inv_mirror_use_mods/seed-LyVe6KOAvb" "$inv_mirror_use_off_mods/seed-LyVe6KOAvb"
    sed -i 's/0x0600/0x0100/; s/0x0610/0x0100/' \
      "$inv_mirror_use_off_mods/seed-LyVe6KOAvb/use.lua"
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_mirror_use_off_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --stop-replay-at 1300 \
      --input-script "$tmpdir/inverted-mirror-use.txt" --frames 1401 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-mirror-use-off.out" 2> "$tmpdir/inverted-mirror-use-off.err"
    inv_mirror_use_off_rc=$?
    inv_mirror_use_off_ok=1
    grep -q '^state link_player_handler_state 20$' "$tmpdir/inverted-mirror-use-off.out" || inv_mirror_use_off_ok=0
    grep -q '^state submodule_index 35$' "$tmpdir/inverted-mirror-use-off.out" || inv_mirror_use_off_ok=0

    if [ "$inv_mirror_use_rc" != 0 ] || [ "$inv_mirror_use_off_rc" != 0 ] || \
       [ "$inv_mirror_use_ok" != 1 ] || [ "$inv_mirror_use_off_ok" != 1 ]; then
      echo "FAIL inverted-proof mirror-bonk use"
      sed -n '1,20p' "$tmpdir/inverted-mirror-use.err"
      sed -n '1,20p' "$tmpdir/inverted-mirror-use-off.err"
      grep -E '^state (link_player_handler_state|submodule_index) ' \
        "$tmpdir/inverted-mirror-use.out" "$tmpdir/inverted-mirror-use-off.out" || true
      fail=1
    else
      echo "ok inverted-proof mirror-bonk use"
    fi

    # Dynamic Turtle Rock collision proof on screen 0x47. The start exit row
    # is pointed at the screen, then Link is placed above the opened tiles at
    # global x=0xF08, y=0x80 and walks down. The imported inverted table must
    # let him through; the JP base table at $8F:FD94 must block the same step.
    inv_tr_mods="$tmpdir/inverted-tr-mods"
    mkdir -p "$inv_tr_mods"
    cp -a "$inv_mod" "$inv_tr_mods/seed-LyVe6KOAvb"
    cat > "$inv_tr_mods/seed-LyVe6KOAvb/tr-step.lua" <<'LUA'
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame ~= 2800 then return end
  zelda.state.set("overworld_screen_index", 0x47)
  zelda.state.set("savegame_is_darkworld", 0x40)
  zelda.state.set("player_is_indoors", 0)
  zelda.state.set("link_x_coord", 0x0F08)
  zelda.state.set("link_y_coord", 0x0080)
  zelda.state.set("camera_x_coord_scroll_low", 0x0F00)
  zelda.state.set("camera_y_coord_scroll_low", 0x0000)
  zelda.state.set("link_player_handler_state", 0)
  zelda.state.set("main_module_index", 9)
  zelda.state.set("submodule_index", 0)
  zelda.state.set("subsubmodule_index", 0)
end)
LUA
    python3 - "$inv_tr_mods/seed-LyVe6KOAvb/seed.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
row = d["z3r_settings"]["starting_area_exit_table"]
row[2] = 0x47
row[9] = 0x00
row[10] = 0x01
row[11] = 0x80
row[12] = 0x0E
json.dump(d, open(p, "w"), indent=2)
PY
    python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "tr-step.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
      "$inv_tr_mods/seed-LyVe6KOAvb/mod.json"
    cat > "$tmpdir/inverted-tr-step.txt" <<'TXT'
1400 Down
3000 -
TXT
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_tr_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --stop-replay-at 1300 \
      --input-script "$tmpdir/inverted-tr-step.txt" --frames 3300 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-tr-step.out" 2> "$tmpdir/inverted-tr-step.err"
    inv_tr_rc=$?
    inv_tr_y=$(grep '^state link_y_coord ' "$tmpdir/inverted-tr-step.out" | awk '{print $3}')
    inv_tr_screen=$(grep '^state overworld_screen_index ' "$tmpdir/inverted-tr-step.out" | awk '{print $3}')

    inv_tr_base_mods="$tmpdir/inverted-tr-base-mods"
    mkdir -p "$inv_tr_base_mods"
    cp -a "$inv_tr_mods/seed-LyVe6KOAvb" "$inv_tr_base_mods/seed-LyVe6KOAvb"
    python3 - "$inv_tr_base_mods/seed-LyVe6KOAvb/seed.json" "$inv_rom" <<'PY'
import json, sys
def pc(snes):
    return ((snes >> 16) & 0x7f) * 0x8000 + (snes & 0x7fff)
rom = open(sys.argv[2], "rb").read()
p = sys.argv[1]
d = json.load(open(p))
off = pc(0x8FFD94)
d["tr_tile_attributes"] = rom[off:off + 512].hex()
json.dump(d, open(p, "w"), indent=2)
PY
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_tr_base_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --stop-replay-at 1300 \
      --input-script "$tmpdir/inverted-tr-step.txt" --frames 3300 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-tr-base.out" 2> "$tmpdir/inverted-tr-base.err"
    inv_tr_base_rc=$?
    inv_tr_base_y=$(grep '^state link_y_coord ' "$tmpdir/inverted-tr-base.out" | awk '{print $3}')

    if [ "$inv_tr_rc" != 0 ] || [ "$inv_tr_base_rc" != 0 ] || \
       [ "$inv_tr_screen" != 71 ] || [ "$inv_tr_y" -lt 224 ] || \
       [ "$inv_tr_base_y" -ge 144 ]; then
      echo "FAIL inverted-proof tr-step: screen $inv_tr_screen y $inv_tr_y/$inv_tr_base_y"
      sed -n '1,20p' "$tmpdir/inverted-tr-step.err"
      sed -n '1,20p' "$tmpdir/inverted-tr-base.err"
      grep -E '^state (link_x_coord|link_y_coord|overworld_screen_index) ' \
        "$tmpdir/inverted-tr-step.out" "$tmpdir/inverted-tr-base.out" || true
      fail=1
    else
      echo "ok inverted-proof tr-step (0x47) y $inv_tr_base_y -> $inv_tr_y"
    fi

    # ShouldStartatExit proof: force the spawn-select choice that the seed
    # marks with should_start_at_exit. With the flag the engine must switch to
    # overworld mode and load the start room's exit; with the flag cleared it
    # must stay on the interior preview.
    inv_spawn_mods="$tmpdir/inverted-spawn-mods"
    mkdir -p "$inv_spawn_mods"
    cp -a "$inv_mod" "$inv_spawn_mods/seed-LyVe6KOAvb"
    cat > "$inv_spawn_mods/seed-LyVe6KOAvb/spawn.lua" <<'LUA'
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame < 1298 or frame > 1305 then return end
  zelda.state.set("choice_in_multiselect_box", 2)
  if frame >= 1302 then
    zelda.state.set("main_module_index", 0x1B)
    zelda.state.set("submodule_index", 0)
    zelda.state.set("which_starting_point", 0)
  end
end)
LUA
    python3 - "$inv_spawn_mods/seed-LyVe6KOAvb/seed.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["z3r_settings"]["should_start_at_exit"] = [0, 0, 1]
json.dump(d, open(p, "w"), indent=2)
PY
    python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "spawn.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
      "$inv_spawn_mods/seed-LyVe6KOAvb/mod.json"
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_spawn_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1306 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-spawn.out" 2> "$tmpdir/inverted-spawn.err"
    inv_spawn_rc=$?
    inv_spawn_room=$(grep '^state dungeon_room_index ' "$tmpdir/inverted-spawn.out" | awk '{print $3}')
    inv_spawn_screen=$(grep '^state overworld_screen_index ' "$tmpdir/inverted-spawn.out" | awk '{print $3}')
    inv_spawn_indoor=$(grep '^state player_is_indoors ' "$tmpdir/inverted-spawn.out" | awk '{print $3}')
    inv_spawn_module=$(grep '^state main_module_index ' "$tmpdir/inverted-spawn.out" | awk '{print $3}')

    inv_spawn_off_mods="$tmpdir/inverted-spawn-off-mods"
    mkdir -p "$inv_spawn_off_mods"
    cp -a "$inv_spawn_mods/seed-LyVe6KOAvb" "$inv_spawn_off_mods/seed-LyVe6KOAvb"
    python3 - "$inv_spawn_off_mods/seed-LyVe6KOAvb/seed.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["z3r_settings"]["should_start_at_exit"] = [0, 0, 0]
json.dump(d, open(p, "w"), indent=2)
PY
    ./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_spawn_off_mods" \
      --replay tests/replays-features/f4e-item-novo.sav --frames 1306 \
      --dump-state zelda3.sfc \
      > "$tmpdir/inverted-spawn-off.out" 2> "$tmpdir/inverted-spawn-off.err"
    inv_spawn_off_rc=$?
    inv_spawn_off_indoor=$(grep '^state player_is_indoors ' "$tmpdir/inverted-spawn-off.out" | awk '{print $3}')
    inv_spawn_off_module=$(grep '^state main_module_index ' "$tmpdir/inverted-spawn-off.out" | awk '{print $3}')

    if [ "$inv_spawn_rc" != 0 ] || [ "$inv_spawn_off_rc" != 0 ] || \
       [ "$inv_spawn_room" != 241 ] || [ "$inv_spawn_screen" != 3 ] || \
       [ "$inv_spawn_indoor" != 0 ] || [ "$inv_spawn_module" != 8 ] || \
       [ "$inv_spawn_off_indoor" != 1 ] || [ "$inv_spawn_off_module" != 7 ]; then
      echo "FAIL inverted-proof should-start-at-exit: room $inv_spawn_room screen $inv_spawn_screen indoor $inv_spawn_indoor/$inv_spawn_off_indoor module $inv_spawn_module/$inv_spawn_off_module"
      sed -n '1,20p' "$tmpdir/inverted-spawn.err"
      sed -n '1,20p' "$tmpdir/inverted-spawn-off.err"
      grep -E '^state (dungeon_room_index|overworld_screen_index|player_is_indoors|main_module_index) ' \
        "$tmpdir/inverted-spawn.out" "$tmpdir/inverted-spawn-off.out" || true
      fail=1
    else
      echo "ok inverted-proof should-start-at-exit room $inv_spawn_room -> screen $inv_spawn_screen"
    fi
  fi
fi

# Inverted mode: a settings-only mod forged in the temporary directory. The
# new-file replay creates a save; with InvertedMode the save must land on the
# Dark World house screen (0x6C) and must not be a bunny without the pearl.
# A second run moves the same save to the Light World Sanctuary screen; there
# the inverted pearl rule must turn Link into a bunny. The vanilla run at the
# same frame is the gate: an inverted=0 mod must not change any state.
inv_root="$tmpdir/inverted-proof"
inv_start_dir="$inv_root/start"
inv_van_dir="$inv_root/van"
inv_mod="$inv_start_dir/seed-inverted"
inv_van="$inv_van_dir/seed-not-inverted"
mkdir -p "$inv_mod" "$inv_van"
cat > "$inv_mod/mod.json" <<'JSON'
{
  "id": "seed-inverted-proof",
  "name": "Inverted proof",
  "version": "1.0.0",
  "api": 1,
  "enabled": true,
  "features": {"Z3rRules": true},
  "seed_data": "seed.json"
}
JSON
cat > "$inv_mod/seed.json" <<'JSON'
{
  "format": 1,
  "seed": "inverted-proof",
  "z3r_settings": {"inverted": 1},
  "locations": []
}
JSON
cp "$inv_mod/mod.json" "$inv_van/mod.json"
sed -i 's/seed-inverted-proof/seed-not-inverted-proof/; s/Inverted proof/Vanilla proof/' "$inv_van/mod.json"
sed 's/"inverted": 1/"inverted": 0/' "$inv_mod/seed.json" > "$inv_van/seed.json"

./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_start_dir" \
  --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
  --dump-state zelda3.sfc \
  > "$tmpdir/inverted-start.out" 2> "$tmpdir/inverted-start.err"
inv_start_rc=$?
inv_start_ok=1
grep -q '^state savegame_is_darkworld 64$' "$tmpdir/inverted-start.out" || inv_start_ok=0
grep -q '^state overworld_screen_index 108$' "$tmpdir/inverted-start.out" || inv_start_ok=0
grep -q '^state link_is_bunny 0$' "$tmpdir/inverted-start.out" || inv_start_ok=0
grep -q '^state sram_progress_indicator 2$' "$tmpdir/inverted-start.out" || inv_start_ok=0
./zelda3 --config tests/regress.ini --headless --no-mods \
  --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
  --dump-state zelda3.sfc \
  > "$tmpdir/inverted-gate.out" 2> "$tmpdir/inverted-gate.err"
inv_gate_rc=$?
./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_van_dir" \
  --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
  --dump-state zelda3.sfc \
  > "$tmpdir/inverted-vanilla.out" 2> "$tmpdir/inverted-vanilla.err"
inv_van_rc=$?
grep '^state ' "$tmpdir/inverted-gate.out" > "$tmpdir/inverted-gate.state"
grep '^state ' "$tmpdir/inverted-vanilla.out" > "$tmpdir/inverted-vanilla.state"
if [ "$inv_start_rc" != 0 ] || [ "$inv_gate_rc" != 0 ] || \
   [ "$inv_van_rc" != 0 ] || [ "$inv_start_ok" != 1 ] || \
   ! diff -q "$tmpdir/inverted-gate.state" "$tmpdir/inverted-vanilla.state" >/dev/null; then
  echo "FAIL inverted-proof: start/gate"
  sed -n '1,20p' "$tmpdir/inverted-start.err"
  sed -n '1,20p' "$tmpdir/inverted-gate.err"
  sed -n '1,20p' "$tmpdir/inverted-vanilla.err"
  fail=1
else
  echo "ok inverted-proof start dark world 0x6c, no bunny"
fi

# The same settings-only mod with a Lua fixture that moves the loaded save to
# the Light World Sanctuary screen without a pearl. Inverted mode must make
# Link a bunny there.
cp "$inv_mod/mod.json" "$inv_mod/mod.json.bak"
cp "$inv_mod/seed.json" "$inv_mod/seed.json.bak"
cat > "$inv_mod/inverted.lua" <<'LUA'
local frame = 0
zelda.on("FrameStart", function()
  frame = frame + 1
  if frame ~= 1250 then return end
  zelda.state.set("dungeon_room_index", 0x12)
  zelda.state.set("main_module_index", 8)
  zelda.state.set("submodule_index", 0)
  zelda.state.set("subsubmodule_index", 0)
  zelda.state.set("savegame_is_darkworld", 0)
  zelda.state.set("player_is_indoors", 0)
end)
LUA
python3 -c 'import json, sys; p = sys.argv[1]; m = json.load(open(p)); m["script"] = "inverted.lua"; m["permissions"] = ["state_write"]; json.dump(m, open(p, "w"), indent=2)' \
  "$inv_mod/mod.json"
./zelda3 --config tests/regress.ini --headless --mods-dir "$inv_start_dir" \
  --replay tests/replays-features/f4e-item-novo.sav --frames 1300 \
  --dump-state zelda3.sfc \
  > "$tmpdir/inverted-pearl.out" 2> "$tmpdir/inverted-pearl.err"
inv_pearl_rc=$?
inv_pearl_ok=1
grep -q '^state savegame_is_darkworld 0$' "$tmpdir/inverted-pearl.out" || inv_pearl_ok=0
grep -q '^state overworld_screen_index 19$' "$tmpdir/inverted-pearl.out" || inv_pearl_ok=0
grep -q '^state link_is_bunny 1$' "$tmpdir/inverted-pearl.out" || inv_pearl_ok=0
if [ "$inv_pearl_rc" != 0 ] || [ "$inv_pearl_ok" != 1 ]; then
  echo "FAIL inverted-proof pearl/bunny in the Light World"
  sed -n '1,20p' "$tmpdir/inverted-pearl.err"
  grep -E '^state (savegame_is_darkworld|overworld_screen_index|link_is_bunny) ' \
    "$tmpdir/inverted-pearl.out" || true
  fail=1
else
  echo "ok inverted-proof pearl/bunny in the Light World"
fi

# Restore the script-less variant used by the start/gate run and drop the
# temporary copies; the directory lives under the regress tmpdir.
mv "$inv_mod/mod.json.bak" "$inv_mod/mod.json"
mv "$inv_mod/seed.json.bak" "$inv_mod/seed.json"

# Options menu: open it, toggle a feature, write zelda3.user.ini and boot the
# generated file through the normal config parser (include + key names).
menu_start=$(date +%s%N)
if tools/test-menu.sh > "$tmpdir/menu.out" 2> "$tmpdir/menu.err"; then
  menu_end=$(date +%s%N)
  echo "ok menu-headless $(( (menu_end - menu_start) / 1000000 ))ms"
else
  echo "FAIL menu-headless"
  sed -n '1,40p' "$tmpdir/menu.err"
  fail=1
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
