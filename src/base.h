/* base.h - fixed-size types, the arena, and the few numeric helpers the model needs.
 *
 * Types: o/u/i/f families with their bit width. Sizes are pinned by compile-time asserts.
 * Arena: one block, a bump pointer. Everything a run allocates (file text, layout, entries, optimizer tables) lives
 * in it and is released together. There is no malloc in src/. With -DEK_ASAN every allocation is surrounded by
 * poisoned bytes so AddressSanitizer sees an overflow inside the arena.
 * ek_sqrt and ek_exp are written out instead of taken from libm: results are then identical on every IEEE-754
 * host, which makes a fixed seed reproduce the same suggestion everywhere, and the binary needs only libc. */
#ifndef EK_BASE_H
#define EK_BASE_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef __UINT8_TYPE__  u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__  i32;
typedef double          f64;

typedef char ek_assert_u8[sizeof(u8) == 1 ? 1 : -1];
typedef char ek_assert_u16[sizeof(u16) == 2 ? 1 : -1];
typedef char ek_assert_u32[sizeof(u32) == 4 ? 1 : -1];
typedef char ek_assert_i32[sizeof(i32) == 4 ? 1 : -1];
typedef char ek_assert_f64[sizeof(f64) == 8 ? 1 : -1];

/* exit codes of the binary */
#define EK_OK     0 /* done */
#define EK_USAGE  1 /* bad command line */
#define EK_IO     2 /* a file could not be read, or a layout, config or output file is invalid */
#define EK_STRICT 3 /* --strict: input lines were skipped, or chords conflict */
#define EK_NOSOL  4 /* suggest found no conflict-free assignment */

#ifndef EK_ARENA_BYTES
#define EK_ARENA_BYTES (64u * 1024u * 1024u)
#endif

#ifdef EK_ASAN
#include <sanitizer/asan_interface.h>
#define EK_POISON(p, n)   __asan_poison_memory_region((p), (n))
#define EK_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#define EK_POISON(p, n)   ((void)(p), (void)(n))
#define EK_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

typedef struct {
    u8 *base;
    size_t cap;
    size_t used;
} Arena;

static void arena_init(Arena *a, void *mem, size_t cap)
{
    a->base = (u8 *)mem;
    a->cap = cap;
    a->used = 0;
    EK_POISON(a->base, a->cap);
}

/* zeroed, 8-byte aligned; NULL when the arena is full */
static void *arena_alloc(Arena *a, size_t n)
{
    size_t start = (a->used + 7u) & ~(size_t)7u;
    size_t need = n + 8u; /* 8 poisoned bytes follow every block */
    void *p;

    if (n > a->cap || start > a->cap - n || a->cap - start - n < 8u) {
        return NULL;
    }
    p = a->base + start;
    a->used = start + need;
    if (a->used > a->cap) {
        a->used = a->cap;
    }
    EK_UNPOISON(p, n);
    memset(p, 0, n);
    return p;
}

static size_t arena_mark(const Arena *a)
{
    return a->used;
}

static void arena_reset(Arena *a, size_t mark)
{
    if (mark < a->used) {
        EK_POISON(a->base + mark, a->used - mark);
        a->used = mark;
    }
}

#define EK_NEW(a, T, n) ((T *)arena_alloc((a), sizeof(T) * (size_t)(n)))

/* copy n bytes of s into the arena as a NUL-terminated string; NULL when full */
static char *arena_str(Arena *a, const char *s, size_t n)
{
    char *p = (char *)arena_alloc(a, n + 1u);

    if (p != NULL) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

/* ---- numeric helpers ---- */

/* Newton from above: the start (x+1)/2 is never below sqrt(x), and the sequence decreases until it settles. */
static f64 ek_sqrt(f64 x)
{
    f64 g, h;
    u32 i;

    if (!(x > 0.0)) {
        return 0.0;
    }
    g = (x + 1.0) * 0.5;
    for (i = 0; i < 200u; i++) {
        h = (g + x / g) * 0.5;
        if (h >= g) {
            break;
        }
        g = h;
    }
    return g;
}

/* exp(-x) for x >= 0: halve x until it is below 0.5, Taylor series, square back. Returns 0 past x = 40. */
static f64 ek_expneg(f64 x)
{
    f64 term = 1.0, sum = 1.0;
    u32 k, i;

    if (x <= 0.0) {
        return 1.0;
    }
    if (x > 40.0) {
        return 0.0;
    }
    for (k = 0; x > 0.5; k++) {
        x *= 0.5;
    }
    for (i = 1; i <= 12u; i++) {
        term *= -x / (f64)i;
        sum += term;
    }
    while (k-- > 0) {
        sum *= sum;
    }
    return sum;
}

/* xorshift32; seed 0 is replaced by a fixed non-zero value */
typedef struct {
    u32 s;
} Rng;

static void rng_seed(Rng *r, u32 seed)
{
    r->s = seed != 0u ? seed : 2463534242u;
}

static u32 rng_next(Rng *r)
{
    u32 x = r->s;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

static u32 rng_below(Rng *r, u32 n)
{
    return rng_next(r) % n;
}

static f64 rng_unit(Rng *r)
{
    return (f64)(rng_next(r) >> 8) / 16777216.0;
}

/* ---- text helpers (C89 has no strcasecmp) ---- */

static int ek_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ek_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/* case-insensitive compare of s[0..n) with the NUL-terminated lowercase string lit */
static int ek_ieq(const char *s, size_t n, const char *lit)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (lit[i] == '\0' || ek_lower((u8)s[i]) != (u8)lit[i]) {
            return 0;
        }
    }
    return lit[n] == '\0';
}

/* trim in place of a [b, e) window */
static void ek_trim(const char **b, const char **e)
{
    while (*b < *e && ek_is_space((u8)**b)) {
        (*b)++;
    }
    while (*e > *b && ek_is_space((u8)(*e)[-1])) {
        (*e)--;
    }
}

#endif
