# ergokeys benchmarks

Appended by `bench/run-bench`. The older log of `make bench` is bench/RESULTS.md. Findings first, then the log.

## Findings

1. **The default annealing costs time and buys nothing here.** In all 24 in-process rows (4 layouts, normal and any-modifier
   modes) the annealing rows end at exactly the same cost as the greedy rows, and the real inputs agree: for `sample.tsv`,
   the Hyprland, i3 and dwl samples the `after:` line is identical with `--iters 0`. Meanwhile `suggest` on a real config
   (12 to 59 shortcuts) takes 8 to 10 ms with the default 200000 annealing steps and 3 ms with `--iters 0`, so about 70 percent of
   the process time is annealing; on 500 shortcuts it adds 2 to 7 ms to 8 to 15 ms. The README and DESIGN.md already say
   annealing does not beat greedy with the current cost model. Not changed: the default is a documented design
   decision (the interface asks for annealing and a future cost model may need it), so this is the thing to decide, not a bug.
2. Everything is fast enough that speed is not a problem: `simulate` is 1.0 to 1.2 ms end to end of which 0.85 ms is starting the
   process (`--version`), so parsing a layout and a 12 to 59 line config is about 0.2 to 0.3 ms.
3. `shortcut_cost`, the hot path (called once per shortcut per candidate move), takes about 370 to 400 ns on `ortho-5x12` and `split-4x6`
   but 780 to 830 ns on `qwerty-ansi` and `qwerty-ansi-full`, twice as long. UNSURE why; the stagger of the qwerty layouts and
   the number of keys searched are the first suspects, not profiled (no perf here).
4. gcc and clang are within about 20 percent of each other everywhere; the order of rows does not change.

