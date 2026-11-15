/* fuzz_parse.c - feeds random and mutated text to every parser: layout, weights, chord and the four importers.
 * The inputs are copied into exactly sized heap blocks so a sanitizer sees any read past the end. A parser may
 * reject its input but must not crash, hang or touch memory outside it.
 *   fuzz_parse [ITERATIONS [SEED]]     seeded run, the default mode of tests/run-tests
 * Built with -DEK_LIBFUZZER and clang's -fsanitize=fuzzer it exports LLVMFuzzerTestOneInput instead of main. */
#include "../src/ek.c"
#include "t.h"

static f64 g_mem[(8u * 1024u * 1024u) / sizeof(f64)];
static Arena arena;
static Layout L;

static void one(const char *data, size_t n)
{
    char *in = (char *)malloc(n + 1u);
    size_t mark = arena_mark(&arena);
    Layout T;
    LayoutErr le;
    Weights W;
    u32 line;
    const char *why;
    Chord c;
    int fmt;

    if (in == NULL) {
        return;
    }
    memcpy(in, data, n);
    layout_parse(in, n, &T, &le);
    weights_defaults(&W);
    weights_parse(in, n, &W, &line, &why);
    chord_parse(&L, in, n, &c);
    detect_format(in, n);
    for (fmt = 0; fmt < 4; fmt++) {
        Entries E;
        Report R;

        report_init(&R);
        if (entries_init(&arena, &E, import_capacity(in, n))) {
            import_any(fmt, &arena, &L, in, n, &E, &R);
            CHECK(R.nstored <= EK_MAXSKIPS);
            CHECK(E.n <= E.cap);
        }
        arena_reset(&arena, mark);
    }
    free(in);
}

#ifdef EK_LIBFUZZER
int LLVMFuzzerTestOneInput(const unsigned char *d, size_t n)
{
    static int ready;

    if (!ready) {
        arena_init(&arena, g_mem, sizeof(g_mem));
        if (!t_layout(&arena, "layouts/qwerty-ansi.layout", &L)) {
            abort();
        }
        ready = 1;
    }
    one((const char *)d, n);
    return 0;
}
#else

static const char *const words[] = {
    "bind", "bindm", "binde", "bindl", "bindsym", "bindcode", "set", "$mod", "$mainMod", "SUPER", "SHIFT", "CTRL",
    "ALT", "Mod4", "Mod1", "Return", "XKB_KEY_a", "XKB_KEY_Q", "MODKEY", "WLR_MODIFIER_ALT", "TAGKEYS(", "#define",
    "{", "}", "=", ",", "+", "-", "|", "\t", "\n", " ", "\\\n", "mouse:272", "code:10", "XF86AudioMute", "--release",
    "Ctrl+Alt+Delete", "C-x", "M-", "s-", "Super+Shift+1", "row", "key", "finger", "home", "mod", "name", "pitch",
    "travel", "seed", "0", "1", "-1", "1e308", "nan", "99999999999999999999", "\"", "'", "(", ")", "$", "#",
    "exec", "workspace", "fixed", "lshift", "esc", "a", "b", "space"};

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *p;
    long len;

    *n = 0;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (char *)malloc((size_t)len + 1u);
    if (p != NULL && fread(p, 1, (size_t)len, f) == (size_t)len) {
        *n = (size_t)len;
    }
    fclose(f);
    return p;
}

#define MAXIN 8192

/* random bytes, a salad of format words, or a mutated copy of a real file */
static size_t make_input(Rng *r, char *buf, char **corp, const size_t *clen, u32 ncorp)
{
    u32 mode = rng_below(r, 4);
    size_t n = 0, i;

    if (mode == 0) {
        n = rng_below(r, 400);
        for (i = 0; i < n; i++) {
            buf[i] = (char)rng_below(r, 256);
        }
    } else if (mode == 1) {
        u32 w = rng_below(r, 120);

        for (i = 0; i < w; i++) {
            const char *s = words[rng_below(r, sizeof(words) / sizeof(words[0]))];
            size_t l = strlen(s);

            if (n + l + 1u >= MAXIN) {
                break;
            }
            memcpy(buf + n, s, l);
            n += l;
            if (rng_below(r, 3) == 0) {
                buf[n++] = ' ';
            }
        }
    } else {
        u32 which = rng_below(r, ncorp), edits = 1u + rng_below(r, 8);

        n = clen[which] < MAXIN ? clen[which] : MAXIN;
        memcpy(buf, corp[which], n);
        if (mode == 3 && n > 0) {
            n = rng_below(r, (u32)n + 1u); /* truncation */
        }
        for (i = 0; i < edits && n > 0; i++) {
            size_t p = rng_below(r, (u32)n);

            switch (rng_below(r, 4)) {
            case 0: buf[p] = (char)rng_below(r, 256); break;
            case 1:
                if (n + 1u < MAXIN) {
                    memmove(buf + p + 1, buf + p, n - p);
                    buf[p] = (char)rng_below(r, 256);
                    n++;
                }
                break;
            case 2:
                memmove(buf + p, buf + p + 1, n - p - 1u);
                n--;
                break;
            default: {
                const char *s = words[rng_below(r, sizeof(words) / sizeof(words[0]))];
                size_t l = strlen(s);

                if (n + l < MAXIN) {
                    memmove(buf + p + l, buf + p, n - p);
                    memcpy(buf + p, s, l);
                    n += l;
                }
                break;
            }
            }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    static const char *const files[] = {"tests/data/sample.tsv", "tests/data/hyprland.conf", "tests/data/i3.conf",
                                        "tests/data/dwl-config.h", "layouts/qwerty-ansi.layout",
                                        "layouts/split-4x6.layout", "config/default.conf"};
    char *corp[sizeof(files) / sizeof(files[0])];
    size_t clen[sizeof(files) / sizeof(files[0])];
    u32 ncorp = 0, i, iters = argc > 1 ? (u32)strtoul(argv[1], NULL, 10) : 20000u;
    u32 seed = argc > 2 ? (u32)strtoul(argv[2], NULL, 10) : 1u;
    Rng r;
    static char buf[MAXIN + 64];

    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout(&arena, "layouts/qwerty-ansi.layout", &L)) {
        return 2;
    }
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        corp[ncorp] = slurp(files[i], &clen[ncorp]);
        if (corp[ncorp] == NULL) {
            fprintf(stderr, "cannot read %s (run from the repository root)\n", files[i]);
            return 2;
        }
        ncorp++;
    }
    rng_seed(&r, seed);
    for (i = 0; i < iters; i++) {
        size_t n = make_input(&r, buf, corp, clen, ncorp);

        one(buf, n);
    }
    for (i = 0; i < ncorp; i++) {
        free(corp[i]);
    }
    printf("fuzz_parse: %u inputs, seed %u\n", (unsigned)iters, (unsigned)seed);
    return t_report("fuzz_parse");
}
#endif
