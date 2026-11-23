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


def price(lpath: Path, wpath: Path) -> dict:
    """Solve chord.mod for every chord of the layout. Returns the layout, the weights, the chords {id: (mods, key)} and their costs."""
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
    chords, paths = {}, {}
    for n in range(0, 5):
        for ms in itertools.combinations(MODS, n):
            if any(not modkeys[m] for m in ms):
                continue
            for k in chordable:
                c = f"c{len(chords)}"
                chords[c] = (ms, k)
                for combo in itertools.product(*[modkeys[m] for m in ms]):
                    paths[f"p{len(paths)}"] = (c, ms, combo, k)

    a = AMPL()
    a.read(str(HERE / "chord.mod"))
    a.set["FINGERS"], a.set["KEYS"], a.set["ALLCHORDS"], a.set["ALLPATHS"] = FINGERS, list(keys) + ["none"], list(chords), list(paths)
    a.param["pitch"] = pitch
    for i, name in enumerate(("x", "y", "xlo", "xhi")):
        a.param[name] = {k: v[i] for k, v in keys.items()}
    a.param["fing"] = {k: L["finger"][k] for k in keys if k in L["finger"]}
    a.param["hx"], a.param["hy"] = hx, hy
    a.param["hand"] = {f: int(f[0] == "R") for f in FINGERS}
    a.param["pinky"] = {f: int(f[1] == "P") for f in FINGERS}
    a.param["ring"] = {f: int(f[1] == "R") for f in FINGERS}
    a.param["thumb"] = {f: int(f[1] == "T") for f in FINGERS}
    a.param["chord"] = {p: c for p, (c, ms, combo, k) in paths.items()}
    kk = {}
    for p, (c, ms, combo, k) in paths.items():
        for m, key in zip(ms, combo):
            kk[(p, MODS.index(m) + 1)] = key
        kk[(p, 5)] = k
    a.param["kk"] = kk
    for w in ("travel", "vertical_factor", "pen_mod_pinky", "pen_mod_ring", "pen_one_hand", "pen_far_row", "pen_finger_conflict", "bonus_alternate", "pen_key_pinky", "pen_key_ring"):
        a.param[w] = W[w]
    a.option["solver"] = "highs"
    model, ids = {}, list(chords)
    for i in range(0, len(ids), 40):            # the free AMPL licence allows 2000 variables: 40 chords at a time stay far below
        batch = ids[i:i + 40]
        a.set["CHORDS"] = batch
        a.set["PATHS"] = [p for p, v in paths.items() if v[0] in batch]
        a.solve()
        if a.get_value("solve_result") != "solved":
            print("solver:", a.get_value("solve_result"), file=sys.stderr)
            raise SystemExit(2)
        model.update({c: max(0.0, v) for c, v in a.var["cost"].get_values().to_dict().items()})   # a whole shortcut is clamped at zero

    return {"L": L, "W": W, "chords": chords, "model": model, "chordable": chordable, "modkeys": modkeys}


def parse_tsv(path: Path) -> list:
    """action TAB chord [TAB weight [TAB fixed]] -> [(action, mods tuple in MODS order, key, weight, fixed)]; single-step chords only."""
    rows = []
    for raw in path.read_text().splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        f = raw.split("\t")
        parts = f[1].split("+")
        ms = {p.lower() for p in parts[:-1]}
        if len(f) < 2 or " " in f[1] or not ms <= set(MODS):
            raise SystemExit(f"ek.py assign: only single-step chords with ctrl/alt/super/shift: {raw!r}")
        rows.append((f[0], tuple(m for m in MODS if m in ms), parts[-1].lower(), float(f[2]) if len(f) > 2 and f[2] else 1.0, len(f) > 3 and f[3] in ("fixed", "pin")))
    return rows