How sure: a loaded machine (load average about 14 on 12 threads while this ran, the owner's compiles and editors), so absolute
times are upper bounds; the spread columns show the noise, which is small (a few percent) because every row is 11 runs after a
warm-up. The earlier log `bench/RESULTS.md` (one layout, 9 runs, no warm-up) agrees within 15 percent. The quality claim in (1)
is an exact comparison of printed costs, not a timing.

## Results log

## 2026-11-15, commit 8e2911f

machine: Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz, 12 threads, Linux 7.3.3-1-cachyos, shared with the owner's editors
load average at start: 14.52 10.32 6.41
command: `bench/run-bench`; in-process build `cc -std=gnu99 -O2 -ffp-contract=off bench/bench.c -lm`, program build as in the Makefile (`-std=c89 -O2`)

### In process, gcc (GCC) 16.2.1 20260810, 500 synthetic shortcuts (fixed seed), 11 runs per row, ms

`layouts/ortho-5x12.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |      8.62 |      8.53 |      8.72 | 3218514.5 | 2440088.2 |   24.2% |
| greedy + anneal 20000          |  500 |     10.62 |     10.40 |     12.06 | 3218514.5 | 2440088.2 |   24.2% |
| greedy + anneal 200000         |  500 |     14.47 |     14.34 |     14.89 | 3218514.5 | 2440088.2 |   24.2% |
| any modifiers, greedy only     |  500 |     10.60 |     10.30 |     11.11 | 3218514.5 | 1359469.7 |   57.8% |
| any modifiers, anneal 20000    |  500 |     12.99 |     12.59 |     14.86 | 3218514.5 | 1359469.7 |   57.8% |
| any modifiers, anneal 200000   |  500 |     18.96 |     18.30 |     19.22 | 3218514.5 | 1359469.7 |   57.8% |

shortcut_cost, Ctrl+Shift+key: median 387 ns per call (min 383, max 392; 8 runs of 200000 calls, checksum 424488659.3)

`layouts/qwerty-ansi-full.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     13.01 |     12.98 |     14.00 | 3117202.4 | 1982372.4 |   36.4% |
| greedy + anneal 20000          |  500 |     15.12 |     15.04 |     15.40 | 3117202.4 | 1982372.4 |   36.4% |
| greedy + anneal 200000         |  500 |     20.04 |     19.75 |     20.90 | 3117202.4 | 1982372.4 |   36.4% |
| any modifiers, greedy only     |  500 |     17.58 |     17.21 |     18.21 | 3117202.4 | 1355580.5 |   56.5% |
| any modifiers, anneal 20000    |  500 |     20.50 |     20.07 |     21.67 | 3117202.4 | 1355580.5 |   56.5% |
| any modifiers, anneal 200000   |  500 |     27.06 |     26.91 |     27.87 | 3117202.4 | 1355580.5 |   56.5% |

shortcut_cost, Ctrl+Shift+key: median 809 ns per call (min 802, max 835; 8 runs of 200000 calls, checksum 528715871.8)

`layouts/qwerty-ansi.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     12.03 |     11.97 |     12.10 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     13.90 |     13.86 |     14.05 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     18.59 |     18.50 |     19.18 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     14.85 |     14.78 |     15.34 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     17.67 |     17.33 |     18.03 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     24.62 |     23.92 |     34.06 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: median 775 ns per call (min 760, max 789; 8 runs of 200000 calls, checksum 482717870.8)

`layouts/split-4x6.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |      9.22 |      8.96 |     13.17 | 1703079.3 | 1442480.0 |   15.3% |
| greedy + anneal 20000          |  500 |     11.16 |     10.87 |     11.34 | 1703079.3 | 1442480.0 |   15.3% |
| greedy + anneal 200000         |  500 |     14.99 |     14.85 |     15.23 | 1703079.3 | 1442480.0 |   15.3% |
| any modifiers, greedy only     |  500 |     12.16 |     11.86 |     12.37 | 1703079.3 |  651115.8 |   61.8% |
| any modifiers, anneal 20000    |  500 |     14.61 |     14.54 |     14.67 | 1703079.3 |  651115.8 |   61.8% |
| any modifiers, anneal 200000   |  500 |     20.69 |     20.53 |     21.23 | 1703079.3 |  651115.8 |   61.8% |

shortcut_cost, Ctrl+Shift+key: median 369 ns per call (min 365, max 379; 8 runs of 200000 calls, checksum 184247340.3)

### In process, clang version 23.1.4, 500 synthetic shortcuts (fixed seed), 11 runs per row, ms

`layouts/ortho-5x12.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |      9.27 |      9.26 |      9.33 | 3218514.5 | 2440088.2 |   24.2% |
| greedy + anneal 20000          |  500 |     11.25 |     11.23 |     11.31 | 3218514.5 | 2440088.2 |   24.2% |
| greedy + anneal 200000         |  500 |     15.04 |     15.01 |     15.35 | 3218514.5 | 2440088.2 |   24.2% |
| any modifiers, greedy only     |  500 |      8.69 |      8.54 |      9.00 | 3218514.5 | 1359469.7 |   57.8% |
| any modifiers, anneal 20000    |  500 |     10.42 |     10.20 |     10.62 | 3218514.5 | 1359469.7 |   57.8% |
| any modifiers, anneal 200000   |  500 |     15.60 |     15.51 |     15.89 | 3218514.5 | 1359469.7 |   57.8% |

shortcut_cost, Ctrl+Shift+key: median 403 ns per call (min 396, max 408; 8 runs of 200000 calls, checksum 424488659.3)

`layouts/qwerty-ansi-full.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     14.13 |     14.06 |     14.30 | 3117202.4 | 1982372.4 |   36.4% |
| greedy + anneal 20000          |  500 |     16.00 |     15.76 |     16.43 | 3117202.4 | 1982372.4 |   36.4% |
| greedy + anneal 200000         |  500 |     20.58 |     20.53 |     22.92 | 3117202.4 | 1982372.4 |   36.4% |
| any modifiers, greedy only     |  500 |     14.08 |     13.70 |     14.50 | 3117202.4 | 1355580.5 |   56.5% |
| any modifiers, anneal 20000    |  500 |     16.51 |     16.01 |     18.74 | 3117202.4 | 1355580.5 |   56.5% |
| any modifiers, anneal 200000   |  500 |     22.32 |     21.84 |     22.70 | 3117202.4 | 1355580.5 |   56.5% |

shortcut_cost, Ctrl+Shift+key: median 830 ns per call (min 823, max 864; 8 runs of 200000 calls, checksum 528715871.8)

`layouts/qwerty-ansi.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     12.99 |     12.83 |     13.70 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     15.04 |     14.94 |     16.94 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     19.31 |     19.24 |     20.03 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     12.44 |     12.29 |     14.84 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     14.64 |     14.35 |     14.94 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     20.33 |     20.17 |     20.86 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: median 822 ns per call (min 795, max 890; 8 runs of 200000 calls, checksum 482717870.8)

`layouts/split-4x6.layout`

| case                           |    n | median ms |    min ms |    max ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |      9.91 |      9.67 |     10.83 | 1703079.3 | 1442480.0 |   15.3% |
| greedy + anneal 20000          |  500 |     11.69 |     11.40 |     12.12 | 1703079.3 | 1442480.0 |   15.3% |
| greedy + anneal 200000         |  500 |     15.24 |     15.09 |     16.04 | 1703079.3 | 1442480.0 |   15.3% |
| any modifiers, greedy only     |  500 |     10.13 |      9.87 |     10.77 | 1703079.3 |  651115.8 |   61.8% |
| any modifiers, anneal 20000    |  500 |     12.12 |     11.95 |     12.60 | 1703079.3 |  651115.8 |   61.8% |
| any modifiers, anneal 200000   |  500 |     17.23 |     17.01 |     17.70 | 1703079.3 |  651115.8 |   61.8% |

shortcut_cost, Ctrl+Shift+key: median 385 ns per call (min 372, max 416; 8 runs of 200000 calls, checksum 184247340.3)

### End to end: `build/ergokeys` as a process (start, parse the layout and the input, compute, print), 25 runs, ms

| command | median | min | max |
|---|---|---|---|
| `ergokeys --version` | 0.85 | 0.81 | 0.95 |
| `ergokeys simulate tests/data/sample.tsv -l ortho-5x12 -L layouts` | 1.02 | 0.92 | 1.32 |
| `ergokeys simulate tests/data/sample.tsv -l qwerty-ansi-full -L layouts` | 1.13 | 1.03 | 1.24 |
| `ergokeys simulate tests/data/sample.tsv -l qwerty-ansi -L layouts` | 1.04 | 1.00 | 1.13 |
| `ergokeys simulate tests/data/sample.tsv -l split-4x6 -L layouts` | 1.06 | 0.97 | 1.18 |
| `ergokeys suggest tests/data/sample.tsv -l ortho-5x12 -L layouts` | 8.06 | 7.83 | 8.34 |
| `ergokeys suggest tests/data/sample.tsv -l qwerty-ansi-full -L layouts` | 10.32 | 10.00 | 10.70 |
| `ergokeys suggest tests/data/sample.tsv -l qwerty-ansi -L layouts` | 10.00 | 9.69 | 11.51 |
| `ergokeys suggest tests/data/sample.tsv -l split-4x6 -L layouts` | 8.82 | 8.56 | 9.82 |
| `ergokeys simulate tests/data/hyprland.conf -L layouts` | 1.27 | 1.24 | 1.41 |
| `ergokeys simulate tests/data/i3.conf -L layouts` | 1.14 | 1.03 | 1.38 |
| `ergokeys simulate tests/data/dwl-config.h -L layouts` | 1.33 | 1.29 | 1.44 |
| `ergokeys suggest tests/data/hyprland.conf -L layouts` | 8.45 | 8.26 | 8.81 |
| `ergokeys suggest tests/data/hyprland.conf --iters 0 -L layouts` | 2.73 | 2.64 | 3.32 |
| `ergokeys suggest tests/data/sample.tsv --iters 0 -L layouts` | 2.68 | 2.63 | 2.98 |

Quality next to speed, `after:` line of `suggest` with the default 200000 annealing steps and with `--iters 0` (greedy only):

- `tests/data/sample.tsv`: default `after:  cost 30273.64, 25393.89 mm  (34.6% lower)`, greedy `after:  cost 30273.64, 25393.89 mm  (34.6% lower)`: identical
- `tests/data/hyprland.conf`: default `after:  cost 1919.00, 1936.56 mm  (37.7% lower)`, greedy `after:  cost 1919.00, 1936.56 mm  (37.7% lower)`: identical
- `tests/data/i3.conf`: default `after:  cost 2925.76, 2775.21 mm  (21.5% lower)`, greedy `after:  cost 2925.76, 2775.21 mm  (21.5% lower)`: identical
- `tests/data/dwl-config.h`: default `after:  cost 2736.50, 2449.42 mm  (32.9% lower)`, greedy `after:  cost 2736.50, 2449.42 mm  (32.9% lower)`: identical

sample.tsv has 12 shortcuts; hyprland.conf 59 lines, i3.conf 45, dwl-config.h 47.
load average at end: 14.99 10.69 6.61
