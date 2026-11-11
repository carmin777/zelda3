#!/usr/bin/env bash
# Headless check for the in-game options menu writer.
#
# Opens the menu, toggles ItemSwitchLR on the Gameplay page and Mono on the
# Sound page through the same action path used by Enter, saves zelda3.user.ini
# and checks the result. The test writes to a temporary directory, never to the
# repository's user ini.
set -eu

cd "$(dirname "$0")/.." || exit 2

for f in zelda3 zelda3.sfc zelda3_assets.dat tests/regress.ini; do
  if [ ! -e "$f" ]; then
    echo "menu: missing $f" >&2
    exit 2
  fi
done

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/zelda3-menu.XXXXXX") || exit 2
trap 'rm -rf "$tmpdir"' EXIT

./zelda3 --config tests/regress.ini --headless \
  --menu-test "$tmpdir/zelda3.user.ini" zelda3.sfc \
  > "$tmpdir/out" 2> "$tmpdir/err"

if ! grep -q 'menu-test: ItemSwitchLR 0 -> 1 mono=1' "$tmpdir/out"; then
  echo "menu: the menu did not toggle ItemSwitchLR or Mono" >&2
  cat "$tmpdir/out" "$tmpdir/err" >&2
  exit 1
fi
if [ ! -s "$tmpdir/zelda3.user.ini" ]; then
  echo "menu: zelda3.user.ini was not created" >&2
  exit 1
fi
for line in '!include zelda3.ini' 'ItemSwitchLR = 1' 'AudioChannels = 1' 'Menu = Home'; do
  if ! grep -qx "$line" "$tmpdir/zelda3.user.ini"; then
    echo "menu: missing '$line' in the generated user ini" >&2
    sed -n '1,80p' "$tmpdir/zelda3.user.ini" >&2
    exit 1
  fi
done

# Boot the generated file through the normal config path (no --config) in a
# temporary directory. This checks that !include, the sections and the key
# names produced by the menu are accepted by the ini parser.
root=$PWD
rundir="$tmpdir/run"
mkdir -p "$rundir"
ln -s "$root/zelda3.ini" "$rundir/zelda3.ini"
ln -s "$root/zelda3_assets.dat" "$rundir/zelda3_assets.dat"
cp "$tmpdir/zelda3.user.ini" "$rundir/zelda3.user.ini"
if ! (cd "$rundir" && "$root/zelda3" --headless --frames 0 \
        > boot.out 2> boot.err); then
  echo "menu: the generated user ini did not boot" >&2
  cat "$rundir/boot.out" "$rundir/boot.err" >&2
  exit 1
fi
if grep -q "Can't parse\\|Duplicate key" "$rundir/boot.err"; then
  echo "menu: the generated user ini has parser errors" >&2
  cat "$rundir/boot.err" >&2
  exit 1
fi

echo "menu: headless save test passed"
