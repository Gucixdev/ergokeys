"""ek.py: solve chord.mod on an ergokeys layout and compare it with the C binary, chord by chord.
   python ek.py [LAYOUT] [-c WEIGHTS]    LAYOUT = a name in ../layouts or a path (default qwerty-ansi)
Needs amplpy with the HiGHS module (see ./run) and ../out/ergokeys (make). Prints how many chords agree and the worst differences."""
from __future__ import annotations

import itertools
import json
import subprocess
import sys
from pathlib import Path

from amplpy import AMPL

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
MODS = ["ctrl", "alt", "super", "shift"]
CNAME = {"ctrl": "Ctrl", "alt": "Alt", "super": "Super", "shift": "Shift"}
FINGERS = ["LP", "LR", "LM", "LI", "LT", "RT", "RI", "RM", "RR", "RP"]


def read_layout(path: Path) -> dict:
    """The .layout format (see ../layouts), read again here so the model does not depend on the C parser."""
    L = {"pitch": 19.05, "keys": {}, "finger": {}, "home": {}, "mod": {m: [] for m in MODS}, "nochord": set()}

    def add(label, xu, yu, w):
        L["keys"][label] = ((xu + w / 2) * L["pitch"], (yu + 0.5) * L["pitch"], (xu + 0.5) * L["pitch"], (xu + w - 0.5) * L["pitch"])
    for raw in path.read_text().splitlines():
        t = raw.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "pitch":
            L["pitch"] = float(t[1])
        elif t[0] == "row":
            for i, lab in enumerate(t[3:]):
                add(lab, float(t[2]) + i, float(t[1]), 1.0)
        elif t[0] == "key":
            add(t[1], float(t[2]), float(t[3]), float(t[4]))
            if "nochord" in t[5:]:
                L["nochord"].add(t[1])
        elif t[0] == "finger":
            for lab in t[2:]:
                L["finger"][lab] = t[1]
        elif t[0] == "home":
            L["home"][t[1]] = (t[2], float(t[3]) if len(t) > 3 else None)
        elif t[0] == "mod":
            L["mod"][t[1]] += t[2:]
    return L


def read_weights(path: Path) -> dict:
    return {l.split()[0]: float(l.split()[1]) for l in path.read_text().splitlines() if l.strip() and not l.startswith("#")}


def main() -> int:
    args = sys.argv[1:]
    wpath = ROOT / "config" / "default.conf"
    if "-c" in args:
        i = args.index("-c")
        wpath = Path(args[i + 1])
        del args[i:i + 2]
    lay = args[0] if args else "qwerty-ansi"
    lpath = Path(lay) if Path(lay).exists() else ROOT / "layouts" / f"{lay}.layout"
    L, W = read_layout(lpath), read_weights(wpath)
    keys = L["keys"]
    pitch = L["pitch"]
    hx, hy = {}, {}
    for f in FINGERS:
        lab, xu = L["home"][f]
        hx[f] = xu * pitch if xu is not None else keys[lab][0]
        hy[f] = keys[lab][1]
    modkeys = {m: [k for k in L["mod"][m] if k in keys] for m in MODS}
    modset = {k for m in MODS for k in modkeys[m]}
    chordable = [k for k in keys if k in L["finger"] and k not in modset and k not in L["nochord"]]
    chords = {}
    for n in range(0, 5):
        for ms in itertools.combinations(MODS, n):
            if any(not modkeys[m] for m in ms):
                continue
            for k in chordable:
                chords[f"c{len(chords)}"] = (ms, k)

    a = AMPL()
    a.read(str(HERE / "chord.mod"))
    a.set["FINGERS"], a.set["KEYS"], a.set["MODS"], a.set["ALLCHORDS"] = FINGERS, list(keys), MODS, list(chords)
    for m in MODS:
        a.set["MODKEY"][m] = modkeys[m]
    a.param["pitch"] = pitch
    for i, name in enumerate(("x", "y", "xlo", "xhi")):
        a.param[name] = {k: v[i] for k, v in keys.items()}
    a.param["fing"] = {k: L["finger"][k] for k in keys if k in L["finger"]}
    a.param["hx"], a.param["hy"] = hx, hy
    a.param["hand"] = {f: int(f[0] == "R") for f in FINGERS}
    a.param["pinky"] = {f: int(f[1] == "P") for f in FINGERS}
    a.param["ring"] = {f: int(f[1] == "R") for f in FINGERS}
    a.param["thumb"] = {f: int(f[1] == "T") for f in FINGERS}
    a.param["cm"] = {(c, m): int(m in ms) for c, (ms, k) in chords.items() for m in MODS}
    a.param["ck"] = {c: k for c, (ms, k) in chords.items()}
    for w in ("travel", "vertical_factor", "pen_mod_pinky", "pen_mod_ring", "pen_one_hand", "pen_far_row", "pen_finger_conflict", "bonus_alternate", "pen_key_pinky", "pen_key_ring"):
        a.param[w] = W[w]
    a.option["solver"] = "highs"
    model, ids = {}, list(chords)
    for i in range(0, len(ids), 40):            # the free AMPL licence allows 2000 variables: 40 chords at a time stay far below
        a.set["CHORDS"] = ids[i:i + 40]
        a.solve()
        if a.get_value("solve_result") != "solved":
            print("solver:", a.get_value("solve_result"), file=sys.stderr)
            return 2
        model.update({c: max(0.0, v) for c, v in a.var["cost"].get_values().to_dict().items()})   # a whole shortcut is clamped at zero

    exe = ROOT / "out" / "ergokeys"
    diffs, bad, badnames = [], 0, []
    for c, (ms, k) in chords.items():
        name = "+".join([CNAME[m] for m in ms] + [k])
        p = subprocess.run([str(exe), "score", name, "-l", str(lpath), "-c", str(wpath), "--json"], capture_output=True, text=True)
        try:
            real = json.loads(p.stdout)["scores"][0]["cost"]
        except (ValueError, KeyError, IndexError):
            bad += 1
            badnames.append(name + " -> " + (p.stderr.strip() or p.stdout.strip())[:80])
            continue
        diffs.append((abs(real - model[c]), name, model[c], real))
    diffs.sort(reverse=True)
    off = [d for d in diffs if d[0] > 1e-3]
    floor = W["pen_finger_conflict"] - 2 * W["bonus_alternate"]
    explained = [d for d in off if min(d[2], d[3]) >= floor]       # a chord with two presses on one finger costs at least pen_finger_conflict minus the bonuses
    print(f"layout {lpath.name}: {len(chords)} chords modelled, {len(diffs)} compared with out/ergokeys, {bad} not parsed by ergokeys, {len(off)} differ by more than 0.001 mm")
    print(f"max difference {diffs[0][0]:.4f}" if diffs else "nothing compared")
    print(f"{len(explained)} of the differing chords are finger-conflict chords (cost >= {floor:g}: one finger presses twice, ergokeys starts the second press where the finger already is, the model at home); {len(off) - len(explained)} are not explained")
    for d, name, m, r in diffs[:8]:
        if d > 1e-3:
            print(f"  {name:32s} model {m:9.4f}  ergokeys {r:9.4f}  diff {d:.4f}")
    for b in badnames[:6]:
        print("  not parsed:", b)
    return 1 if len(off) > len(explained) or bad else 0


if __name__ == "__main__":
    sys.exit(main())