def assign_main(args: list) -> int:
    """ek.py assign TSV|--random N [--seed S] [-l LAYOUT] [-c WEIGHTS]: the exact optimum of assign.mod against `ergokeys suggest`."""
    import random
    import tempfile
    seed, nrand, lay, wpath, tsv = 1, 0, "qwerty-ansi", ROOT / "config" / "default.conf", None
    while args:
        a = args.pop(0)
        if a == "--random":
            nrand = int(args.pop(0))
        elif a == "--seed":
            seed = int(args.pop(0))
        elif a == "-l":
            lay = args.pop(0)
        elif a == "-c":
            wpath = Path(args.pop(0))
        else:
            tsv = Path(a)
    lpath = Path(lay) if Path(lay).exists() else ROOT / "layouts" / f"{lay}.layout"
    P = price(lpath, wpath)
    chords, model = P["chords"], P["model"]
    cid = {v: c for c, v in chords.items()}
    if nrand:
        rng = random.Random(seed)
        groups = sorted({v[0] for v in chords.values() if v[0]})
        used, lines = set(), []
        while len(lines) < nrand:
            ms = rng.choice(groups)
            k = rng.choice(P["chordable"])
            if (ms, k) in used:
                continue
            used.add((ms, k))
            lines.append(f"a{len(lines)}\t{'+'.join(CNAME[m] for m in ms)}+{k}\t{rng.randint(1, 100)}" + ("\tfixed" if rng.random() < 0.1 else ""))
        tsv = Path(tempfile.mkdtemp()) / "random.tsv"
        tsv.write_text("\n".join(lines) + "\n")
    rows = parse_tsv(tsv)
    fixed_cost = sum(w * model[cid[(ms, k)]] for _, ms, k, w, fx in rows if fx)
    reserved = {cid[(ms, k)] for _, ms, k, w, fx in rows if fx}
    move = [(n, ms, w) for n, ms, k, w, fx in rows if not fx]
    # only the cheapest free chords of a modifier set can matter (n movable + n reserved there): pruning is exact and keeps the model small
    ok, keep = {}, set()
    for ms in {m for _, m, _ in move}:
        need = sum(1 for _, m, _ in move if m == ms)
        free = sorted((model[c], c) for c, v in chords.items() if v[0] == ms and c not in reserved)[:need]
        for a, m, _ in move:
            if m == ms:
                ok[a] = [c for _, c in free]
        keep |= {c for _, c in free}
    am = AMPL()
    am.read(str(HERE / "assign.mod"))
    am.set["ACTIONS"], am.set["CHORDS"] = [n for n, _, _ in move], sorted(keep)
    am.param["cost"] = {c: model[c] for c in keep}
    am.param["weight"] = {n: w for n, _, w in move}
    for n in ok:
        am.set["OK"][n] = ok[n]
    am.option["solver"] = "highs"
    am.solve()
    if am.get_value("solve_result") != "solved":
        print("solver:", am.get_value("solve_result"), file=sys.stderr)
        return 2
    best = am.get_objective("total").value() + fixed_cost
    p = subprocess.run([str(ROOT / "out" / "ergokeys"), "suggest", str(tsv), "-l", str(lpath), "-c", str(wpath), "--json"], capture_output=True, text=True)
    got = json.loads(p.stdout)["after"]["cost"]
    start = json.loads(p.stdout)["before"]["cost"]
    print(f"{len(rows)} actions ({len(rows) - len(move)} fixed): AMPL optimum {best:.4f}, ergokeys suggest {got:.4f}, before {start:.4f}, difference {got - best:+.4f}")
    return 0 if abs(got - best) < 1e-3 else 1


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "assign":
        return assign_main(args[1:])
    wpath = ROOT / "config" / "default.conf"
    if "-c" in args:
        i = args.index("-c")
        wpath = Path(args[i + 1])
        del args[i:i + 2]
    lay = args[0] if args else "qwerty-ansi"
    lpath = Path(lay) if Path(lay).exists() else ROOT / "layouts" / f"{lay}.layout"
    P = price(lpath, wpath)
    chords, model = P["chords"], P["model"]
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
    print(f"layout {lpath.name}: {len(chords)} chords modelled, {len(diffs)} compared with out/ergokeys, {bad} not parsed by ergokeys, {len(off)} differ by more than 0.001 mm")
    print(f"max difference {diffs[0][0]:.4f}" if diffs else "nothing compared")
    for d, name, m, r in diffs[:8]:
        if d > 1e-3:
            print(f"  {name:32s} model {m:9.4f}  ergokeys {r:9.4f}  diff {d:.4f}")
    for b in badnames[:6]:
        print("  not parsed:", b)
    return 1 if off else 0


if __name__ == "__main__":
    sys.exit(main())
