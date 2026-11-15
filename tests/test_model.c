/* test_model.c - the cost model against numbers worked out by hand on the qwerty-ansi layout.
 * Geometry used below (pitch 19.05 mm): home keys sit at y = 76.2, F is at x = 100.0 (left index home), J at
 * x = 157.2 (right index home), G is one pitch right of F, H one pitch left of J, R is one row above and 4.7625 mm
 * left of F's column, B is one row below and 9.525 mm right of F. */
#include "../src/ek.c"
#include "t.h"

static f64 g_mem[(8u * 1024u * 1024u) / sizeof(f64)];

static void zero_penalties(Weights *W)
{
    int i;

    for (i = 0; i < EK_NCONF; i++) {
        W->v[i] = 0.0;
    }
    W->v[C_TRAVEL] = 1.0;
    W->v[C_VERTICAL] = 1.3;
}

static Cost cost_of(const Layout *L, const Weights *W, const char *text)
{
    Chord c;
    const char *why = t_chord(L, text, &c);
    Cost z;

    z.mm = -1.0;
    z.cost = -1.0;
    if (why != NULL) {
        fprintf(stderr, "chord '%s': %s\n", text, why);
        t_fails++;
        return z;
    }
    return shortcut_cost(L, W, &c);
}

/* a copy of L where modifier m is offered only on the key labelled lab */
static Layout only_mod(const Layout *L, int m, const char *lab)
{
    Layout M = *L;
    int k = layout_find(L, lab, strlen(lab));

    M.nmod[m] = 1;
    M.modkey[m][0] = (u8)k;
    return M;
}

static void test_geometry(const Layout *L)
{
    int g = layout_find(L, "g", 1), f = layout_find(L, "f", 1), h = layout_find(L, "h", 1), j = layout_find(L, "j", 1);

    CHECK(g >= 0 && f >= 0 && h >= 0 && j >= 0);
    CHECK_NEAR(L->pitch, 19.05, 1e-12);
    CHECK_NEAR(L->x[g] - L->x[f], 19.05, 1e-9);
    CHECK_NEAR(L->x[j] - L->x[h], 19.05, 1e-9);
    CHECK_NEAR(L->hx[F_LI], L->x[f], 1e-9);
    CHECK_NEAR(L->hx[F_RI], L->x[j], 1e-9);
    CHECK_NEAR(L->y[g], L->y[f], 1e-9);
}

static void test_hand_values(const Layout *L)
{
    Weights W;
    Cost c;

    zero_penalties(&W);
    /* G is one pitch from F: adjacent keys are 19.05 mm apart */
    c = cost_of(L, &W, "g");
    CHECK_NEAR(c.mm, 19.05, 1e-9);
    CHECK_NEAR(c.cost, 19.05, 1e-9);
    /* A is the left pinky's home key */
    c = cost_of(L, &W, "a");
    CHECK_NEAR(c.mm, 0.0, 1e-9);
    CHECK_NEAR(c.cost, 0.0, 1e-9);
    /* R: dx 4.7625, dy 19.05; cost uses 1.3 on dy */
    c = cost_of(L, &W, "r");
    CHECK_NEAR(c.mm, sqrt(4.7625 * 4.7625 + 19.05 * 19.05), 1e-9);
    CHECK_NEAR(c.cost, sqrt(4.7625 * 4.7625 + 1.3 * 19.05 * 1.3 * 19.05), 1e-9);
    /* with the vertical factor at 1 the cost equals the distance */
    W.v[C_VERTICAL] = 1.0;
    c = cost_of(L, &W, "r");
    CHECK_NEAR(c.cost, c.mm, 1e-9);
    /* a travel weight of 2 doubles the cost and leaves the millimetres alone */
    W.v[C_TRAVEL] = 2.0;
    c = cost_of(L, &W, "g");
    CHECK_NEAR(c.mm, 19.05, 1e-9);
    CHECK_NEAR(c.cost, 38.1, 1e-9);
}

