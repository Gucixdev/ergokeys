# Design decisions

One entry per decision: what was chosen, why, and what it costs. Numbers quoted here come from real runs
(`./run`, `bench/RESULTS.md`).

## Scope

ergokeys answers three questions about a set of keyboard shortcuts: how far do the fingers travel, which shortcuts
are the most awkward, and which cheaper binding could each action have. It is a command line tool with one binary and
no dependencies beyond libc. It does not read key events, does not change any configuration, and does not know what
an action means.

## Language and build

- C89, compiled with `-std=c89 -pedantic -Wall -Wextra -Werror`, under gcc and clang. The only non-standard
  constructs are the fixed-width types, taken from the compiler's `__UINT8_TYPE__` family and pinned by compile-time
  size asserts in `src/base.h`.
- One translation unit: `src/main.c` includes `src/ek.c`, which includes the other files. The tests include the same
  `ek.c`, so they call the real functions, including the static ones.
- Make is used because it was asked for. The `run` script wraps it: it builds, then runs every test.
- No libm. Square root and exp(-x) are written out in `src/base.h` (`ek_sqrt`, `ek_expneg`). Both are checked against
  libm in `tests/test_model.c` (relative error below 1e-14 and 1e-10). The reason is reproducibility: a fixed seed
  then gives the same suggestion on every IEEE-754 host. `tests/run-tests` compares the gcc and clang binaries on all
  sample configs, and `-ffp-contract=off` is part of the build flags so that fused multiply-add cannot change a
  result.

## Memory

- There is no `malloc` in `src/`. One static array (`EK_ARENA_BYTES`, 64 MiB by default) backs an arena with a bump
  pointer. The input text, layout, entry arrays, conflict lists and the optimizer's tables all live in it.
- Every allocation is zeroed, 8-byte aligned and followed by 8 bytes that are poisoned when built with
  `-DEK_ASAN`, so AddressSanitizer reports an overflow that stays inside the arena. `arena_reset` poisons what it
  frees, which catches use after reset.
- A full arena is an ordinary error path (`suggest` returns -1, the CLI exits 2 with a message), tested with a 512
  byte arena in `tests/test_opt.c`.
- Checks that the search allocates nothing: `tests/test_opt.c` compares `mallinfo2` before and after `suggest` on 500
  shortcuts, and `tests/run-tests` runs `nm -u` on the object file of the whole program and fails if it references
  `malloc`, `calloc`, `realloc`, `free`, `mmap` or `brk`. (libc's stdio may allocate inside `fopen`; our code never
  asks.)
- No recursion anywhere. Loops are bounded by fixed array sizes: 128 keys, 4 steps per shortcut, 200 stored skip
  records, 256 stored conflicts.

## Layouts are data

`layouts/*.layout` are text files, one directive per line: `name`, `pitch`, `row Y X0 LABELS...`, `key LABEL X Y W
[nochord]`, `finger FINGER LABELS...`, `home FINGER LABEL [X]`, `mod ctrl|alt|super|shift LABELS...`. Positions are in
key units (1 unit = pitch = 19.05 mm) with y downward. A parse error names the line and the reason. Search order for
`layout NAME`: `-L DIR`, `$ERGOKEYS_LAYOUTS`, `./layouts`, the install directory; a name containing a slash or ending
in `.layout` is read as a path.

Four layouts ship: `qwerty-ansi` (60% board with function row and arrows), `qwerty-ansi-full` (the same main block plus the navigation block, arrows as an inverted T and the numeric pad, 104 keys), `ortho-5x12` (a 5 by 12 ortholinear
grid), `split-4x6` (two halves of 4 rows by 6 columns, column stagger, 3 thumb keys per half). The finger assignment
and the home keys are explicit for every key, not derived, so a layout with an unusual fingering is just a different
file. The stagger values of the split layout are approximate; edit the y values to match a real board.

Not in the layouts: the navigation block and numeric pad on `qwerty-ansi` (use `qwerty-ansi-full`), print/scroll/pause
anywhere, and anything that sits on a layer on the compact boards. The model has no hand shifting, so a reach to the
nav block or the pad is a straight line from where the finger stands. A shortcut that uses such a key is skipped on import and reported, never guessed.

Wide keys: a finger presses a wide key (space, shift, enter) at the point of the key nearest to where the finger
already is (`layout_land_x`), not at its centre.

## Shortcuts: chords and sequences

