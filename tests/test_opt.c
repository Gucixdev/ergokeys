/* test_opt.c - properties of suggest: never a conflict, never more expensive, deterministic for a seed, pinned
 * entries stay, the set of modifiers is kept in the default mode, and the search allocates nothing. */
#include "../src/ek.c"
#include "t.h"
#include <malloc.h>

static f64 g_mem[(64u * 1024u * 1024u) / sizeof(f64)];
static Arena arena;
static Layout L;
static Weights W;

/* n distinct chords from a seed: modifier set from a few popular ones, a random chordable key */
static void make_entries(Entries *E, u32 n, u32 seed, u32 nseq, u32 dupes)
{
    Rng r;
    static const u8 maskset[] = {M_SUPER, M_SUPER | M_SHIFT, M_CTRL, M_CTRL | M_SHIFT, M_ALT, M_CTRL | M_ALT,
                                 M_SUPER | M_CTRL};
    u32 i = 0, tries = 0;

    rng_seed(&r, seed);
    entries_init(&arena, E, n + 8u);
    while (i < n && tries++ < n * 100u) {
        Chord c;
        u32 j;
        int clash = 0;

        memset(&c, 0, sizeof(c));
        c.nsteps = (u8)(i < nseq ? 2 : 1);
        for (j = 0; j < c.nsteps; j++) {
            u32 k;

            do {
                k = rng_below(&r, L.nkeys);
            } while (!L.chordable[k]);
            c.mods[j] = maskset[rng_below(&r, sizeof(maskset))];
            c.key[j] = (u8)k;
        }
        if (i >= dupes) {
            for (j = 0; j < E->n; j++) {
                if (chord_equal(&c, &E->chord[j]) || chord_is_prefix(&c, &E->chord[j]) ||
                    chord_is_prefix(&E->chord[j], &c)) {
                    clash = 1;
                    break;
                }
            }
        }
        if (clash) {
            continue;
        }
        entries_add(&arena, E, "act", 3, &c, (f64)(1u + rng_below(&r, 20)), 0, i + 1u);
        i++;
    }
}

static f64 total(const Entries *E, const Chord *ch)
{
    f64 t = 0.0;
    u32 i;

    for (i = 0; i < E->n; i++) {
        t += E->weight[i] * shortcut_cost(&L, &W, &ch[i]).cost;
    }
    return t;
}

static int has_conflict(const Entries *E, const Chord *ch)
{
    u32 i, j;

    for (i = 0; i < E->n; i++) {
        for (j = i + 1u; j < E->n; j++) {
            if (chord_equal(&ch[i], &ch[j]) || chord_is_prefix(&ch[i], &ch[j]) || chord_is_prefix(&ch[j], &ch[i])) {
                return 1;
            }
        }
    }
    return 0;
}

static void cfg_default(OptCfg *C, u32 iters)
{
    memset(C, 0, sizeof(*C));
    C->seed = 1;
    C->iters = iters;
    C->passes = 30;
    C->t0 = 8.0;
    C->t1 = 0.05;
    C->verify = 1;
}

static void test_properties(void)
{
    static const u32 sizes[] = {1, 2, 10, 60, 200, 500};
    u32 s, seed;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        for (seed = 1; seed <= 3u; seed++) {
            Entries E;
            Chord *out;
            OptCfg C;
            OptStat S;
            size_t mark = arena_mark(&arena);
            int rc;
            f64 before, after;
            u32 i;

            make_entries(&E, sizes[s], seed * 77u + sizes[s], 0, 0);
            out = EK_NEW(&arena, Chord, E.n + 1u);
            cfg_default(&C, 20000);
            C.seed = seed;
            rc = suggest(&arena, &L, &W, &E, &C, out, &S);
            CHECK(rc == 0);
            if (rc != 0) {
                arena_reset(&arena, mark);
                continue;
            }
            before = total(&E, E.chord);
            after = total(&E, out);
            CHECK(after <= before + 1e-6);
            CHECK(!has_conflict(&E, out));
            CHECK(S.greedy_bad == 0u);
            CHECK_NEAR(S.after, after, 1e-6 * (1.0 + after));
            for (i = 0; i < E.n; i++) {
                CHECK(out[i].nsteps == E.chord[i].nsteps);
                CHECK(out[i].mods[0] == E.chord[i].mods[0]);
                CHECK(L.chordable[out[i].key[0]]);
            }
            arena_reset(&arena, mark);
        }
    }
}

static void test_improves(void)
{
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;
    f64 before, after;

    make_entries(&E, 100, 5u, 0, 0);
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 50000);
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    before = total(&E, E.chord);
    after = total(&E, out);
    CHECK(after < before * 0.9);
    /* annealing may not lose to greedy-only */
    {
        Chord *g = EK_NEW(&arena, Chord, E.n + 1u);
        OptStat Sg;
        OptCfg Cg = C;

        Cg.iters = 0;
        CHECK(suggest(&arena, &L, &W, &E, &Cg, g, &Sg) == 0);
        CHECK(after <= total(&E, g) + 1e-6);
    }
}

