#!/usr/bin/env python3
"""Measure the voxel gauntlet against the normal SNES frame.

Usage: voxel_gauntlet_metrics.py <gauntlet-output-dir>

Reads the normal/voxel PPM pairs and writes metrics.txt:
  - background fraction (largest border-connected flat region);
  - Link displacement from the OAM sprite (normal JSON) and the plugin's
    projected Link point;
  - mean luminance ratio voxel/normal;
  - mean absolute RGB difference at a forced 0-degree camera, when the
    gauntlet was run with VOXEL_GAUNTLET_0DEG=1.
"""

import glob
import json
import os
import sys
from collections import Counter, deque

from PIL import Image


def border_color(im):
    w, h = im.size
    colors = []
    for x in range(w):
        colors.append(im.getpixel((x, 0)))
        colors.append(im.getpixel((x, h - 1)))
    for y in range(h):
        colors.append(im.getpixel((0, y)))
        colors.append(im.getpixel((w - 1, y)))
    return Counter(colors).most_common(1)[0][0]


def background_fraction(im, target=None):
    """Largest border-connected region of the background colour."""
    w, h = im.size
    bg = border_color(im) if target is None else target
    seen = bytearray(w * h)
    queue = deque()

    def add(x, y):
        if 0 <= x < w and 0 <= y < h:
            idx = y * w + x
            if not seen[idx] and im.getpixel((x, y)) == bg:
                seen[idx] = 1
                queue.append((x, y))

    for x in range(w):
        add(x, 0)
        add(x, h - 1)
    for y in range(h):
        add(0, y)
        add(w - 1, y)
    count = 0
    while queue:
        x, y = queue.popleft()
        count += 1
        add(x + 1, y)
        add(x - 1, y)
        add(x, y + 1)
        add(x, y - 1)
    return count / float(w * h), bg


def luminance(im):
    total = 0.0
    n = 0
    for r, g, b in im.convert("RGB").getdata():
        total += 0.299 * r + 0.587 * g + 0.114 * b
        n += 1
    return total / max(1, n)


def normal_link(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            desc = json.load(f)
    except (OSError, ValueError):
        return None
    disp = desc.get("display", {})
    extra = float(disp.get("extra_left_right", 0))
    if "extra_left_right" not in disp and isinstance(disp.get("extra"), list):
        extra = float(disp["extra"][0])
    best = None
    for s in desc.get("sprites", []):
        if s.get("owner_kind") != 1 or s.get("size") != 16:
            continue
        if best is None or s["y"] + s["size"] > best["y"] + best["size"]:
            best = s
    if best is None:
        return None
    return (best["x"] + extra + best["size"] * 0.5,
            best["y"] + best["size"])


def read_log(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return [ln.split() for ln in f.read().splitlines() if ln.strip()]
    except OSError:
        return []


def projected_link(path):
    lines = [p for p in read_log(path) if p and p[0] != "bg"]
    if not lines:
        return None
    parts = lines[-1]
    if len(parts) < 3:
        return None
    return (float(parts[1]), float(parts[2]))


def logged_bg(path):
    lines = [p for p in read_log(path) if p and p[0] == "bg"]
    if not lines or len(lines[-1]) < 4:
        return None
    return tuple(int(v) for v in lines[-1][1:4])


def diff_mean(a, b):
    if a.size != b.size:
        return float("nan")
    pa = a.convert("RGB").getdata()
    pb = b.convert("RGB").getdata()
    total = 0
    n = 0
    for ca, cb in zip(pa, pb):
        total += abs(ca[0] - cb[0]) + abs(ca[1] - cb[1]) + abs(ca[2] - cb[2])
        n += 3
    return total / max(1, n)


def main(out):
    rows = []
    for normal_path in sorted(glob.glob(os.path.join(out, "*-normal.ppm"))):
        base = normal_path[:-len("-normal.ppm")]
        voxel_path = base + "-voxel.ppm"
        if not os.path.exists(voxel_path):
            continue
        name = os.path.basename(base)
        normal = Image.open(normal_path)
        voxel = Image.open(voxel_path)
        bg_n, _ = background_fraction(normal)
        bg_v, _ = background_fraction(voxel,
                                      logged_bg(base + "-link.txt"))

        # Compare the actual 4:3 game area, not the 16:9 side bars: the latter
        # are black in the normal frame and intentionally filled by the voxel.
        extra = 0
        try:
            with open(base + "-normal.json", "r", encoding="utf-8") as f:
                disp = json.load(f).get("display", {})
            if isinstance(disp.get("extra"), list):
                extra = int(disp["extra"][0])
            else:
                extra = int(disp.get("extra_left_right", 0))
        except (OSError, ValueError):
            pass
        w, h = normal.size
        box = (extra, 0, min(w, extra + 256), h)
        lum_n = luminance(normal.crop(box))
        lum_v = luminance(voxel.crop(box))
        lum_ratio = lum_v / lum_n if lum_n > 0 else float("nan")

        nl = normal_link(base + "-normal.json")
        pl = projected_link(base + "-link.txt")
        if nl is not None and pl is not None:
            dx = pl[0] - nl[0]
            dy = pl[1] - nl[1]
            dist = (dx * dx + dy * dy) ** 0.5
        else:
            dx = dy = dist = float("nan")

        deg_path = base + "-0deg.ppm"
        deg_mean = diff_mean(normal.crop(box), Image.open(deg_path).crop(box)) \
            if os.path.exists(deg_path) else float("nan")

        rows.append((name, bg_n, bg_v, dx, dy, dist, lum_ratio, deg_mean))

    path = os.path.join(out, "metrics.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("scene bg_normal bg_voxel link_dx link_dy link_dist "
                "lum_ratio deg0_mean_abs\n")
        for row in rows:
            f.write("%s %.4f %.4f %.2f %.2f %.2f %.4f %.3f\n" % row)
        if rows:
            f.write("mean_bg_voxel %.4f\n" %
                    (sum(r[2] for r in rows) / len(rows)))
            f.write("mean_lum_ratio %.4f\n" %
                    (sum(r[6] for r in rows) / len(rows)))
            valid = [r[7] for r in rows if r[7] == r[7]]
            if valid:
                f.write("mean_deg0_abs %.3f\n" %
                        (sum(valid) / len(valid)))
    print("metrics -> %s" % path)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: voxel_gauntlet_metrics.py <gauntlet-output-dir>",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
