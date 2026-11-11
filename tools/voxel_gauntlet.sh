#!/bin/bash
# Voxel gauntlet: renders a fixed set of scenes with the normal renderer and
# with plugins/voxel, side by side, in 16:9 widescreen, so each round of voxel
# work is judged on the same pictures.
#
#   tools/voxel_gauntlet.sh <tag> [extra zelda3 args...]
#
# Output (outside git): .agents/shots/gauntlet/<tag>/NN-<scene>.png (normal
# left, voxel right) and grid.png with every scene. Scenes come from the
# reference replays, so no save or ROM data is written anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."
TAG=${1:?usage: tools/voxel_gauntlet.sh <tag>}
shift || true
OUT=.agents/shots/gauntlet/$TAG
mkdir -p "$OUT"
INI=$(mktemp "${TMPDIR:-/tmp}/zelda3-gauntlet.XXXXXX.ini")
trap 'rm -f "$INI"' EXIT
cat > "$INI" <<EOF
!include $(pwd)/tests/regress.ini
[General]
ExtendedAspectRatio = extend_y, 16:9
EOF
make -s -C plugins/voxel >/dev/null

# replay-ref:frame:name — overworld, towns, castle, dungeons, Dark World.
SCENES=(
  "1:1800:links-house-rain"
  "1:4200:castle-courtyard"
  "2:1500:light-world-field"
  "2:9000:eastern-palace"
  "3:2500:kakariko"
  "3:12000:desert"
  "4:6000:death-mountain"
  "5:8000:agahnim-tower"
  "6:3000:dark-world"
  "7:6000:swamp"
  "9:9000:dark-world-village"
  "12:5000:turtle-rock"
  "2:10300:links-house-day"
  "1:1779:lost-woods"
  "4:26700:hyrule-castle-day"
)
i=0
for s in "${SCENES[@]}"; do
  IFS=: read -r ref frame name <<<"$s"
  n=$(printf '%02d' $i)
  ./zelda3 --config "$INI" --headless --no-mods --replay-ref "$ref" \
    --frames "$frame" --frame-desc-dump "$frame" \
    --screenshot "$OUT/$n-$name-normal.ppm" "$@" zelda3.sfc >/dev/null 2>&1 || true
  if [ -f ".agents/shots/frame-desc-$frame.json" ]; then
    cp ".agents/shots/frame-desc-$frame.json" "$OUT/$n-$name-normal.json"
  fi
  rm -f ".agents/shots/frame-desc-$frame"*
  rm -f "$OUT/$n-$name-link.txt"
  ZELDA3_VOXEL_LINK_LOG="$OUT/$n-$name-link.txt" \
    ./zelda3 --config "$INI" --headless --no-mods --replay-ref "$ref" \
    --frames "$frame" --render-plugin plugins/voxel/voxel.so \
    --screenshot "$OUT/$n-$name-voxel.ppm" "$@" zelda3.sfc >/dev/null 2>&1 || true
  if [ "${VOXEL_GAUNTLET_0DEG:-0}" = "1" ]; then
    rm -f "$OUT/$n-$name-0deg-link.txt"
    ZELDA3_VOXEL_CAMERA_ANGLE=90 ZELDA3_VOXEL_CAMERA_ZOOM=1.0 \
      ZELDA3_VOXEL_LINK_LOG="$OUT/$n-$name-0deg-link.txt" \
      ./zelda3 --config "$INI" --headless --no-mods --replay-ref "$ref" \
      --frames "$frame" --render-plugin plugins/voxel/voxel.so \
      --screenshot "$OUT/$n-$name-0deg.ppm" "$@" zelda3.sfc >/dev/null 2>&1 || true
  fi
  i=$((i + 1))
done

python3 tools/voxel_gauntlet_metrics.py "$OUT"

python3 - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image, ImageDraw
out = sys.argv[1]
pairs = []
for normal in sorted(glob.glob(os.path.join(out, '*-normal.ppm'))):
    voxel = normal.replace('-normal.ppm', '-voxel.ppm')
    if not os.path.exists(voxel):
        continue
    a, b = Image.open(normal).convert('RGB'), Image.open(voxel).convert('RGB')
    w, h = a.size
    side = Image.new('RGB', (w * 2 + 4, h + 14), (20, 20, 20))
    side.paste(a, (0, 14)); side.paste(b.resize((w, h)), (w + 4, 14))
    ImageDraw.Draw(side).text((2, 1), os.path.basename(normal)[:-11], fill=(230, 230, 230))
    path = normal.replace('-normal.ppm', '.png')
    side.save(path)
    pairs.append(path)
    os.remove(normal); os.remove(voxel)
if pairs:
    tiles = [Image.open(p) for p in pairs]
    tw, th = tiles[0].size
    cols = 2
    rows = (len(tiles) + cols - 1) // cols
    grid = Image.new('RGB', (tw * cols, th * rows), (0, 0, 0))
    for k, t in enumerate(tiles):
        grid.paste(t, ((k % cols) * tw, (k // cols) * th))
    grid.save(os.path.join(out, 'grid.png'))
print(f'{len(pairs)} scenes -> {out}/grid.png')
PY