static void test_modifiers(const Layout *L)
{
    Weights W;
    Layout left, right;
    Cost c, cl, cr;

    zero_penalties(&W);
    left = only_mod(L, 0, "lctrl");
    right = only_mod(L, 0, "rctrl");
    /* the cheaper Ctrl key is used */
    cl = cost_of(&left, &W, "Ctrl+c");
    cr = cost_of(&right, &W, "Ctrl+c");
    c = cost_of(L, &W, "Ctrl+c");
    CHECK(cl.cost != cr.cost);
    CHECK_NEAR(c.cost, cl.cost < cr.cost ? cl.cost : cr.cost, 1e-9);
    CHECK_NEAR(c.mm, cl.cost < cr.cost ? cl.mm : cr.mm, 1e-9);
    /* Ctrl alone costs more than the key alone by the modifier's travel */
    CHECK(c.mm > cost_of(L, &W, "c").mm);

    /* pen_mod_pinky: lctrl is a left pinky key; J is on the right hand, so no other penalty applies */
    W.v[C_TRAVEL] = 0.0;
    W.v[C_MOD_PINKY] = 15.0;
    c = cost_of(&left, &W, "Ctrl+j");
    CHECK_NEAR(c.cost, 15.0, 1e-9);
    /* pen_one_hand: key A on the same hand as lctrl */
    W.v[C_ONE_HAND] = 20.0;
    c = cost_of(&left, &W, "Ctrl+a");
    CHECK_NEAR(c.cost, 35.0, 1e-9);
    c = cost_of(&left, &W, "Ctrl+j");
    CHECK_NEAR(c.cost, 15.0, 1e-9);
    /* with both Ctrl keys available the opposite hand is chosen: only the pinky penalty stays */
    c = cost_of(L, &W, "Ctrl+a");
    CHECK_NEAR(c.cost, 15.0, 1e-9);
    /* a thumb modifier is exempt from pen_one_hand and pen_mod_pinky: lalt is a left thumb key */
    {
        Layout t = only_mod(L, 1, "lalt");

        c = cost_of(&t, &W, "Alt+a");
        CHECK_NEAR(c.cost, 0.0, 1e-9);
    }
    /* the alternation bonus can push a cost below zero; a whole shortcut is clamped at zero */
    W.v[C_MOD_PINKY] = 0.0;
    W.v[C_ONE_HAND] = 0.0;
    W.v[C_ALTERNATE] = 100.0;
    c = cost_of(&left, &W, "Ctrl+j");
    CHECK_NEAR(c.cost, 0.0, 1e-12);
}

static void test_sequences(const Layout *L)
{
    Weights W;
    Cost c;

    zero_penalties(&W);
    /* G then H: the left index goes F to G, the right index J to H, each one pitch */
    c = cost_of(L, &W, "g h");
    CHECK_NEAR(c.mm, 38.1, 1e-9);
    /* G then G: the finger is already there, the second step does not move */
    c = cost_of(L, &W, "g g");
    CHECK_NEAR(c.mm, 19.05, 1e-9);
    /* G then B with the same finger: from G (119.06, 76.2) to B (128.58, 95.25) */
    c = cost_of(L, &W, "g b");
    CHECK_NEAR(c.mm, 19.05 + sqrt(9.525 * 9.525 + 19.05 * 19.05), 1e-9);
    CHECK_NEAR(c.cost, 19.05 + sqrt(9.525 * 9.525 + 1.3 * 19.05 * 1.3 * 19.05), 1e-9);
    /* penalties of the sequence terms */
    W.v[C_SAME_FINGER] = 25.0;
    c = cost_of(L, &W, "g b");
    CHECK_NEAR(c.cost, 19.05 + sqrt(9.525 * 9.525 + 1.3 * 19.05 * 1.3 * 19.05) + 25.0, 1e-9);
    W.v[C_REPEAT_KEY] = 5.0;
    c = cost_of(L, &W, "g g");
    CHECK_NEAR(c.cost, 19.05 + 5.0, 1e-9);
    W.v[C_SAME_FINGER] = 0.0;
    W.v[C_REPEAT_KEY] = 0.0;
    W.v[C_ALTERNATE] = 6.0;
    c = cost_of(L, &W, "g h");
    CHECK_NEAR(c.cost, 38.1 - 6.0, 1e-9);
    /* the walk home after the last step */
    W.v[C_ALTERNATE] = 0.0;
    W.v[C_RETURN_TRIP] = 1.0;
    c = cost_of(L, &W, "g");
    CHECK_NEAR(c.mm, 38.1, 1e-9);
    CHECK_NEAR(c.cost, 38.1, 1e-9);
    c = cost_of(L, &W, "a");
    CHECK_NEAR(c.mm, 0.0, 1e-9);
}

