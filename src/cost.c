/* cost.c - the weights, and the cost model: fingers start at home, travel to the keys of a chord, and the chord
 * is charged for travel plus penalties minus a bonus.
 *
 * Every number lives in the table below (name, default, documentation). A config file overrides by name;
 * `ergokeys config` prints this table as a config file. Cost units are millimetres of weighted travel.
 *
 * A step presses the modifiers and then the key. For each modifier the layout may offer several physical keys
 * (left and right Ctrl); every combination is tried and the cheapest is kept, ties to the first. A finger moves in
 * a straight line from where it is to the key. Per step:
 *   travel          sum of weighted finger distances, in mm: sqrt(dx^2 + (vertical_factor*dy)^2)
 *   pen_mod_pinky   a pinky that has to move to a modifier; pen_mod_ring the same for a ring finger
 *   pen_one_hand    per modifier held by a non-thumb finger of the hand that also presses the key
 *   bonus_alternate subtracted when every modifier is on the other hand than the key, and for a step whose key is
 *                   on the other hand than the previous step's key
 *   pen_far_row     per key row the key sits above its finger's home row beyond the first one
 *   pen_same_finger / pen_repeat_key  the key uses the finger of the previous step's key (another key / the same)
 *   pen_finger_conflict  two keys of one step need the same finger
 * After the last step, with return_trip = 1, every finger that moved walks back home. The cost of a whole shortcut
 * is clamped at zero. */

enum {
    C_TRAVEL, C_VERTICAL, C_MOD_PINKY, C_MOD_RING, C_ONE_HAND, C_SAME_FINGER, C_REPEAT_KEY, C_FAR_ROW,
    C_FINGER_CONFLICT, C_ALTERNATE, C_RETURN_TRIP, C_SUGGEST_MODS, C_SEED, C_ANNEAL_ITERS, C_ANNEAL_T0,
    C_ANNEAL_T1, C_GREEDY_PASSES, C_KEY_PINKY, C_KEY_RING, C_HAND_SPAN, C_OVERSPAN, C_FINGER_REACH, C_OVERREACH, EK_NCONF
};

typedef struct {
    const char *name;
    f64 def;
    const char *doc;
} ConfDef;

static const ConfDef ek_conf[EK_NCONF] = {
    {"travel", 1.0, "cost per millimetre of weighted finger travel"},
    {"vertical_factor", 1.3, "multiplier on the vertical part of a finger move; reaching up or down is slower than sideways"},
    {"pen_mod_pinky", 15.0, "a pinky that has to move to press a modifier"},
    {"pen_mod_ring", 5.0, "a ring finger that has to move to press a modifier"},
    {"pen_one_hand", 20.0, "per modifier held by a non-thumb finger of the hand that also presses the key"},
    {"pen_same_finger", 25.0, "a sequence step whose key uses the finger of the previous step's different key"},
    {"pen_repeat_key", 5.0, "a sequence step that presses the previous step's key again"},
    {"pen_far_row", 12.0, "per key row above the finger's home row beyond the first (the number row is one extra row)"},
    {"pen_finger_conflict", 200.0, "two keys of one step that need the same finger"},
    {"bonus_alternate", 6.0, "subtracted when modifiers and key are on different hands, or consecutive keys alternate hands"},
    {"return_trip", 0.0, "1 counts the walk back to home after the last step, 0 stops at the last key"},
    {"suggest_mods", 0.0, "0 keeps each action's modifiers and moves only its key, 1 may also use any modifier set seen in the input"},
    {"seed", 1.0, "seed of the suggestion search; the same seed gives the same result"},
    {"anneal_iters", 200000.0, "simulated annealing steps in suggest, 0 for greedy swaps only"},
    {"anneal_t0", 8.0, "annealing start temperature, in cost units per average action weight"},
    {"anneal_t1", 0.05, "annealing end temperature"},
    {"greedy_passes", 30.0, "most passes of the greedy improvement before and after annealing"},
    {"pen_key_pinky", 8.0, "a pinky that presses the key of a step, home key or not; 0 treats every finger alike on the key"},
    {"pen_key_ring", 3.0, "a ring finger that presses the key of a step, home key or not"},
    {"hand_span", 0.0, "your hand: the widest distance in mm between two non-thumb fingers of one hand that is still comfortable (measure it, see docs/DESIGN.md); 0 turns the reach check off"},
    {"pen_overspan", 1.0, "per millimetre two fingers of one hand are pressed further apart than hand_span"},
    {"finger_reach", 0.0, "your hand: how far in mm a non-thumb finger comfortably reaches from its home key without the hand moving (see docs/DESIGN.md); 0 turns the check off"},
    {"pen_overreach", 1.0, "per millimetre a non-thumb finger lands further from its home key than finger_reach"}};

typedef struct {
    f64 v[EK_NCONF];
} Weights;

