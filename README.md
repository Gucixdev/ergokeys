# ergokeys

ergokeys measures how awkward keyboard shortcuts are to press. It simulates the fingers on a keyboard layout, adds up
how far each finger travels in millimetres, scores the shortcuts with a cost model whose weights you can change, and
proposes bindings that cost less. It reads shortcut lists in its own TSV format and the key bindings of Hyprland,
i3 and sway, and dwl. It is one C89 binary with no dependencies beyond libc. It only reads files and prints; it never
changes your configuration.

## Build and run

```sh
./run                      # ./build, runs every test, prints an example
make                       # only the binary: out/ergokeys
out/ergokeys help        # all subcommands and options
```

```sh
out/ergokeys score Ctrl+c g "C-x C-s" Super+Shift+1     # shortcuts given on the command line
out/ergokeys simulate hyprland                          # the config of a program: hyprland, sway, i3 or dwl
out/ergokeys simulate                                   # no argument: the first of those found on this machine
out/ergokeys simulate tests/data/sample.tsv             # or any file: cost and travel of every shortcut in it
out/ergokeys simulate ~/.config/hypr/hyprland.conf      # the format is detected from the content
out/ergokeys suggest i3                                 # propose cheaper bindings (a program name or a file)
out/ergokeys suggest FILE -o proposal.tsv               # write them out (refuses to overwrite)
out/ergokeys layout split-4x6                           # show a layout and its finger map
out/ergokeys span a t                                   # mm between two keys: measure your hand (hand_span)
out/ergokeys score "C-x C-s" --hand-span 60 --finger-reach 30   # optional hand model; the default is general
out/ergokeys config > my.conf                           # every weight with its meaning; edit, then use -c my.conf
```

Options: `-l LAYOUT` (name or path of a `.layout` file), `-c WEIGHTS`, `-f FORMAT`, `--seed N`, `--iters N`,
`--mods keep|any`, `--json`, `--strict`. Exit status: 0 done, 1 bad command line, 2 a file could not be read or is
invalid, 3 `--strict` found skipped lines or conflicts, 4 `suggest` found no conflict-free assignment.
`./install` builds and installs for the user (`~/.local`; `PREFIX=/usr/local` with sudo for the system).
`./build` compiles what changed (`BUILD_FORCE=1 ./build` rebuilds everything); `./run` and `./install` call it, `./install -rebuild` starts from a full rebuild.

## AMPL model

`ampl/chord.mod` is the cost of one chord written from scratch in AMPL (A Mathematical Programming Language), from the physical description of the hand and not from `src/cost.c`: fingers at home, the cheapest modifier key per modifier chosen by the solver. `ampl/run [LAYOUT]` solves it with HiGHS for every chord of a layout and compares each result with `out/ergokeys score`; it needs `uv` (it builds its own venv with amplpy once). The free AMPL licence allows 2000 variables, so chords are solved 40 at a time. Result today: on the four shipped layouts every chord agrees to 0.001 mm except the chords where one finger has to press twice (cost over 188: unusable anyway); `ergokeys` starts the second press where the finger already is, the model at home. Details: [docs/DESIGN.md](docs/DESIGN.md#ampl-model).

## Examples

Output of `score`, `simulate` and `suggest` on the sample files: [docs/howto.md](docs/howto.md). For example, `suggest`
lowers the weighted cost of the sample Hyprland config by 37.7 percent, and it ignores what an action means: it will
move "workspace 1" off the `1` key. Read it as a list of cheap chords, not as a patch.

## How it works

- Layouts are text files in `layouts/`: key positions in millimetres, which finger presses each key, and the finger
  home keys. Shipped: `qwerty-ansi`, `qwerty-ansi-full` (with navigation block and numpad), `ortho-5x12`, `split-4x6`. Add one by writing a file; there is nothing to
  recompile.
- A shortcut is a chord (modifiers plus a key) or a sequence of up to four such steps (`C-x C-s`). Fingers start at
  home, walk to the modifier keys and then to the key, and stay where they are for the next step of a sequence.
  Travel is counted in millimetres. The cost adds weighted travel, penalties (pinky and ring finger on modifiers, a
  modifier held by the hand that also presses the key, a key far above the home row, the same finger twice in a row),
  and a bonus for alternating hands. Every weight is named and documented in `ergokeys config`.
- `suggest` resolves conflicts in the input, then moves actions to cheaper free chords by greedy swaps followed by
  simulated annealing with a fixed seed. It never produces two actions on one chord (or a chord that is the start of a
  sequence), and the result never costs more than the input. The same seed gives the same output on every machine.
- The importers skip what they cannot place (keycode and mouse bindings, keys missing from the layout, bindings in
  an i3 `mode` block) and list each skipped line with its reason on stderr.

Details and the reasons for each choice: [docs/DESIGN.md](docs/DESIGN.md).

## Principles

- C89, fixed-size types, one translation unit, no recursion, and no `malloc`: all memory is one static arena that the
  tests poison and check.
- Everything tunable is data: layouts are files, every weight is a config entry with its documentation next to it.
- The defaults of the cost model come from common typing advice, not from measurements. Treat the numbers as a
  consistent yardstick for comparing two bindings on one layout, not as physiology.
- Tolerant input, loud about it: skipped lines are never silent, and `--strict` makes them an error.

## Status

Tested under gcc and clang, plain and with address and undefined-behaviour sanitizers: the cost model against values
worked out by hand (58 checks), the parsers (146), `suggest` properties on up to 500 shortcuts (6880: no conflict,
never a higher cost, deterministic, no heap use, equal to the independently computed optimum), the CLI (101), and fuzzing
of the parsers (160000 checks in `./run`). Benchmarks: [docs/bench.md](docs/bench.md) (`bench/run-bench`), `suggest` on 500 shortcuts
takes 8 to 25 ms, and its default annealing adds nothing on the shipped inputs (see Findings there).

Not done or not verified:

- The sample configs follow the stock Hyprland, i3 and dwl files; they were not exported from a running system. The dwl
  importer reads `config.h` text and understands only `#define MODKEY` and `TAGKEYS`.
- The finger model is simple: straight-line moves, one thumb row, no hand shifting. Hand size is two weights,
  `hand_span` and `finger_reach`, off until you measure them with `ergokeys span` ([docs/DESIGN.md](docs/DESIGN.md)
  says how). Weights like `pen_key_pinky` (8) come from common typing advice, not measurements. Not run on a real
  keyboard.
- `suggest` has no notion of mnemonics; sequences and `fixed` actions are never moved, and annealing does not beat the
  greedy phase with the current cost model ([docs/DESIGN.md](docs/DESIGN.md) says why).
- Lean 4 proofs ([lean4/Ergokeys.lean](lean4/Ergokeys.lean), run by `./run`) cover the bookkeeping of `suggest`: the
  relocation and the swap, with the guards of `opt.c`, keep one action per chord, and the cost change the search
  adds up equals the change of the total (exact integers; the C code adds f64). Not proved: the cost model itself
  (floats and a square root), the conflict resolution at the start, and that the C code matches the model: that link
  is only the review and the tests above.

## License

MIT, see [LICENSE](LICENSE).

---