static void test_exhaustive(const Layout *L)
{
    Weights W, Z;
    u32 m, k;
    u32 bad = 0, nonfinite = 0, n = 0, moved = 0;

    weights_defaults(&W);
    zero_penalties(&Z);
    for (m = 0; m < EK_NMASK; m++) {
        for (k = 0; k < L->nkeys; k++) {
            Chord c;
            Cost a, b;

            if (!L->chordable[k]) {
                continue;
            }
            memset(&c, 0, sizeof(c));
            c.nsteps = 1;
            c.mods[0] = (u8)m;
            c.key[0] = (u8)k;
            a = shortcut_cost(L, &W, &c);
            b = shortcut_cost(L, &Z, &c);
            n++;
            if (a.cost < 0.0 || a.mm < 0.0 || b.cost < 0.0) {
                bad++;
            }
            if (!(a.cost == a.cost) || !(a.mm == a.mm) || a.cost > 1e6) {
                nonfinite++;
            }
            /* without modifiers there is no choice to make, so millimetres do not depend on the weights */
            if (m == 0u && fabs(a.mm - b.mm) > 1e-9) {
                moved++;
            }
        }
    }
    CHECK(n > 500u);
    CHECK(bad == 0u);
    CHECK(nonfinite == 0u);
    CHECK(moved == 0u);
}

static void test_mirror(void)
{
    /* the same cost for a chord and its mirror image on a layout that is symmetric: ortho-5x12 mirrors
     * columns 0..5 onto 11..6 for the letter keys */
    static Arena arena;
    Layout L;
    Weights W;
    static const char *const pair[][2] = {{"r", "u"}, {"f", "j"}, {"b", "n"}, {"t", "y"}, {"e", "i"}, {"x", "."}};
    u32 i;

    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout(&arena, "layouts/ortho-5x12.layout", &L)) {
        t_fails++;
        return;
    }
    zero_penalties(&W);
    for (i = 0; i < sizeof(pair) / sizeof(pair[0]); i++) {
        Cost a = cost_of(&L, &W, pair[i][0]), b = cost_of(&L, &W, pair[i][1]);

        CHECK_NEAR(a.mm, b.mm, 1e-9);
        CHECK_NEAR(a.cost, b.cost, 1e-9);
    }
}

static void test_sqrt_exp(void)
{
    f64 x;
    f64 worst = 0.0;

    for (x = 0.001; x < 1e5; x *= 1.37) {
        f64 r = ek_sqrt(x), d = fabs(r * r - x) / x;

        if (d > worst) {
            worst = d;
        }
    }
    CHECK(worst < 1e-14);
    CHECK_NEAR(ek_sqrt(0.0), 0.0, 0.0);
    CHECK_NEAR(ek_sqrt(-4.0), 0.0, 0.0);
    worst = 0.0;
    for (x = 0.0; x < 30.0; x += 0.37) {
        f64 d = fabs(ek_expneg(x) - exp(-x)) / exp(-x);

        if (d > worst) {
            worst = d;
        }
    }
    CHECK(worst < 1e-10);
    CHECK_NEAR(ek_expneg(100.0), 0.0, 0.0);
    CHECK_NEAR(ek_expneg(0.0), 1.0, 0.0);
}

int main(void)
{
    static Arena arena;
    Layout L;

    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout(&arena, "layouts/qwerty-ansi.layout", &L)) {
        return 2;
    }
    test_geometry(&L);
    test_hand_values(&L);
    test_modifiers(&L);
    test_sequences(&L);
    test_exhaustive(&L);
    test_mirror();
    test_sqrt_exp();
    return t_report("test_model");
}