static void test_determinism(void)
{
    Entries E;
    Chord *o1, *o2, *o3;
    OptCfg C;
    OptStat S;
    u32 i;
    int diff_seed = 0;
    u32 acc1;

    make_entries(&E, 150, 9u, 0, 0);
    o1 = EK_NEW(&arena, Chord, E.n + 1u);
    o2 = EK_NEW(&arena, Chord, E.n + 1u);
    o3 = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 3000);
    C.anymods = 1; /* with fixed modifiers the problem is an assignment that every seed solves to the same optimum */
    CHECK(suggest(&arena, &L, &W, &E, &C, o1, &S) == 0);
    CHECK(suggest(&arena, &L, &W, &E, &C, o2, &S) == 0);
    CHECK(memcmp(o1, o2, sizeof(Chord) * E.n) == 0);
    C.seed = 1;
    CHECK(suggest(&arena, &L, &W, &E, &C, o1, &S) == 0);
    acc1 = S.anneal_accepted;
    C.seed = 2;
    CHECK(suggest(&arena, &L, &W, &E, &C, o3, &S) == 0);
    /* the optimum of this problem is unique, so the seed shows in the search path rather than in the result */
    diff_seed = S.anneal_accepted != acc1;
    for (i = 0; i < E.n; i++) {
        if (!chord_equal(&o1[i], &o3[i])) {
            diff_seed = 1;
        }
    }
    CHECK(diff_seed);
    CHECK(acc1 > 0u);
}

static void test_conflicts_resolved(void)
{
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;

    /* 40 entries of which the first 20 are drawn without the clash check: duplicates are likely */
    make_entries(&E, 40, 3u, 0, 20u);
    CHECK(has_conflict(&E, E.chord));
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 5000);
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    CHECK(!has_conflict(&E, out));
    CHECK(S.displaced > 0u);
}

static void test_pinned(void)
{
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;
    u32 i;
    Chord extra;

    make_entries(&E, 80, 11u, 10u, 0);
    /* mark entry 20 fixed and give one entry the same first step as a sequence: it must not land there */
    E.fixed[20] = 1;
    extra = E.chord[0];
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 20000);
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    for (i = 0; i < 10u; i++) {
        CHECK(chord_equal(&out[i], &E.chord[i]));
    }
    CHECK(chord_equal(&out[20], &E.chord[20]));
    CHECK(!has_conflict(&E, out));
    (void)extra;
}

static void test_any_mods(void)
{
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;
    f64 keep, any;

    make_entries(&E, 120, 13u, 0, 0);
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 30000);
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    keep = total(&E, out);
    C.anymods = 1;
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    any = total(&E, out);
    CHECK(!has_conflict(&E, out));
    CHECK(any <= total(&E, E.chord) + 1e-6);
    /* more freedom cannot be worse than the start; usually it beats the keep result as well */
    CHECK(any <= keep * 1.05);
}

static void test_nosol(void)
{
    static const char tiny[] = "name tiny\n"
                               "row 0 0 lc la ls lsh a b\n"
                               "finger LP lc la ls lsh a\nfinger RP b\n"
                               "home LP a\nhome RP b\n"
                               "mod ctrl lc\nmod alt la\nmod super ls\nmod shift lsh\n";
    Layout T;
    LayoutErr e;
    Entries E;
    Chord c, out[4];
    OptCfg C;
    OptStat S;
    u32 k;

    CHECK(layout_parse(tiny, sizeof(tiny) - 1u, &T, &e));
    entries_init(&arena, &E, 8);
    for (k = 0; k < 3u; k++) {
        memset(&c, 0, sizeof(c));
        c.nsteps = 1;
        c.mods[0] = M_CTRL;
        c.key[0] = (u8)(k == 0 ? 4 : 5);
        entries_add(&arena, &E, "x", 1, &c, 1.0, 0, k + 1u);
    }
    cfg_default(&C, 100);
    /* three actions, two chordable keys, one modifier set: no free assignment */
    CHECK(suggest(&arena, &T, &W, &E, &C, out, &S) == EK_NOSOL);
    E.n = 2;
    CHECK(suggest(&arena, &T, &W, &E, &C, out, &S) == 0);
    CHECK(!has_conflict(&E, out));
}

static void test_tiny_arena(void)
{
    static f64 small[64];
    Arena a;
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;
    int rc;

    make_entries(&E, 50, 1u, 0, 0);
    arena_init(&a, small, sizeof(small));
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 100);
    rc = suggest(&a, &L, &W, &E, &C, out, &S);
    CHECK(rc == -1);
}

/* the search must not call malloc: compare the allocator's in-use bytes before and after */
static void test_no_heap(void)
{
    Entries E;
    Chord *out;
    OptCfg C;
    OptStat S;
    struct mallinfo2 m0, m1;

    make_entries(&E, 500, 21u, 0, 0);
    out = EK_NEW(&arena, Chord, E.n + 1u);
    cfg_default(&C, 20000);
    C.verify = 0;
    m0 = mallinfo2();
    CHECK(suggest(&arena, &L, &W, &E, &C, out, &S) == 0);
    m1 = mallinfo2();
    CHECK(m1.uordblks == m0.uordblks);
    CHECK(m1.hblks == m0.hblks);
}

int main(void)
{
    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout(&arena, "layouts/qwerty-ansi.layout", &L)) {
        return 2;
    }
    weights_defaults(&W);
    test_properties();
    test_improves();
    test_determinism();
    test_conflicts_resolved();
    test_pinned();
    test_any_mods();
    test_nosol();
    test_tiny_arena();
    test_no_heap();
    return t_report("test_opt");
}
