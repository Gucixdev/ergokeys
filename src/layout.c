/* layout.c - a keyboard as data: key centres in millimetres, the finger that owns each key, each finger's home.
 *
 * File format (one directive per line, '#' starts a comment, labels are lowercase and contain no spaces):
 *   name NAME                          layout name
 *   pitch MM                           key pitch, 19.05 for a standard keyboard; positions below are in key units
 *   row Y X0 LABEL...                  unit-width keys in a row: key i has its left edge at X0+i, top edge at Y
 *   key LABEL X Y W [nochord]          one key of width W; nochord keeps it out of the suggestion pool
 *   finger FINGER LABEL...             FINGER is LP LR LM LI LT RT RI RM RR RP (left/right, pinky..thumb)
 *   home FINGER LABEL [X]              the finger rests on that key; X moves the rest point to x = X (units)
 *   mod ctrl|alt|super|shift LABEL...  physical keys that produce the modifier; the cheapest one is used
 * A finger landing on a key wider than one unit lands on the point of the key nearest to where the finger is, so a
 * thumb on a space bar or a pinky on a long shift costs only the distance to the nearest usable point. */

#define EK_MAXKEYS 128
#define EK_LABEL 12
#define EK_NF 10
#define EK_NMASK 16
#define EK_MAXMODKEYS 4

enum { F_LP, F_LR, F_LM, F_LI, F_LT, F_RT, F_RI, F_RM, F_RR, F_RP };
enum { M_CTRL = 1, M_ALT = 2, M_SUPER = 4, M_SHIFT = 8 };

static const char *const ek_finger_name[EK_NF] = {"LP", "LR", "LM", "LI", "LT", "RT", "RI", "RM", "RR", "RP"};
static const char *const ek_mod_name[4] = {"Ctrl", "Alt", "Super", "Shift"};

#define EK_HAND(f) ((f) >= F_RT ? 1 : 0)
#define EK_IS_THUMB(f) ((f) == F_LT || (f) == F_RT)
#define EK_IS_PINKY(f) ((f) == F_LP || (f) == F_RP)
#define EK_IS_RING(f) ((f) == F_LR || (f) == F_RR)

typedef struct {
    char name[40];
    f64 pitch;
    u32 nkeys;
    char label[EK_MAXKEYS][EK_LABEL];
    f64 x[EK_MAXKEYS];     /* centre, mm */
    f64 y[EK_MAXKEYS];
    f64 xlo[EK_MAXKEYS];   /* leftmost and rightmost point a finger may land on, mm */
    f64 xhi[EK_MAXKEYS];
    u8 finger[EK_MAXKEYS];
    u8 has_finger[EK_MAXKEYS];
    u8 chordable[EK_MAXKEYS];
    u8 nmod[4];
    u8 modkey[4][EK_MAXMODKEYS];
    f64 hx[EK_NF];         /* home point of each finger, mm */
    f64 hy[EK_NF];
    u8 has_home[EK_NF];
} Layout;

typedef struct {
    u32 line;
    const char *why;
    char ctx[48];
} LayoutErr;

