# chord.mod - what one keyboard chord costs the hand, written in AMPL (A Mathematical Programming Language) from the physical description.
# A chord is a set of modifiers plus one key. Fingers start at their home points. The presses happen in a fixed order: the held modifiers
# (ctrl, alt, super, shift), then the key. A finger that presses twice starts its second press where its first one left it. If the layout
# has several keys for a modifier (left and right Ctrl) every choice is a PATH; the solver picks the cheapest path of each chord, so the
# only decision in this file is that choice. Units: millimetres of weighted finger travel plus penalties. Weights have the names of ergokeys' config.

set FINGERS;                                   # LP LR LM LI LT RT RI RM RR RP
set KEYS;                                      # key labels of the layout
set SLOTS ordered = 1..5;                      # 1..4 the modifiers in order ctrl alt super shift, 5 the key
set ALLCHORDS;
set ALLPATHS;                                  # one path = one chord + one physical key for each held modifier
set CHORDS within ALLCHORDS;                   # priced now: chords do not depend on each other, so they can be solved in batches
set PATHS within ALLPATHS;                     # the paths of those chords

param chord {ALLPATHS} symbolic in ALLCHORDS;
param kk {ALLPATHS, SLOTS} symbolic in KEYS, default 'none';      # the key pressed in slot s ('none' = slot not used)

param pitch;
param x {KEYS};  param y {KEYS};               # key centre, mm
param xlo {KEYS}; param xhi {KEYS};            # leftmost / rightmost point a finger may land on
param fing {KEYS} symbolic in FINGERS;
param hx {FINGERS}; param hy {FINGERS};        # home point of each finger, mm
param hand {FINGERS} in {0, 1};
param pinky {FINGERS} in {0, 1};
param ring {FINGERS} in {0, 1};
param thumb {FINGERS} in {0, 1};

param travel; param vertical_factor; param pen_mod_pinky; param pen_mod_ring; param pen_one_hand; param pen_far_row;
param pen_finger_conflict; param bonus_alternate; param pen_key_pinky; param pen_key_ring;

param used {p in PATHS, s in SLOTS} = if kk[p, s] = 'none' then 0 else 1;
param finger {p in PATHS, s in SLOTS: used[p, s] = 1} symbolic in FINGERS = fing[kk[p, s]];

# where every finger stands BEFORE slot s: home at the start, afterwards the last landing of that finger
param xb {p in PATHS, s in SLOTS, f in FINGERS} =
  if s = first(SLOTS) then hx[f]
  else if used[p, prev(s)] = 1 and finger[p, prev(s)] = f then max(xlo[kk[p, prev(s)]], min(xhi[kk[p, prev(s)]], xb[p, prev(s), f]))
  else xb[p, prev(s), f];
param yb {p in PATHS, s in SLOTS, f in FINGERS} =
  if s = first(SLOTS) then hy[f]
  else if used[p, prev(s)] = 1 and finger[p, prev(s)] = f then y[kk[p, prev(s)]]
  else yb[p, prev(s), f];
# how many earlier presses of the same finger
param before {p in PATHS, s in SLOTS: used[p, s] = 1} = sum {t in SLOTS: ord(t) < ord(s) and used[p, t] = 1 and finger[p, t] = finger[p, s]} 1;

# the press in slot s: where it lands, how far the finger walks
param tx {p in PATHS, s in SLOTS: used[p, s] = 1} = max(xlo[kk[p, s]], min(xhi[kk[p, s]], xb[p, s, finger[p, s]]));
param dx {p in PATHS, s in SLOTS: used[p, s] = 1} = tx[p, s] - xb[p, s, finger[p, s]];
param dy {p in PATHS, s in SLOTS: used[p, s] = 1} = y[kk[p, s]] - yb[p, s, finger[p, s]];
param walk {p in PATHS, s in SLOTS: used[p, s] = 1} = travel * sqrt(dx[p, s]^2 + (vertical_factor * dy[p, s])^2);
param moved {p in PATHS, s in SLOTS: used[p, s] = 1} = if sqrt(dx[p, s]^2 + dy[p, s]^2) > 1e-9 then 1 else 0;

param keyfinger {p in PATHS} symbolic in FINGERS = finger[p, 5];
param up {p in PATHS} = (hy[keyfinger[p]] - y[kk[p, 5]]) / pitch;           # rows above the key finger's home row
param nmods {p in PATHS} = sum {s in 1..4} used[p, s];
param samehand {p in PATHS} = sum {s in 1..4: used[p, s] = 1 and hand[finger[p, s]] = hand[keyfinger[p]]} 1;
param allcross {p in PATHS} = if nmods[p] > 0 and samehand[p] = 0 then 1 else 0;

# a held modifier: walk + strain of a pinky or ring that had to move + a second press of the finger + the same-hand penalty (not for the thumb)
param modcost {p in PATHS, s in 1..4: used[p, s] = 1} = walk[p, s]
  + moved[p, s] * (pinky[finger[p, s]] * pen_mod_pinky + ring[finger[p, s]] * pen_mod_ring)
  + (if before[p, s] > 0 then pen_finger_conflict else 0)
  + (if hand[finger[p, s]] = hand[keyfinger[p]] and thumb[finger[p, s]] = 0 then pen_one_hand else 0);
# the key: walk + weak-finger charge + reaching above the home row beyond one row + a second press - the all-cross bonus
param keycost {p in PATHS} = walk[p, 5] + pinky[keyfinger[p]] * pen_key_pinky + ring[keyfinger[p]] * pen_key_ring
  + pen_far_row * max(0, up[p] - 1) + (if before[p, 5] > 0 then pen_finger_conflict else 0) - bonus_alternate * allcross[p];
param pathcost {p in PATHS} = keycost[p] + sum {s in 1..4: used[p, s] = 1} modcost[p, s];

var take {PATHS} binary;
s.t. one {c in CHORDS}: sum {p in PATHS: chord[p] = c} take[p] = 1;
var cost {c in CHORDS} = sum {p in PATHS: chord[p] = c} pathcost[p] * take[p];
minimize total: sum {c in CHORDS} cost[c];
