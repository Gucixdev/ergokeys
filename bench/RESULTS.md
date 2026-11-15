# Benchmark log

Every row is the median and minimum wall time of repeated runs of `suggest`; `cost in` and `cost out` are the
weighted totals before and after. Appended by `bench/run-bench`.

## 2026-11-15, commit c8a9bb0

machine: Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz, 12 cores; compiler: gcc (GCC) 16.2.1 20260810, -O2; 500 shortcuts, 9 runs per row

| case                           |    n | median ms |    min ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     10.48 |     10.46 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     12.18 |     12.17 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     16.18 |     16.14 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     13.05 |     13.00 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     15.27 |     15.25 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     21.00 |     20.79 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: 705 ns per call (200000 calls, checksum 53635319.0)

## 2026-11-15, commit c8a9bb0

machine: Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz, 12 cores; compiler: clang version 23.1.4, -O2; 500 shortcuts, 9 runs per row

| case                           |    n | median ms |    min ms |   cost in |  cost out |   lower |
|--------------------------------|------|-----------|-----------|-----------|-----------|---------|
| greedy only                    |  500 |     11.42 |     11.40 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 20000          |  500 |     13.20 |     13.18 | 2734068.9 | 2008531.2 |   26.5% |
| greedy + anneal 200000         |  500 |     17.08 |     17.03 | 2734068.9 | 2008531.2 |   26.5% |
| any modifiers, greedy only     |  500 |     10.84 |     10.80 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 20000    |  500 |     12.69 |     12.51 | 2734068.9 | 1358389.6 |   50.3% |
| any modifiers, anneal 200000   |  500 |     17.81 |     17.68 | 2734068.9 | 1358389.6 |   50.3% |

shortcut_cost, Ctrl+Shift+key: 713 ns per call (200000 calls, checksum 53635319.0)