static int layout_find(const Layout *L, const char *s, size_t n)
{
    u32 i;

    if (n == 0 || n >= EK_LABEL) {
        return -1;
    }
    for (i = 0; i < L->nkeys; i++) {
        if (strlen(L->label[i]) == n && memcmp(L->label[i], s, n) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int layout_finger_from_name(const char *s, size_t n)
{
    int f;

    for (f = 0; f < EK_NF; f++) {
        if (n == 2u && s[0] == ek_finger_name[f][0] && s[1] == ek_finger_name[f][1]) {
            return f;
        }
    }
    return -1;
}

static int layout_mod_from_word(const char *s, size_t n)
{
    int m;

    for (m = 0; m < 4; m++) {
        if (ek_ieq(s, n, m == 0 ? "ctrl" : m == 1 ? "alt" : m == 2 ? "super" : "shift")) {
            return m;
        }
    }
    return -1;
}

static void layout_ctx(LayoutErr *e, const char *s, size_t n)
{
    if (n > sizeof(e->ctx) - 1u) {
        n = sizeof(e->ctx) - 1u;
    }
    memcpy(e->ctx, s, n);
    e->ctx[n] = '\0';
}

static int layout_num(const char *s, size_t n, f64 *out)
{
    char tmp[32];
    char *end;

    if (n == 0 || n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    *out = strtod(tmp, &end);
    return *end == '\0' && *out > -1000.0 && *out < 1000.0;
}

static int layout_add_key(Layout *L, const char *s, size_t n, f64 xu, f64 yu, f64 w, int chordable, LayoutErr *e)
{
    u32 k = L->nkeys;

    if (n == 0 || n >= EK_LABEL) {
        e->why = "key label empty or longer than 11 characters";
        layout_ctx(e, s, n);
        return 0;
    }
    if (layout_find(L, s, n) >= 0) {
        e->why = "duplicate key label";
        layout_ctx(e, s, n);
        return 0;
    }
    if (k >= EK_MAXKEYS) {
        e->why = "more than 128 keys";
        return 0;
    }
    memcpy(L->label[k], s, n);
    L->label[k][n] = '\0';
    L->x[k] = (xu + w * 0.5) * L->pitch;
    L->y[k] = (yu + 0.5) * L->pitch;
    L->xlo[k] = (xu + 0.5) * L->pitch;
    L->xhi[k] = (xu + w - 0.5) * L->pitch;
    L->chordable[k] = (u8)chordable;
    L->nkeys = k + 1u;
    return 1;
}

#define EK_MAXTOK 80

/* Parse a layout file. Returns 1 on success; on failure fills *e (line, reason, offending text). */
static int layout_parse(const char *buf, size_t len, Layout *L, LayoutErr *e)
{
    size_t pos = 0;
    u32 line = 0;
    u32 i;
    int f;

    memset(L, 0, sizeof(*L));
    memset(e, 0, sizeof(*e));
    L->pitch = 19.05;
    strcpy(L->name, "unnamed");

    while (pos < len) {
        const char *tok[EK_MAXTOK];
        size_t tl[EK_MAXTOK];
        u32 nt = 0;
        size_t b = pos, q;

        while (pos < len && buf[pos] != '\n') {
            pos++;
        }
        q = pos;
        if (pos < len) {
            pos++;
        }
        line++;
        e->line = line;
        while (b < q) {
            size_t s;

            while (b < q && ek_is_space((u8)buf[b])) {
                b++;
            }
            if (b >= q || buf[b] == '#') {
                break;
            }
            s = b;
            while (b < q && !ek_is_space((u8)buf[b])) {
                b++;
            }
            if (nt >= EK_MAXTOK) {
                e->why = "too many tokens on one line";
                return 0;
            }
            tok[nt] = buf + s;
            tl[nt] = b - s;
            nt++;
        }
        if (nt == 0) {
            continue;
        }
        if (tl[0] == 4u && memcmp(tok[0], "name", 4) == 0) {
            if (nt != 2u || tl[1] >= sizeof(L->name)) {
                e->why = "name takes one word of at most 39 characters";
                return 0;
            }
            memcpy(L->name, tok[1], tl[1]);
            L->name[tl[1]] = '\0';
        } else if (tl[0] == 5u && memcmp(tok[0], "pitch", 5) == 0) {
            f64 v;

            if (nt != 2u || !layout_num(tok[1], tl[1], &v) || v < 5.0 || v > 100.0) {
                e->why = "pitch takes one number between 5 and 100";
                return 0;
            }
            L->pitch = v;
        } else if (tl[0] == 3u && memcmp(tok[0], "row", 3) == 0) {
            f64 yu, xu;

            if (nt < 4u || !layout_num(tok[1], tl[1], &yu) || !layout_num(tok[2], tl[2], &xu)) {
                e->why = "row takes Y X0 and at least one label";
                return 0;
            }
            for (i = 3; i < nt; i++) {
                if (!layout_add_key(L, tok[i], tl[i], xu + (f64)(i - 3u), yu, 1.0, 1, e)) {
                    return 0;
                }
            }
        } else if (tl[0] == 3u && memcmp(tok[0], "key", 3) == 0) {
            f64 xu, yu, w;
            int chord = 1;

            if ((nt != 5u && nt != 6u) || !layout_num(tok[2], tl[2], &xu) || !layout_num(tok[3], tl[3], &yu) ||
                !layout_num(tok[4], tl[4], &w) || w < 1.0 || w > 20.0) {
                e->why = "key takes LABEL X Y W (W from 1 to 20) and an optional nochord";
                return 0;
            }
            if (nt == 6u) {
                if (tl[5] != 7u || memcmp(tok[5], "nochord", 7) != 0) {
                    e->why = "unknown key option";
                    layout_ctx(e, tok[5], tl[5]);
                    return 0;
                }
                chord = 0;
            }
            if (!layout_add_key(L, tok[1], tl[1], xu, yu, w, chord, e)) {
                return 0;
            }
        } else if (tl[0] == 6u && memcmp(tok[0], "finger", 6) == 0) {
            if (nt < 3u || (f = layout_finger_from_name(tok[1], tl[1])) < 0) {
                e->why = "finger takes a finger name (LP LR LM LI LT RT RI RM RR RP) and labels";
                if (nt >= 2u) {
                    layout_ctx(e, tok[1], tl[1]);
                }
                return 0;
            }
            for (i = 2; i < nt; i++) {
                int k = layout_find(L, tok[i], tl[i]);

                if (k < 0) {
                    e->why = "finger names a key that is not defined above it";
                    layout_ctx(e, tok[i], tl[i]);
                    return 0;
                }
                L->finger[k] = (u8)f;
                L->has_finger[k] = 1;
            }
        } else if (tl[0] == 4u && memcmp(tok[0], "home", 4) == 0) {
            int k;
            f64 xu;

            if ((nt != 3u && nt != 4u) || (f = layout_finger_from_name(tok[1], tl[1])) < 0 ||
                (k = layout_find(L, tok[2], tl[2])) < 0 || (nt == 4u && !layout_num(tok[3], tl[3], &xu))) {
                e->why = "home takes FINGER LABEL [X] with a defined label";
                return 0;
            }
            L->hx[f] = nt == 4u ? xu * L->pitch : L->x[k];
            L->hy[f] = L->y[k];
            L->has_home[f] = 1;
        } else if (tl[0] == 3u && memcmp(tok[0], "mod", 3) == 0) {
            int m;

            if (nt < 3u || (m = layout_mod_from_word(tok[1], tl[1])) < 0) {
                e->why = "mod takes ctrl, alt, super or shift and labels";
                return 0;
            }
            for (i = 2; i < nt; i++) {
                int k = layout_find(L, tok[i], tl[i]);

                if (k < 0) {
                    e->why = "mod names a key that is not defined above it";
                    layout_ctx(e, tok[i], tl[i]);
                    return 0;
                }
                if (L->nmod[m] >= EK_MAXMODKEYS) {
                    e->why = "more than 4 keys for one modifier";
                    return 0;
                }
                L->modkey[m][L->nmod[m]++] = (u8)k;
                L->chordable[k] = 0;
            }
        } else {
            e->why = "unknown directive";
            layout_ctx(e, tok[0], tl[0]);
            return 0;
        }
    }

    e->line = 0;
    if (L->nkeys == 0) {
        e->why = "layout has no keys";
        return 0;
    }
    for (i = 0; i < L->nkeys; i++) {
        if (!L->has_finger[i]) {
            e->why = "key without a finger";
            layout_ctx(e, L->label[i], strlen(L->label[i]));
            return 0;
        }
    }
    for (f = 0; f < 4; f++) {
        if (L->nmod[f] == 0) {
            e->why = "no key for modifier";
            layout_ctx(e, ek_mod_name[f], strlen(ek_mod_name[f]));
            return 0;
        }
    }
    for (i = 0; i < L->nkeys; i++) {
        if (!L->has_home[L->finger[i]]) {
            e->why = "a finger is used by a key but has no home";
            layout_ctx(e, ek_finger_name[L->finger[i]], 2);
            return 0;
        }
    }
    return 1;
}

/* where a finger at x lands when it presses key k: the point of the key nearest to x, on the key's centre line */
static f64 layout_land_x(const Layout *L, u32 k, f64 fx)
{
    if (fx < L->xlo[k]) {
        return L->xlo[k];
    }
    if (fx > L->xhi[k]) {
        return L->xhi[k];
    }
    return fx;
}
