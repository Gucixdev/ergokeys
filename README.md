# ergokeys

ergokeys measures how awkward keyboard shortcuts are to press. It simulates the fingers on a keyboard layout, adds up
how far each finger travels in millimetres, scores the shortcuts with a cost model whose weights you can change, and
proposes bindings that cost less. It reads shortcut lists in its own TSV format and the key bindings of Hyprland,
i3 and sway, and dwl. It is one C89 binary with no dependencies beyond libc. It only reads files and prints; it never
changes your configuration.

## Build and run

```sh
./run                      # builds with make, runs every test, prints an example
make                       # only the binary: build/ergokeys
build/ergokeys help        # all subcommands and options
```

```sh
build/ergokeys score Ctrl+c g "C-x C-s" Super+Shift+1     # shortcuts given on the command line
build/ergokeys simulate tests/data/sample.tsv             # cost and travel of every shortcut in a file
build/ergokeys simulate ~/.config/hypr/hyprland.conf      # the format is detected from the content
build/ergokeys suggest tests/data/i3.conf                 # propose cheaper bindings
build/ergokeys suggest FILE -o proposal.tsv               # write them out (refuses to overwrite)
build/ergokeys layout split-4x6                           # show a layout and its finger map
build/ergokeys config > my.conf                           # every weight with its meaning; edit, then use -c my.conf
```

Options: `-l LAYOUT` (name or path of a `.layout` file), `-c WEIGHTS`, `-f FORMAT`, `--seed N`, `--iters N`,
`--mods keep|any`, `--json`, `--strict`. Exit status: 0 done, 1 bad command line, 2 a file could not be read or is
invalid, 3 `--strict` found skipped lines or conflicts, 4 `suggest` found no conflict-free assignment.
`make install` copies the binary and the layouts to `$PREFIX` (default `/usr/local`).

## What it looks like

`score` on the command line (qwerty-ansi, left index finger home on F):

```
layout qwerty-ansi
      mm      cost  shortcut
   89.98    111.16  Ctrl+c
   19.05     19.05  g
  111.28    156.69  Ctrl+x Ctrl+s
  159.49    194.19  Super+Shift+1
```

G is one key away from F, so it costs exactly one key pitch, 19.05 mm. `mm` is the straight-line travel of all fingers;
`cost` adds the penalties and bonuses of the model.

`simulate` on a file with weights (how often each shortcut is used) and two bad lines, which are reported and skipped:

```
tests/data/sample.tsv:12: skipped: unknown key 'Ctrl+Nonsense'
tests/data/sample.tsv:13: skipped: no TAB between action and chord
layout qwerty-ansi, 10 shortcuts from tests/data/sample.tsv (tsv), 2 lines skipped

      mm      cost weight  shortcut                   action
   68.69     84.63     50  Ctrl+s                     save
   89.98    111.16     80  Ctrl+c                     copy
   89.98    111.16     80  Ctrl+v                     paste
   89.98    111.16     40  Ctrl+z                     undo
   95.84     95.42     30  Super+q                    close window
  114.30    108.30     60  Super+enter                terminal
  116.89    133.75     40  Super+1                    workspace 1
  116.89    133.75      5  Super+9                    workspace 9
  118.80    130.64     10  Super+Shift+s              screenshot
  111.28    156.69     20  Ctrl+x Ctrl+s              emacs save

total weighted by use: 39837.95 mm, cost 46282.88
total of each shortcut once: 1012.64 mm, cost 1176.66
```

followed by the ten worst shortcuts by weight times cost, and any conflicts.

`suggest` on the same file. Each action keeps its modifiers and gets the cheapest free key, heaviest use first:

```
layout qwerty-ansi, 10 shortcuts from tests/data/sample.tsv (tsv), modifiers kept, seed 1

before: cost 46282.88, 39837.95 mm
after:  cost 30273.64, 25393.89 mm  (34.6% lower)

9 of 10 actions change:
from                       to                               was       now  action
Ctrl+s                     Ctrl+;                         84.63     66.18  save
Ctrl+c                     Ctrl+j                        111.16     66.18  copy
Ctrl+v                     Ctrl+k                        111.16     66.18  paste
Ctrl+z                     Ctrl+l                        111.16     66.18  undo
Super+q                    Super+a                        95.42     70.20  close window
Super+enter                Super+s                       108.30     70.20  terminal
Super+1                    Super+d                       133.75     70.20  workspace 1
Super+9                    Super+f                       133.75     70.20  workspace 9
Super+Shift+s              Super+Shift+j                 130.64    111.73  screenshot
```

The sequence `Ctrl+x Ctrl+s` is not moved. Run on the sample configs in `tests/data/`, `suggest` lowers the weighted
cost by 37.7 percent for the Hyprland file (22 shortcuts, 1919.00 against 3081.43). The proposal ignores what an action
means: it will move "workspace 1" off the `1` key. Read it as a list of cheap chords, not as a patch.

## How it works

- Layouts are text files in `layouts/`: key positions in millimetres, which finger presses each key, and the finger
  home keys. Shipped: `qwerty-ansi`, `ortho-5x12`, `split-4x6`. Add one by writing a file; there is nothing to
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

Tested: the cost model against values worked out by hand (58 checks), the chord, layout, weights and importer
parsers on inline texts and on the sample configs in `tests/data/` (132 checks), and `suggest` properties on up to 500
shortcuts: never a conflict, never a higher cost, deterministic for a seed, pinned entries stay, no heap use, and equal
to the independently computed optimum (6880 checks). All of it runs under gcc and clang, plain and with address and
undefined-behaviour sanitizers; the parsers are also fuzzed (160000 checks per compiler in `./run`, 2.4 million in one
300000-input run, and 30 seconds of libFuzzer without a finding). The CLI has 95 checks for exit codes, `--help`, JSON
validity and determinism. `bench/RESULTS.md`: `suggest` on 500 shortcuts takes about 10 to 21 milliseconds.

Not done or not verified:

- The sample configs follow the stock files of Hyprland, i3 and dwl but were not exported from a running system, and
  the importers were not run against the user configs of other people. The dwl importer reads `config.h` text, it does
  not understand the C preprocessor beyond `#define MODKEY` and `TAGKEYS`.
- The qwerty-ansi layout has no navigation block (Home, End, Page Up, Delete and so on) and no numeric pad, so
  shortcuts that use those keys are skipped. The split layout's stagger is approximate.
- The finger model is simple: straight-line moves, one thumb row, no hand shifting. Real typists differ.
- `suggest` has no notion of mnemonics or key groups; sequences and `fixed` actions are never moved.
- Annealing does not improve on the greedy phase with the current cost model (`docs/DESIGN.md` says why).
- Lean 4 proofs were not written: no Lean toolchain is installed here.
- Not run on a real keyboard or compared with a typing study.

## License

MIT, see [LICENSE](LICENSE).

---

