/* t.h - a minimal test harness shared by the C tests: CHECK counts, report prints the tally. */
#ifndef EK_T_H
#define EK_T_H

#include <math.h>

static int t_checks = 0;
static int t_fails = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        t_checks++;                                                                                          \
        if (!(cond)) {                                                                                       \
            t_fails++;                                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                  \
        }                                                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                                \
    do {                                                                                                     \
        f64 t_a = (a), t_b = (b);                                                                            \
        t_checks++;                                                                                          \
        if (!(fabs(t_a - t_b) <= (tol))) {                                                                   \
            t_fails++;                                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s = %.9f, expected %.9f\n", __FILE__, __LINE__, #a, t_a, t_b);     \
        }                                                                                                    \
    } while (0)

static int t_report(const char *suite)
{
    printf("%s: %d checks, %d failed\n", suite, t_checks, t_fails);
    return t_fails == 0 ? 0 : 1;
}

/* whole file into the arena; NULL when it cannot be read */
static char *t_read(Arena *a, const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *p;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (char *)arena_alloc(a, (size_t)n + 1u);
    if (p != NULL && fread(p, 1, (size_t)n, f) != (size_t)n) {
        p = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return p;
}

static int t_layout(Arena *a, const char *path, Layout *L)
{
    size_t len;
    LayoutErr e;
    char *buf = t_read(a, path, &len);

    if (buf == NULL) {
        fprintf(stderr, "cannot read %s (run the tests from the repository root)\n", path);
        return 0;
    }
    if (!layout_parse(buf, len, L, &e)) {
        fprintf(stderr, "%s:%u: %s\n", path, (unsigned)e.line, e.why);
        return 0;
    }
    return 1;
}

static const char *t_chord(const Layout *L, const char *s, Chord *c)
{
    return chord_parse(L, s, strlen(s), c);
}

#endif