A shortcut is up to 4 steps. A step is a modifier set plus one key. A chord has one step; Emacs style `C-x C-s` has two.
Parsing accepts `+` and `-` separators, the modifier words of i3, sway, Hyprland and dwl (Ctrl, Control, Alt, Mod1,
Super, Mod4, Win, Shift, ...), the Emacs prefixes `C- M- s- S-`, X11 key names (`Return`, `comma`, `exclam`), and
shifted symbols, which imply Shift (`Ctrl++` is Ctrl+Shift+=). Two shortcuts conflict when they are equal or when one
is the start of the other (`C-x` and `C-x C-s`).

## Cost model

All weights are in one table in `src/cost.c` (name, default, documentation). `ergokeys config` prints it as a config
file, `config/default.conf` is that output (a test keeps them equal), and `-c FILE` overrides any value by name.
Units: millimetres of weighted finger travel plus penalties.

Simulation of one step:

1. Every finger starts at its home point. Fingers keep their position between the steps of a sequence.
2. Each modifier of the step is pressed by a finger that walks to a modifier key. When the layout offers two keys for
   a modifier (left and right Ctrl) every combination is tried and the cheapest is kept, ties to the first.
3. The key is pressed by its own finger, which walks from where it is.
4. Travel is `sqrt(dx^2 + (vertical_factor*dy)^2)` times `travel`: reaching up or down is slower than sideways, so
   dy is scaled (default 1.3). The unweighted straight-line distance is tracked separately and is what the `mm`
   column shows.

Terms added to the travel cost (defaults in parentheses):

| term | meaning |
| --- | --- |
| `pen_mod_pinky` (15), `pen_mod_ring` (5) | a pinky or ring finger that has to move to a modifier |
| `pen_one_hand` (20) | per modifier held by a non-thumb finger of the same hand that presses the key |
| `pen_far_row` (12) | per key row above the finger's home row beyond the first |
| `pen_same_finger` (25), `pen_repeat_key` (5) | a sequence step on the finger of the previous step (another key / the same key) |
| `pen_finger_conflict` (200) | two keys of one step on one finger |
| `bonus_alternate` (6, subtracted) | all modifiers on the other hand than the key; a step whose key is on the other hand than the previous step's key |
| `return_trip` (0) | 1 adds the walk of every moved finger back home after the last step |

The cost of a whole shortcut is clamped at zero.

The defaults are a starting point from common typing advice (pinkies are weak, same-finger repeats are slow, hand
alternation is fast), not values fitted to measurements. That is why every one of them is a named, documented config
entry. `tests/test_model.c` checks the model against values worked out by hand (adjacent keys 19.05 mm, the distance
to R and to B from the index home, G-then-H, G-then-G, each penalty on its own with the others at zero).

Known simplifications: a finger walks in a straight line, with no lift and no reach limit; the hand does not shift;
the thumb row is one row; the cost of a key does not depend on what was pressed before the shortcut. All of these are
visible in `src/cost.c` and can change without touching the rest.

What the model knows about the hand, and what it does not (checked 2026-11-22 by running it):

- Fingers are ten independent points, each walking a straight line from its home key. The only per-finger facts are
  which keys it owns and where it rests; hand size is not an input. `pitch` and `travel` scale every distance alike,
  and that moves almost nothing: the order of the keys for a given modifier set, which is all `suggest` uses, stays at
  Spearman 0.996 to 1.000 when `travel` goes from 0.7 to 1.5 (about 0.86 with every penalty at zero).
- What a real hand size would change is the reach between two fingers of one hand (Ctrl on the pinky and a key two
  rows up on the ring finger) and how far one finger can go without the hand shifting. Neither is in the model.
- Every key a finger presses costs the same wherever that finger rests, so `Super+a` (pinky) equals `Super+f` (index)
  and `q`, `w`, `e`, `r` tie. "Pinkies are weak" held only for modifiers. `pen_key_pinky` and `pen_key_ring` charge
  the pressed key; they default to 0 so older numbers stay, and a value around 8 and 3 is what that advice suggests.
- A modifier is chosen per step, the cheapest, so `Ctrl+c` is priced with the right Ctrl on a layout that has both.
  That is the best case, not what a hand that always uses the left Ctrl pays: give such a layout one `mod ctrl` key.
- Checked, not assumed: over 720 chords (every modifier set times 45 keys) adding a modifier never lowers the cost or
  the travel, and no chord with a modifier is clamped to zero.

## Importers

Four formats, detected from the content unless `--format` is given: own TSV (`action TAB chord [TAB weight [TAB
fixed]]`), Hyprland `bind = MODS, KEY, dispatcher, args` (with `$variables`, `bind` flags, `_` or space between
modifiers), i3 and sway `bindsym [--flags] CHORD command` (with `set $var`), and dwl `config.h` (`#define MODKEY`,
`{ MOD|MOD, XKB_KEY_x, fn, arg }`, and `TAGKEYS(...)` expanded to its four rows).

