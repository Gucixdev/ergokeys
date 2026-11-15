# Benchmark log

Every row is the median and minimum wall time of repeated runs of `suggest`; `cost in` and `cost out` are the
weighted totals before and after. Appended by `bench/run-bench`.

## 2026-11-15, commit 259530b (uncommitted changes in src)

machine: Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz, 12 cores; compiler: gcc (GCC) 16.2.1 20260810, -O2; 500 shortcuts, 9 runs per row

| case                           |    n | median ms |    min ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     10.50 |     10.50 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     12.18 |     12.17 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     16.20 |     16.19 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     13.02 |     13.01 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     15.29 |     15.28 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     20.97 |     20.94 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: 698 ns per call (200000 calls, checksum 53635319.0)

## 2026-11-15, commit 259530b (uncommitted changes in src)

machine: Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz, 12 cores; compiler: clang version 23.1.4, -O2; 500 shortcuts, 9 runs per row

| case                           |    n | median ms |    min ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     11.46 |     11.40 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     13.19 |     13.18 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     17.06 |     17.03 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     10.85 |     10.77 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     12.68 |     12.51 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     17.84 |     17.77 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: 718 ns per call (200000 calls, checksum 53635319.0)
