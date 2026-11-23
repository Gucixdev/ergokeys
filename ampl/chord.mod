# chord.mod - what one keyboard chord costs the hand, written in AMPL from the physical description, not from the C code.
# A chord is a set of modifiers plus one key. Every finger starts at its home point. A modifier is held by whichever finger owns the
# modifier key; the key is pressed by its own finger. If the layout has two keys for a modifier (left and right Ctrl) the cheapest
# choice wins - that choice is the only decision here, so the solver makes it instead of a loop trying every combination.
# Units: millimetres of weighted finger travel plus penalties. All weights are parameters, the same names as ergokeys' config.

set FINGERS;                                   # LP LR LM LI LT RT RI RM RR RP
set KEYS;                                      # key labels of the layout
set MODS;                                      # ctrl alt super shift
set MODKEY {MODS} within KEYS;                 # the physical keys that give one modifier
set ALLCHORDS;                                 # every chord (ids)
set CHORDS within ALLCHORDS;                   # the ones priced now: chords do not depend on each other, so they can be solved in batches

param pitch;                                   # mm per key unit
param x {KEYS};  param y {KEYS};               # key centre, mm
param xlo {KEYS}; param xhi {KEYS};            # leftmost / rightmost point a finger may land on
param fing {KEYS} symbolic in FINGERS;         # the finger that presses the key
param hx {FINGERS}; param hy {FINGERS};        # home point of each finger, mm
param hand {FINGERS} in {0, 1};                # 0 left, 1 right
param pinky {FINGERS} in {0, 1};
param ring {FINGERS} in {0, 1};
param thumb {FINGERS} in {0, 1};
param cm {ALLCHORDS, MODS} in {0, 1};             # chord c holds modifier m
param ck {ALLCHORDS} symbolic in KEYS;            # the key of chord c

param travel; param vertical_factor; param pen_mod_pinky; param pen_mod_ring; param pen_one_hand; param pen_far_row;
param pen_finger_conflict; param bonus_alternate; param pen_key_pinky; param pen_key_ring;

# where a finger lands on a key: the point of the key nearest to its home x, on the key's centre line
param land {k in KEYS} = max(xlo[k], min(xhi[k], hx[fing[k]]));
param dx {k in KEYS} = land[k] - hx[fing[k]];
param dy {k in KEYS} = y[k] - hy[fing[k]];
param walk {k in KEYS} = travel * sqrt(dx[k]^2 + (vertical_factor * dy[k])^2);      # weighted travel of the finger to key k
param moves {k in KEYS} = if sqrt(dx[k]^2 + dy[k]^2) > 1e-9 then 1 else 0;
param up {k in KEYS} = (hy[fing[k]] - y[k]) / pitch;                                   # rows above the finger's home row

# holding modifier key k: its travel, plus pinky/ring strain if that finger had to move
param hold {k in KEYS} = walk[k] + moves[k] * (pinky[fing[k]] * pen_mod_pinky + ring[fing[k]] * pen_mod_ring);
# pressing key k: its travel, the weak-finger charge, and reaching above the home row beyond one row
param press {k in KEYS} = walk[k] + pinky[fing[k]] * pen_key_pinky + ring[fing[k]] * pen_key_ring + pen_far_row * max(0, up[k] - 1);

var pick {c in CHORDS, m in MODS, k in MODKEY[m]: cm[c, m] = 1} binary;               # which key holds modifier m
var over {CHORDS, FINGERS} >= 0;                                                       # presses beyond the first on one finger
var cross {CHORDS} binary;                                                             # every modifier is held by the other hand than the key

s.t. one {c in CHORDS, m in MODS: cm[c, m] = 1}: sum {k in MODKEY[m]} pick[c, m, k] = 1;
s.t. clash {c in CHORDS, f in FINGERS}:
  over[c, f] >= sum {m in MODS, k in MODKEY[m]: cm[c, m] = 1 and fing[k] = f} pick[c, m, k] + (if fing[ck[c]] = f then 1 else 0) - 1;
s.t. crossnot {c in CHORDS, m in MODS, k in MODKEY[m]: cm[c, m] = 1 and hand[fing[k]] = hand[fing[ck[c]]]}: cross[c] <= 1 - pick[c, m, k];
s.t. crossnone {c in CHORDS: sum {m in MODS} cm[c, m] = 0}: cross[c] = 0;

# the cost of a chord; a modifier on a non-thumb finger of the key's own hand costs pen_one_hand extra
var cost {c in CHORDS} = press[ck[c]]
  + sum {m in MODS, k in MODKEY[m]: cm[c, m] = 1} pick[c, m, k] * (hold[k] + (if hand[fing[k]] = hand[fing[ck[c]]] and thumb[fing[k]] = 0 then pen_one_hand else 0))
  + pen_finger_conflict * sum {f in FINGERS} over[c, f]
  - bonus_alternate * cross[c];

minimize total: sum {c in CHORDS} cost[c];