static void weights_defaults(Weights *W)
{
    int i;

    for (i = 0; i < EK_NCONF; i++) {
        W->v[i] = ek_conf[i].def;
    }
}

/* "name value" or "name = value" per line, '#' comments. Returns 1 on success, else 0 with the line and reason. */
static int weights_parse(const char *buf, size_t len, Weights *W, u32 *errline, const char **why)
{
    size_t pos = 0;
    u32 line = 0;

    while (pos < len) {
        const char *b = buf + pos, *e, *nb, *ne, *vb, *ve;
        size_t q = pos;
        char tmp[40];
        char *end;
        int i, found = -1;
        f64 v;

        while (q < len && buf[q] != '\n') {
            q++;
        }
        e = buf + q;
        pos = q < len ? q + 1u : q;
        line++;
        *errline = line;
        ek_trim(&b, &e);
        if (b >= e || *b == '#') {
            continue;
        }
        nb = b;
        while (b < e && !ek_is_space((u8)*b) && *b != '=') {
            b++;
        }
        ne = b;
        while (b < e && (ek_is_space((u8)*b) || *b == '=')) {
            b++;
        }
        vb = b;
        ve = e;
        for (i = 0; i < EK_NCONF; i++) {
            if ((size_t)(ne - nb) == strlen(ek_conf[i].name) && memcmp(nb, ek_conf[i].name, (size_t)(ne - nb)) == 0) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            *why = "unknown setting";
            return 0;
        }
        if (ve <= vb || (size_t)(ve - vb) >= sizeof(tmp)) {
            *why = "setting needs a number";
            return 0;
        }
        memcpy(tmp, vb, (size_t)(ve - vb));
        tmp[ve - vb] = '\0';
        v = strtod(tmp, &end);
        if (*end != '\0' || v != v || v > 1e9 || v < -1e9) {
            *why = "setting needs a number";
            return 0;
        }
        if (found != C_SUGGEST_MODS && found != C_SEED && found != C_RETURN_TRIP && found != C_ANNEAL_T0 &&
            found != C_ANNEAL_T1 && v < 0.0) {
            *why = "setting must not be negative";
            return 0;
        }
        W->v[found] = v;
    }
    return 1;
}

/* ---- the model ---- */

typedef struct {
    f64 x[EK_NF];
    f64 y[EK_NF];
    int last;           /* key pressed by the previous step, -1 at the start */
} Fingers;

static void fingers_home(const Layout *L, Fingers *st)
{
    int f;

    for (f = 0; f < EK_NF; f++) {
        st->x[f] = L->hx[f];
        st->y[f] = L->hy[f];
    }
    st->last = -1;
}

/* straight-line distance in mm */
static f64 ek_dist(f64 x0, f64 y0, f64 x1, f64 y1)
{
    f64 dx = x1 - x0, dy = y1 - y0;

    return ek_sqrt(dx * dx + dy * dy);
}

/* reach cost of finger f landing at (tx, ty): how far past finger_reach it is from its home key, thumbs exempt */
static f64 reach_cost(const Layout *L, const Weights *W, int f, f64 tx, f64 ty)
{
    f64 over;

    if (W->v[C_FINGER_REACH] <= 0.0 || EK_IS_THUMB(f)) {
        return 0.0;
    }
    over = ek_dist(L->hx[f], L->hy[f], tx, ty) - W->v[C_FINGER_REACH];
    return over > 0.0 ? W->v[C_OVERREACH] * over : 0.0;
}

typedef struct {
    f64 mm;    /* finger travel, millimetres */
    f64 cost;  /* travel weight, penalties and bonus */
} Cost;

/* Cost of one step starting from the finger state *in; the cheapest modifier-key combination wins. *out is the
 * state after it. The step may leave the cost negative: only a whole shortcut is clamped. */