They are tolerant on purpose. A line that looks like a binding but cannot be used (a keycode or mouse binding, a key
that is not in the layout, an unknown modifier, a binding inside an i3 `mode` block, which is a different key table)
is skipped, counted and listed on stderr with its line number and reason. `--strict` turns skipped lines and
conflicts into exit status 3. Nothing is guessed to make a line fit. Input with CRLF line endings works.

## suggest

What moves: every single-step action that is not marked `fixed`. By default (`--mods keep`) its modifier set stays and
only the key changes, because the modifier usually carries the meaning (Super for window management). With `--mods
any` it may also take any modifier set that appears in the input. Sequences and `fixed` actions stay where they are,
and their first step is reserved so nothing moves onto it. The set of actions never changes.

Because a single chord costs the same wherever it appears (fingers start at home), the cost of every (modifier set,
key) pair is computed once into a table of 16 x keys entries. The search then only adds and subtracts table entries.

Phases: resolve conflicts in the input (an action whose chord is taken goes to the cheapest free chord), greedy
passes (best relocation or swap per action while the total drops), simulated annealing with a fixed seed (cubic
cooling schedule, Metropolis rule, the exp written out in `base.h`), greedy again. The best state seen is kept, and a
final guard returns the starting assignment if rounding ever made the result larger, so a result never costs more than
what it started from. With `--seed N` the same seed gives the same output; the seed is also a config key. A greedy
move that does not lower the total is counted in `greedy_bad` and the tests require 0.

Finding recorded after measuring: with modifiers kept, the cost of an action is its weight times the cost of its
key, so the best assignment gives the cheapest keys to the heaviest weights (rearrangement inequality), and the
greedy phase already reaches exactly that optimum. `tests/test_opt.c` computes the optimum independently by sorting
and checks equality on 30, 120 and 400 shortcuts, in both modifier modes. The benchmark agrees: annealing never
improved on greedy (`bench/RESULTS.md`). The annealer stays because the interface asks for it and because it is the
part that keeps working if a term that couples actions (for example "keep these two shortcuts on neighbouring keys")
is added to the objective; with the current objective it is insurance, and `--iters 0` skips it.

What suggest does not know: what an action means. It will happily move "workspace 1" off the `1` key because the
cost model has no notion of a mnemonic or of a group that should stay together. The output is a proposal to read, not a
patch to apply, and `--out` writes a TSV, never a config in the original format.

## Exit codes

| code | meaning |
| --- | --- |
| 0 | done |
| 1 | bad command line |
| 2 | a file could not be read, or a layout, config or output file is invalid, or the arena is full |
| 3 | `--strict`: lines were skipped or shortcuts conflict |
| 4 | `suggest` found no conflict-free assignment (more actions than free chords) |

`--help` prints the text between `HELP-BEGIN` and `HELP-END` in the header of `src/main.c`; the Makefile turns it into
`out/help.inc`, and a test compares the binary's output with the header. `-o FILE` refuses to overwrite.

## Tests and verification

- `tests/test_model.c`, `tests/test_parse.c`, `tests/test_opt.c`: unit and property tests, each built with gcc and
  clang, once plain at -O2 and once with `-fsanitize=address,undefined`.
- `tests/fuzz_parse.c`: random bytes, word salad built from the formats' keywords, and mutated and truncated copies of
  the real sample files go to the layout, weights, chord and all four importers. Inputs are exact-size heap copies so
  an over-read is visible. A libFuzzer entry point exists (`-DEK_LIBFUZZER`) and `tests/run-tests fuzz` runs it for 30
  seconds when clang supports it.
- `tests/cli-tests`: the binary as a user runs it: exit codes, `--help` against the source header, JSON validity of
  every subcommand with `python3 -m json.tool`, determinism, `--out` round trip (the cost `suggest` prints for the
  proposed bindings equals the cost `simulate` prints for the file it wrote).
- `tests/data/` holds real-shaped sample configs for each importer, taken from the stock configs of those programs.
- `bench/bench.c` and `bench/run-bench`: median, minimum and maximum of repeated runs after a warm-up, appended to `docs/bench.md` (`bench/RESULTS.md` is the older log).

Lean 4: `lean4/Ergokeys.lean` proves the bookkeeping of `suggest` (relocation and swap keep one action per chord, the
searched cost change equals the change of the total), run by `./run`. The model itself is examined over the reals in
the devframework test templates (`ergokeys/Cost.lean`: the weighted distance is a metric, it lies between the plain
distance and `vertical_factor` times it, the cheapest modifier choice is monotone in every weight). Not proved: the
f64 rounding of the C code and the penalty rules, which `tests/test_model.c` checks by hand; and a proof says the model
is consistent, not that it matches a hand.
