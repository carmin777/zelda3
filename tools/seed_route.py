#!/usr/bin/env python3
"""Closed-loop seed-oracle route search.

Runs the native C game in seed mode (--seed-rom, no SNES comparison) and
uses --dump-state after short action bursts to choose the next input.  The
generated input tail can be appended to the seed replay and then verified
with the strict --oracle-rom mode.

The search is deliberately simple: a beam over the eight movement
directions (plus "do nothing"), one burst of --step frames per level.  It
prefers reducing the overworld screen distance to a waypoint, walking toward
the matching screen edge, keeping health and avoiding deep water.

Example:
  tools/seed_route.py --start 11200 --targets 0x25,0x20,0x18 \
      --beam 3 --step 60 --max-steps 30 --out /tmp/route.txt
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROM = os.path.join(ROOT, "..", "alttpr", "download", "seed-xvJo08oxyJ.sfc")
DEFAULT_REPLAY = os.path.join(ROOT, "tests", "replays-seed", "seed-xvJo08oxyJ.txt")
DEFAULT_BIN = os.path.join(ROOT, "zelda3")

ACTIONS = [
    "-", "Up", "Down", "Left", "Right",
    "Up Left", "Up Right", "Down Left", "Down Right",
]


def read_base(path):
    with open(path, "r", newline="") as f:
        return f.read()


def parse_state(stdout):
    st = {}
    for line in stdout.splitlines():
        if not line.startswith("state "):
            continue
        parts = line.split()
        if len(parts) == 3:
            st[parts[1]] = parts[2]
    return st


def state_int(st, name, default=0):
    try:
        return int(st.get(name, default))
    except ValueError:
        return default


class Search:
    def __init__(self, args):
        self.args = args
        self.base = read_base(args.replay)
        self.base_last = 0
        for line in self.base.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            try:
                self.base_last = max(self.base_last, int(line.split()[0]))
            except ValueError:
                pass
        self.targets = [int(x, 0) for x in args.targets.split(",")]
        self.seen = set()

    def run(self, tail, frame, oracle=False):
        script = self.base
        if not script.endswith("\n"):
            script += "\n"
        for f, b in tail:
            script += f"{f} {b}\n"
        fd, path = tempfile.mkstemp(prefix="seed-route-", suffix=".txt")
        try:
            with os.fdopen(fd, "w", newline="") as f:
                f.write(script)
            mode = "--oracle-rom" if oracle else "--seed-rom"
            cmd = [
                self.args.bin, "--config", self.args.config, "--headless",
                "--no-mods", mode, self.args.rom,
                "--input-script", path, "--frames", str(frame),
                "--dump-state",
            ]
            p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                               timeout=self.args.timeout)
            st = parse_state(p.stdout)
            return st, p.returncode, p.stderr
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def parse_ctrl(self, st):
        return {
            "screen": state_int(st, "overworld_screen_index"),
            "x": state_int(st, "link_x_coord"),
            "y": state_int(st, "link_y_coord"),
            "health": state_int(st, "link_health_current"),
            "water": state_int(st, "link_is_in_deep_water"),
            "module": state_int(st, "main_module_index"),
            "indoors": state_int(st, "player_is_indoors"),
            "music": state_int(st, "music_unk1"),
            "frame": state_int(st, "frame_counter"),
        }

    def score(self, ctrl, target_idx, start_health, prev):
        if ctrl["module"] == 18 or ctrl["health"] == 0:
            return -1e18
        if ctrl["indoors"]:
            return -1e18
        target = self.targets[target_idx]
        screen = ctrl["screen"]
        row, col = screen >> 3, screen & 7
        trow, tcol = target >> 3, target & 7
        dist = abs(row - trow) + abs(col - tcol)
        edge = 0
        if trow > row:
            edge += ctrl["y"]
        elif trow < row:
            edge -= ctrl["y"]
        if tcol > col:
            edge += ctrl["x"]
        elif tcol < col:
            edge -= ctrl["x"]
        s = -10000.0 * dist + edge
        s += ctrl["health"] * 50
        s -= ctrl["water"] * 100
        s -= max(0, start_health - ctrl["health"]) * 2000
        if screen == target:
            s += 1000000
        if prev is not None:
            # Keep some pressure away from walls and from standing still.
            if ctrl["x"] == prev["x"] and ctrl["y"] == prev["y"]:
                s -= 2000
            if ctrl["screen"] != prev["screen"]:
                s += 2000
        # Novelty: prefer cells the search has not inspected yet.
        cell = (screen, ctrl["x"] // 32, ctrl["y"] // 32)
        if cell not in self.seen:
            s += 500
            self.seen.add(cell)
        return s

    def expand_one(self, item):
        parent, parent_ctrl, tail, frame, target_idx = item
        results = []
        for act in ACTIONS:
            nt = tail + [(frame, act)]
            st, rc, err = self.run(nt, frame + self.args.step)
            if rc != 0:
                continue
            ctrl = self.parse_ctrl(st)
            if ctrl["module"] == 18 or ctrl["health"] == 0 or ctrl["indoors"]:
                continue
            results.append((parent_ctrl, ctrl, nt, frame + self.args.step))
        return results

    def target_ok(self, ctrl, target):
        if ctrl["screen"] != target:
            return False
        if target == 0x18 and self.args.require_music >= 0:
            if ctrl["music"] != self.args.require_music:
                return False
        return True

    def post_check(self, tail, nf, target):
        """Hold no input for --stable-frames and require a stable target."""
        nt = tail + [(nf, "-")]
        st, rc, err = self.run(nt, nf + self.args.stable_frames,
                               oracle=self.args.oracle)
        if rc != 0:
            return False, None, nf
        ctrl = self.parse_ctrl(st)
        if not self.target_ok(ctrl, target):
            return False, ctrl, nf + self.args.stable_frames
        return True, ctrl, nf + self.args.stable_frames

    def go(self):
        if self.args.start <= self.base_last:
            sys.exit("--start must be after the base replay's last frame")
        start_frame = self.args.start
        st, rc, err = self.run([], start_frame)
        if rc != 0:
            sys.exit("seed-route: initial run failed: " + err[-500:])
        ctrl = self.parse_ctrl(st)
        start_health = ctrl["health"]
        print(f"start screen=0x{ctrl['screen']:02X} x={ctrl['x']} y={ctrl['y']} "
              f"health={ctrl['health']} water={ctrl['water']} frame={start_frame}")
        if ctrl["module"] == 18 or ctrl["health"] == 0:
            sys.exit("seed-route: start state is already dead")
        target_idx = 0
        while target_idx < len(self.targets) and ctrl["screen"] == self.targets[target_idx]:
            print(f"reached target 0x{self.targets[target_idx]:02X}")
            target_idx += 1
        if target_idx == len(self.targets):
            print("already at all targets")
            self.write([], start_frame)
            return

        beam = [(None, ctrl, [], start_frame, target_idx)]
        for step_i in range(self.args.max_steps):
            frame = start_frame + step_i * self.args.step
            cands = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=self.args.jobs) as ex:
                for res in ex.map(self.expand_one, beam):
                    cands.extend(res)
            if not cands:
                sys.exit("seed-route: no live candidate")
            scored = []
            reached = []
            for parent_ctrl, ctrl, tail, nf in cands:
                sc = self.score(ctrl, target_idx, start_health, parent_ctrl)
                if ctrl["screen"] == self.targets[target_idx]:
                    reached.append((sc, ctrl, tail, nf))
                scored.append((sc, ctrl, tail, nf))
            if reached:
                reached.sort(key=lambda t: t[0], reverse=True)
                accepted = None
                for sc, ctrl, tail, nf in reached:
                    ok, stable, final_frame = self.post_check(tail, nf, self.targets[target_idx])
                    if ok:
                        accepted = (ctrl, tail, final_frame)
                        print(f"stable target 0x{self.targets[target_idx]:02X} "
                              f"screen=0x{stable['screen']:02X} x={stable['x']} y={stable['y']} "
                              f"health={stable['health']} music={stable['music']}")
                        break
                    if stable is None:
                        print(f"rejected target 0x{self.targets[target_idx]:02X}: "
                              f"run failed at {final_frame}")
                    else:
                        print(f"transient target 0x{self.targets[target_idx]:02X}: "
                              f"screen=0x{stable['screen']:02X} music={stable['music']} "
                              f"at {final_frame}")
                if accepted is not None:
                    ctrl, tail, nf = accepted
                    target_idx += 1
                    if target_idx >= len(self.targets):
                        self.write(tail, nf)
                        return
                    beam = [(None, ctrl, tail, nf, target_idx)]
                    continue
                # Keep searching: treat the transient candidates as ordinary
                # states but do not advance the waypoint.
            scored.sort(key=lambda t: t[0], reverse=True)
            uniq = []
            seen = set()
            for sc, ctrl, tail, nf in scored:
                key = (ctrl["screen"], ctrl["x"], ctrl["y"], ctrl["health"], ctrl["water"])
                if key in seen:
                    continue
                seen.add(key)
                uniq.append((sc, ctrl, tail, nf))
                if len(uniq) >= self.args.beam:
                    break
            beam = [(None, ctrl, tail, nf, target_idx) for sc, ctrl, tail, nf in uniq]
            sc, ctrl, tail, nf = uniq[0]
            print(f"step {step_i}: best screen=0x{ctrl['screen']:02X} "
                  f"x={ctrl['x']} y={ctrl['y']} health={ctrl['health']} "
                  f"water={ctrl['water']} target=0x{self.targets[target_idx]:02X}")
        print("max-steps reached without all targets")
        sc, ctrl, tail, nf = uniq[0]
        self.write(tail, nf)

    def write(self, tail, frame):
        if not self.args.out:
            print("tail:")
            for f, b in tail:
                print(f"{f} {b}")
            return
        with open(self.args.out, "w", newline="") as f:
            f.write(self.base)
            if not self.base.endswith("\n"):
                f.write("\n")
            f.write(f"# generated by tools/seed_route.py through frame {frame}\n")
            for fr, b in tail:
                f.write(f"{fr} {b}\n")
        print(f"wrote {self.args.out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--config", default=os.path.join(ROOT, "tests", "regress.ini"))
    ap.add_argument("--rom", default=DEFAULT_ROM)
    ap.add_argument("--replay", default=DEFAULT_REPLAY)
    ap.add_argument("--start", type=int, required=True)
    ap.add_argument("--targets", default="0x18",
                    help="comma-separated overworld screen indexes")
    ap.add_argument("--step", type=int, default=60)
    ap.add_argument("--beam", type=int, default=3)
    ap.add_argument("--max-steps", type=int, default=30)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--oracle", action="store_true",
                    help="validate the stable target with --oracle-rom")
    ap.add_argument("--stable-frames", type=int, default=120)
    ap.add_argument("--require-music", type=int, default=7,
                    help="music_unk1 required for a stable 0x18 target (-1 disables)")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--out")
    args = ap.parse_args()
    Search(args).go()


if __name__ == "__main__":
    main()