static Cost step_cost(const Layout *L, const Weights *W, const Fingers *in, u8 mods, u8 key, Fingers *out)
{
    int act[4];
    u8 pick[4];
    u32 nact = 0, m, i;
    int have = 0;
    Cost best;
    Fingers bestst;

    best.mm = 0.0;
    best.cost = 0.0;
    bestst = *in;
    for (m = 0; m < 4u; m++) {
        if (mods & (1u << m)) {
            act[nact] = (int)m;
            pick[nact] = 0;
            nact++;
        }
    }

    for (;;) {
        Fingers st = *in;
        u32 used = 0;
        f64 mm = 0.0, cost = 0.0;
        int keyf = L->finger[key];
        int allcross = nact > 0u;

        for (i = 0; i < nact; i++) {
            u32 k = L->modkey[act[i]][pick[i]];
            int f = L->finger[k];
            f64 tx = layout_land_x(L, k, st.x[f]), ty = L->y[k];
            f64 dx = tx - st.x[f], dy = ty - st.y[f];
            f64 d = ek_dist(st.x[f], st.y[f], tx, ty);

            mm += d;
            cost += W->v[C_TRAVEL] * ek_sqrt(dx * dx + W->v[C_VERTICAL] * W->v[C_VERTICAL] * dy * dy);
            cost += reach_cost(L, W, f, tx, ty);
            if (used & (1u << f)) {
                cost += W->v[C_FINGER_CONFLICT];
            }
            used |= 1u << f;
            if (d > 1e-9) {
                if (EK_IS_PINKY(f)) {
                    cost += W->v[C_MOD_PINKY];
                } else if (EK_IS_RING(f)) {
                    cost += W->v[C_MOD_RING];
                }
            }
            if (EK_HAND(f) == EK_HAND(keyf)) {
                allcross = 0;
                if (!EK_IS_THUMB(f)) {
                    cost += W->v[C_ONE_HAND];
                }
            }
            st.x[f] = tx;
            st.y[f] = ty;
        }

        {
            f64 tx = layout_land_x(L, key, st.x[keyf]), ty = L->y[key];
            f64 dx = tx - st.x[keyf], dy = ty - st.y[keyf];
            f64 up = (L->hy[keyf] - ty) / L->pitch;

            mm += ek_dist(st.x[keyf], st.y[keyf], tx, ty);
            cost += W->v[C_TRAVEL] * ek_sqrt(dx * dx + W->v[C_VERTICAL] * W->v[C_VERTICAL] * dy * dy);
            cost += reach_cost(L, W, keyf, tx, ty);
            if (used & (1u << keyf)) {
                cost += W->v[C_FINGER_CONFLICT];
            }
            if (EK_IS_PINKY(keyf)) {
                cost += W->v[C_KEY_PINKY];
            } else if (EK_IS_RING(keyf)) {
                cost += W->v[C_KEY_RING];
            }
            if (up > 1.0 + 1e-9) {
                cost += W->v[C_FAR_ROW] * (up - 1.0);
            }
            if (allcross) {
                cost -= W->v[C_ALTERNATE];
            }
            if (in->last >= 0) {
                int pf = L->finger[in->last];

                if (pf == keyf) {
                    cost += (in->last == (int)key) ? W->v[C_REPEAT_KEY] : W->v[C_SAME_FINGER];
                } else if (EK_HAND(pf) != EK_HAND(keyf)) {
                    cost -= W->v[C_ALTERNATE];
                }
            }
            st.x[keyf] = tx;
            st.y[keyf] = ty;
            st.last = (int)key;
            used |= 1u << keyf;
        }

        if (W->v[C_HAND_SPAN] > 0.0) {
            int fa, fb;

            for (fa = 0; fa < EK_NF; fa++) {
                for (fb = fa + 1; fb < EK_NF; fb++) {
                    if ((used & (1u << fa)) && (used & (1u << fb)) && EK_HAND(fa) == EK_HAND(fb) && !EK_IS_THUMB(fa) &&
                        !EK_IS_THUMB(fb)) {
                        f64 gap = ek_dist(st.x[fa], st.y[fa], st.x[fb], st.y[fb]) - W->v[C_HAND_SPAN];

                        if (gap > 0.0) {
                            cost += W->v[C_OVERSPAN] * gap;
                        }
                    }
                }
            }
        }

        if (!have || cost < best.cost) {
            have = 1;
            best.mm = mm;
            best.cost = cost;
            bestst = st;
        }

        /* odometer over the physical-key choices */
        for (i = 0; i < nact; i++) {
            if ((u32)pick[i] + 1u < L->nmod[act[i]]) {
                pick[i]++;
                break;
            }
            pick[i] = 0;
        }
        if (i == nact) {
            break;
        }
    }
    *out = bestst;
    return best;
}

/* Cost of a whole shortcut from rest: every step in turn, fingers staying where the last step left them. */
static Cost shortcut_cost(const Layout *L, const Weights *W, const Chord *c)
{
    Fingers st, nx;
    Cost total, s;
    u32 i;
    int f;

    total.mm = 0.0;
    total.cost = 0.0;
    fingers_home(L, &st);
    for (i = 0; i < c->nsteps; i++) {
        s = step_cost(L, W, &st, c->mods[i], c->key[i], &nx);
        total.mm += s.mm;
        total.cost += s.cost;
        st = nx;
    }
    if (W->v[C_RETURN_TRIP] > 0.5) {
        for (f = 0; f < EK_NF; f++) {
            f64 dx = L->hx[f] - st.x[f], dy = L->hy[f] - st.y[f];

            total.mm += ek_dist(st.x[f], st.y[f], L->hx[f], L->hy[f]);
            total.cost += W->v[C_TRAVEL] * ek_sqrt(dx * dx + W->v[C_VERTICAL] * W->v[C_VERTICAL] * dy * dy);
        }
    }
    if (total.cost < 0.0) {
        total.cost = 0.0;
    }
    return total;
}
