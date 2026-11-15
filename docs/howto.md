# ergokeys: examples

Real output of the commands from the README, on the files in `tests/data/`.

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


## Other layouts

`build/ergokeys layout qwerty-ansi-full` shows the 104-key board with navigation block and numeric pad;
`score -l qwerty-ansi-full Ctrl+Page_Up KP_5` scores shortcuts that the 60% board skips. Names as the importers
meet them (`Page_Up`, `Prior`, `Next`, `Del`, `KP_7`, `KP_Add`) are accepted.
