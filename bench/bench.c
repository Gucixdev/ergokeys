/* bench.c - time suggest on a set of shortcuts and the cost model on its own.
 *   bench [N [RUNS]]    N shortcuts (default 500), RUNS repetitions per row (default 9)
 * Prints one markdown table row per measurement: median and minimum wall time over RUNS runs, and the quality
 * reached. The input is synthetic but fixed: the same seed always gives the same shortcuts. */
#define _POSIX_C_SOURCE 199309L
#include "../src/ek.c"
#include <time.h>

static f64 g_mem[(64u * 1024u * 1024u) / sizeof(f64)];
static Arena arena;
static Layout L;
static Weights W;

static int t_layout_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    LayoutErr e;
    char *buf;
    long n;
    int ok;

    if (f == NULL) {
        fprintf(stderr, "cannot read %s (run from the repository root)\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)arena_alloc(&arena, (size_t)n + 1u);
    ok = buf != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n && layout_parse(buf, (size_t)n, &L, &e);
    fclose(f);
    return ok;
}

static f64 now(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (f64)t.tv_sec + (f64)t.tv_nsec * 1e-9;
}

static int cmp_f64(const void *a, const void *b)
{
    f64 x = *(const f64 *)a, y = *(const f64 *)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static void make_entries(Entries *E, u32 n)
{
    Rng r;
    u32 i = 0, tries = 0;

    rng_seed(&r, 4242u);
    entries_init(&arena, E, n + 8u);
    while (i < n && tries++ < n * 200u) {
        Chord c;
        u32 j, k;
        int clash = 0;

        memset(&c, 0, sizeof(c));
        c.nsteps = 1;
        do {
            k = rng_below(&r, L.nkeys);
        } while (!L.chordable[k]);
        c.mods[0] = (u8)(1u + rng_below(&r, 15)); /* any non-empty modifier set */
        c.key[0] = (u8)k;
        for (j = 0; j < E->n; j++) {
            if (chord_equal(&c, &E->chord[j])) {
                clash = 1;
                break;
            }
        }
        if (!clash) {
            entries_add(&arena, E, "action", 6, &c, (f64)(1u + rng_below(&r, 50)), 0, i + 1u);
            i++;
        }
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

static void row(const char *name, const Entries *E, const OptCfg *C, u32 runs)
{
    f64 *t = EK_NEW(&arena, f64, runs);
    Chord *out = EK_NEW(&arena, Chord, E->n + 1u);
    OptStat S;
    u32 i;
    f64 before = total(E, E->chord);

    for (i = 0; i < runs; i++) {
        f64 t0 = now();
        int rc = suggest(&arena, &L, &W, E, C, out, &S);

        t[i] = now() - t0;
        if (rc != 0) {
            fprintf(stderr, "suggest failed: %d\n", rc);
            exit(1);
        }
    }
    qsort(t, runs, sizeof(f64), cmp_f64);
    printf("| %-30s | %4u | %9.2f | %9.2f | %9.1f | %9.1f | %6.1f%% |\n", name, (unsigned)E->n, t[runs / 2] * 1e3,
           t[0] * 1e3, before, total(E, out), 100.0 * (before - total(E, out)) / before);
}

int main(int argc, char **argv)
{
    u32 n = argc > 1 ? (u32)strtoul(argv[1], NULL, 10) : 500u;
    u32 runs = argc > 2 ? (u32)strtoul(argv[2], NULL, 10) : 9u;
    Entries E;
    OptCfg C;
    Chord c;
    f64 t0, t1;
    u32 i, reps = 200000u;
    f64 sink = 0.0;

    if (runs == 0u) {
        runs = 1u;
    }
    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout_load("layouts/qwerty-ansi.layout")) {
        return 2;
    }
    weights_defaults(&W);
    make_entries(&E, n);

    printf("| %-30s | %4s | %9s | %9s | %9s | %9s | %7s |\n", "case", "n", "median ms", "min ms", "cost in", "cost out",
           "lower");
    printf("|%s|%s|%s|%s|%s|%s|%s|\n", "--------------------------------", "------", "-----------", "-----------",
           "-----------", "-----------", "---------");
    memset(&C, 0, sizeof(C));
    C.seed = 1;
    C.passes = 30;
    C.t0 = 8.0;
    C.t1 = 0.05;
    C.iters = 0;
    row("greedy only", &E, &C, runs);
    C.iters = 20000;
    row("greedy + anneal 20000", &E, &C, runs);
    C.iters = 200000;
    row("greedy + anneal 200000", &E, &C, runs);
    C.anymods = 1;
    C.iters = 0;
    row("any modifiers, greedy only", &E, &C, runs);
    C.iters = 20000;
    row("any modifiers, anneal 20000", &E, &C, runs);
    C.iters = 200000;
    row("any modifiers, anneal 200000", &E, &C, runs);

    memset(&c, 0, sizeof(c));
    c.nsteps = 1;
    c.mods[0] = M_CTRL | M_SHIFT;
    t0 = now();
    for (i = 0; i < reps; i++) {
        c.key[0] = (u8)(i % L.nkeys);
        sink += shortcut_cost(&L, &W, &c).cost;
    }
    t1 = now();
    printf("\nshortcut_cost, Ctrl+Shift+key: %.0f ns per call (%u calls, checksum %.1f)\n", (t1 - t0) / (f64)reps * 1e9,
           (unsigned)reps, sink);
    return 0;
}
