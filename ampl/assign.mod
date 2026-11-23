# assign.mod - the best keys for a set of shortcuts: ergokeys' `suggest` with modifiers kept, as an exact assignment problem.
# Each movable action keeps its modifier set and takes one key; no two actions share a chord; chords of fixed actions are reserved.
# The cost of a chord comes from chord.mod. Here only the choice is modelled, so a term that couples actions (two shortcuts on
# neighbouring keys, a mnemonic) is one more line in the objective, not a new search algorithm.

set ACTIONS;
set CHORDS;                                          # candidate chord ids
param cost {CHORDS};                                 # one chord, from chord.mod
param weight {ACTIONS};
set OK {ACTIONS} within CHORDS;                      # chords an action may take: its modifier set, a key not reserved

var put {a in ACTIONS, c in OK[a]} binary;
s.t. each {a in ACTIONS}: sum {c in OK[a]} put[a, c] = 1;
s.t. once {c in CHORDS}: sum {a in ACTIONS: c in OK[a]} put[a, c] <= 1;

minimize total: sum {a in ACTIONS, c in OK[a]} weight[a] * cost[c] * put[a, c];
