/* import.c - reading shortcuts: the own TSV format and tolerant importers for Hyprland, i3/sway and dwl config.h.
 *
 * TSV: action <TAB> chord [<TAB> weight [<TAB> fixed]]. '#' starts a comment line. weight is a positive number
 * (how often the action is used, default 1); "fixed" (or "pin") keeps the action where it is in `suggest`.
 * The importers read only lines that look like key bindings and ignore the rest of the file. A line that looks
 * like a binding but cannot be used (unknown key, unknown modifier, keycode or mouse binding) is skipped and
 * recorded in the Report with its line number and the reason. */

#define EK_ACTION_MAX 160
#define EK_MAXSKIPS 200
#define EK_MAXVARS 32
#define EK_WORK 1024

typedef struct {
    u32 n;
    u32 cap;
    const char **action;
    Chord *chord;
    f64 *weight;
    u8 *fixed;
    u32 *line;
} Entries;

typedef struct {
    u32 line;
    const char *why;
    char ctx[32];
} Skip;

typedef struct {
    Skip item[EK_MAXSKIPS];
    u32 nstored;  /* items kept (at most EK_MAXSKIPS) */
    u32 total;    /* lines skipped */
    u32 lines;    /* lines read */
    u32 bindings; /* lines that looked like a binding */
} Report;

enum { FMT_TSV, FMT_HYPR, FMT_I3, FMT_DWL, FMT_AUTO };

static const char *const ek_format_name[] = {"tsv", "hyprland", "i3", "dwl"};

static int entries_init(Arena *a, Entries *E, u32 cap)
{
    E->n = 0;
    E->cap = cap;
    E->action = EK_NEW(a, const char *, cap);
    E->chord = EK_NEW(a, Chord, cap);
    E->weight = EK_NEW(a, f64, cap);
    E->fixed = EK_NEW(a, u8, cap);
    E->line = EK_NEW(a, u32, cap);
    return E->action != NULL && E->chord != NULL && E->weight != NULL && E->fixed != NULL && E->line != NULL;
}

static void report_init(Report *R)
{
    memset(R, 0, sizeof(*R));
}

static void skip_line(Report *R, u32 line, const char *why, const char *ctx, size_t cn)
{
    R->total++;
    if (R->nstored < EK_MAXSKIPS) {
        Skip *s = &R->item[R->nstored++];

        s->line = line;
        s->why = why;
        if (cn > sizeof(s->ctx) - 1u) {
            cn = sizeof(s->ctx) - 1u;
        }
        if (ctx != NULL) {
            memcpy(s->ctx, ctx, cn);
        }
        s->ctx[ctx != NULL ? cn : 0] = '\0';
    }
}

/* action text is copied with runs of whitespace collapsed to one space; returns 0 when the entry list or arena is full */
static int entries_add(Arena *a, Entries *E, const char *act, size_t an, const Chord *c, f64 w, u8 fixed, u32 line)
{
    char tmp[EK_ACTION_MAX + 1];
    size_t o = 0, i;
    int sp = 0;

    if (E->n >= E->cap) {
        return 0;
    }
    for (i = 0; i < an && o < EK_ACTION_MAX; i++) {
        if (ek_is_space((u8)act[i]) || act[i] == '\0') {
            sp = o > 0;
            continue;
        }
        if (sp) {
            tmp[o++] = ' ';
            sp = 0;
            if (o >= EK_ACTION_MAX) {
                break;
            }
        }
        tmp[o++] = act[i];
    }
    E->action[E->n] = arena_str(a, tmp, o);
    if (E->action[E->n] == NULL) {
        return 0;
    }
    E->chord[E->n] = *c;
    E->weight[E->n] = w;
    E->fixed[E->n] = fixed;
    E->line[E->n] = line;
    E->n++;
    return 1;
}

/* next line [*b, *e) without the newline and surrounding blanks; 0 at the end of the text */
static int next_line(const char *buf, size_t len, size_t *pos, const char **b, const char **e)
{
    size_t q;

    if (*pos >= len) {
        return 0;
    }
    q = *pos;
    while (q < len && buf[q] != '\n') {
        q++;
    }
    *b = buf + *pos;
    *e = buf + q;
    *pos = q < len ? q + 1u : q;
    ek_trim(b, e);
    return 1;
}

static int starts_with(const char *b, const char *e, const char *lit)
{
    size_t n = strlen(lit);

    return (size_t)(e - b) >= n && memcmp(b, lit, n) == 0;
}

static const char *find_char(const char *b, const char *e, char c)
{
    while (b < e && *b != c) {
        b++;
    }
    return b;
}

static int contains(const char *b, const char *e, const char *lit)
{
    size_t n = strlen(lit);

    for (; (size_t)(e - b) >= n; b++) {
        if (memcmp(b, lit, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* drop a trailing " # comment" (a '#' preceded by blank) */
static const char *strip_comment(const char *b, const char *e)
{
    const char *p;

    for (p = b; p < e; p++) {
        if (*p == '#' && (p == b || ek_is_space((u8)p[-1]))) {
            return p;
        }
    }
    return e;
}

/* ---- variables: "$mainMod = SUPER" (Hyprland), "set $mod Mod4" (i3) ---- */

typedef struct {
    u32 n;
    char name[EK_MAXVARS][32];
    char val[EK_MAXVARS][64];
} Vars;

static void vars_set(Vars *V, const char *nb, size_t nn, const char *vb, size_t vn)
{
    u32 i;

    if (nn == 0 || nn >= 32u || vn >= 64u) {
        return;
    }
    for (i = 0; i < V->n; i++) {
        if (strlen(V->name[i]) == nn && memcmp(V->name[i], nb, nn) == 0) {
            break;
        }
    }
    if (i == V->n) {
        if (V->n >= EK_MAXVARS) {
            return;
        }
        V->n++;
    }
    memcpy(V->name[i], nb, nn);
    V->name[i][nn] = '\0';
    memcpy(V->val[i], vb, vn);
    V->val[i][vn] = '\0';
}

/* copy [b,e) to out replacing "$name" by its value (longest name first); returns the length, or -1 when it does not fit */
static int vars_expand(const Vars *V, const char *b, const char *e, char *out, size_t cap)
{
    size_t o = 0;

    while (b < e) {
        u32 i, best = V->n;
        size_t bl = 0;

        if (*b == '$') {
            for (i = 0; i < V->n; i++) {
                size_t nl = strlen(V->name[i]);

                if (nl > bl && (size_t)(e - b) >= nl && memcmp(b, V->name[i], nl) == 0) {
                    best = i;
                    bl = nl;
                }
            }
        }
        if (best < V->n) {
            size_t vl = strlen(V->val[best]);

            if (o + vl >= cap) {
                return -1;
            }
            memcpy(out + o, V->val[best], vl);
            o += vl;
            b += bl;
        } else {
            if (o + 1u >= cap) {
                return -1;
            }
            out[o++] = *b++;
        }
    }
    out[o] = '\0';
    return (int)o;
}

/* "SUPER SHIFT", "SUPER_SHIFT", "Ctrl+Alt", "A|B": words split at blanks, '_', '+', '|'. Returns NULL on success. */
static const char *mods_from_list(const char *b, const char *e, u8 *mask, const char **bad, size_t *badn)
{
    *mask = 0;
    while (b < e) {
        const char *w;
        u8 m;

        while (b < e && (ek_is_space((u8)*b) || *b == '_' || *b == '+' || *b == '|')) {
            b++;
        }
        w = b;
        while (b < e && !ek_is_space((u8)*b) && *b != '_' && *b != '+' && *b != '|') {
            b++;
        }
        if (b > w) {
            m = mod_from_word(w, (size_t)(b - w));
            if (m == 0) {
                *bad = w;
                *badn = (size_t)(b - w);
                return "unknown modifier";
            }
            *mask |= m;
        }
    }
    return NULL;
}

/* ---- TSV ---- */

static int import_tsv(Arena *a, const Layout *L, const char *buf, size_t len, Entries *E, Report *R)
{
    size_t pos = 0;
    const char *b, *e;
    u32 line = 0;

    while (next_line(buf, len, &pos, &b, &e)) {
        const char *t1, *t2, *t3;
        const char *ab, *ae, *cb, *ce;
        Chord c;
        f64 w = 1.0;
        u8 fixed = 0;
        const char *why;

        line++;
        R->lines = line;
        if (b >= e || *b == '#') {
            continue;
        }
        R->bindings++;
        t1 = find_char(b, e, '\t');
        if (t1 >= e) {
            skip_line(R, line, "no TAB between action and chord", NULL, 0);
            continue;
        }
        ab = b;
        ae = t1;
        t2 = find_char(t1 + 1, e, '\t');
        cb = t1 + 1;
        ce = t2;
        ek_trim(&ab, &ae);
        ek_trim(&cb, &ce);
        if (ab >= ae) {
            skip_line(R, line, "empty action", NULL, 0);
            continue;
        }
        why = chord_parse(L, cb, (size_t)(ce - cb), &c);
        if (why != NULL) {
            skip_line(R, line, why, cb, (size_t)(ce - cb));
            continue;
        }
        if (t2 < e) {
            const char *wb = t2 + 1, *we;
            char tmp[32];
            char *end;

            t3 = find_char(wb, e, '\t');
            we = t3;
            ek_trim(&wb, &we);
            if (wb < we) {
                if ((size_t)(we - wb) >= sizeof(tmp)) {
                    skip_line(R, line, "bad weight", NULL, 0);
                    continue;
                }
                memcpy(tmp, wb, (size_t)(we - wb));
                tmp[we - wb] = '\0';
                w = strtod(tmp, &end);
                if (*end != '\0' || !(w > 0.0) || w > 1e9) {
                    skip_line(R, line, "weight must be a positive number", tmp, strlen(tmp));
                    continue;
                }
            }
            if (t3 < e) {
                const char *fb = t3 + 1, *fe = e;

                ek_trim(&fb, &fe);
                if (fb < fe) {
                    if (ek_ieq(fb, (size_t)(fe - fb), "fixed") || ek_ieq(fb, (size_t)(fe - fb), "pin")) {
                        fixed = 1;
                    } else {
                        skip_line(R, line, "fourth column must be fixed or pin", fb, (size_t)(fe - fb));
                        continue;
                    }
                }
            }
        }
        if (!entries_add(a, E, ab, (size_t)(ae - ab), &c, w, fixed, line)) {
            return 0;
        }
    }
    return 1;
}

/* ---- Hyprland: bind[flags] = MODS, KEY, DISPATCHER, args ---- */

static int is_hypr_bind(const char *b, const char *e, const char **rhs)
{
    const char *p = b;

    if (!starts_with(b, e, "bind")) {
        return 0;
    }
    p += 4;
    while (p < e && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
        p++;
    }
    while (p < e && ek_is_space((u8)*p)) {
        p++;
    }
    if (p >= e || *p != '=') {
        return 0;
    }
    *rhs = p + 1;
    return 1;
}

static int import_hyprland(Arena *a, const Layout *L, const char *buf, size_t len, Entries *E, Report *R)
{
    size_t pos = 0;
    const char *b, *e, *rhs;
    u32 line = 0;
    Vars V;

    V.n = 0;
    while (next_line(buf, len, &pos, &b, &e)) {
        char work[EK_WORK];
        const char *f0, *f1, *f2, *fe;
        const char *bad = NULL, *why;
        size_t badn = 0;
        u8 mods, key, shift;
        Chord c;
        int n;

        line++;
        R->lines = line;
        if (b >= e || *b == '#') {
            continue;
        }
        if (*b == '$') {
            const char *nb = b, *ne = b, *vb, *ve;

            while (ne < e && !ek_is_space((u8)*ne) && *ne != '=') {
                ne++;
            }
            vb = ne;
            while (vb < e && (ek_is_space((u8)*vb) || *vb == '=')) {
                vb++;
            }
            ve = strip_comment(vb, e);
            ek_trim(&vb, &ve);
            vars_set(&V, nb, (size_t)(ne - nb), vb, (size_t)(ve - vb));
            continue;
        }
        if (!is_hypr_bind(b, e, &rhs)) {
            continue;
        }
        R->bindings++;
        e = strip_comment(rhs, e);
        n = vars_expand(&V, rhs, e, work, sizeof(work));
        if (n < 0) {
            skip_line(R, line, "line too long", NULL, 0);
            continue;
        }
        f0 = work;
        fe = work + n;
        f1 = find_char(f0, fe, ',');
        if (f1 >= fe) {
            skip_line(R, line, "bind needs MODS, KEY, DISPATCHER", NULL, 0);
            continue;
        }
        f1++;
        f2 = find_char(f1, fe, ',');
        if (f2 >= fe) {
            skip_line(R, line, "bind needs MODS, KEY, DISPATCHER", NULL, 0);
            continue;
        }
        {
            const char *mb = f0, *me = f1 - 1;
            const char *kb = f1, *ke = f2;

            f2++;
            why = mods_from_list(mb, me, &mods, &bad, &badn);
            if (why != NULL) {
                skip_line(R, line, why, bad, badn);
                continue;
            }
            ek_trim(&kb, &ke);
            if (starts_with(kb, ke, "code:") || starts_with(kb, ke, "mouse:") || starts_with(kb, ke, "mouse_")) {
                skip_line(R, line, "keycode and mouse bindings are not supported", kb, (size_t)(ke - kb));
                continue;
            }
            if (!key_resolve(L, kb, (size_t)(ke - kb), &key, &shift)) {
                skip_line(R, line, "unknown key", kb, (size_t)(ke - kb));
                continue;
            }
        }
        memset(&c, 0, sizeof(c));
        c.nsteps = 1;
        c.mods[0] = (u8)(mods | (shift ? M_SHIFT : 0));
        c.key[0] = key;
        {
            /* action: dispatcher and its arguments, commas turned into blanks */
            char act[EK_WORK];
            size_t o = 0, i;

            for (i = 0; f2 + i < fe && o + 1u < sizeof(act); i++) {
                act[o++] = f2[i] == ',' ? ' ' : f2[i];
            }
            if (!entries_add(a, E, act, o, &c, 1.0, 0, line)) {
                return 0;
            }
        }
    }
    return 1;
}

/* ---- i3 and sway: bindsym [--flags] MOD+MOD+key command ---- */

static int import_i3(Arena *a, const Layout *L, const char *buf, size_t len, Entries *E, Report *R)
{
    size_t pos = 0;
    const char *b, *e;
    u32 line = 0, depth = 0;
    Vars V;

    V.n = 0;
    while (next_line(buf, len, &pos, &b, &e)) {
        char work[EK_WORK];
        const char *p, *cb, *ce, *why;
        Chord c;
        int n;
        u8 mods, key;

        line++;
        R->lines = line;
        if (b >= e || *b == '#') {
            continue;
        }
        if (*b == '}') {
            if (depth > 0) {
                depth--;
            }
            continue;
        }
        if (e[-1] == '{') {
            depth++;
            continue;
        }
        if (depth > 0) {
            if (starts_with(b, e, "bindsym") || starts_with(b, e, "bindcode")) {
                R->bindings++;
                skip_line(R, line, "binding inside a mode block is a separate key table", NULL, 0);
            }
            continue;
        }
        if (starts_with(b, e, "set ") || starts_with(b, e, "set\t")) {
            const char *nb = b + 3, *ne, *vb, *ve = e;

            ek_trim(&nb, &ve);
            ne = nb;
            while (ne < ve && !ek_is_space((u8)*ne)) {
                ne++;
            }
            vb = ne;
            while (vb < ve && ek_is_space((u8)*vb)) {
                vb++;
            }
            if (nb < ve && *nb == '$') {
                vars_set(&V, nb, (size_t)(ne - nb), vb, (size_t)(ve - vb));
            }
            continue;
        }
        if (starts_with(b, e, "bindcode")) {
            R->bindings++;
            skip_line(R, line, "keycode bindings are not supported", NULL, 0);
            continue;
        }
        if (!starts_with(b, e, "bindsym") || (size_t)(e - b) < 8u || !ek_is_space((u8)b[7])) {
            continue;
        }
        R->bindings++;
        p = b + 7;
        for (;;) {
            while (p < e && ek_is_space((u8)*p)) {
                p++;
            }
            if (p + 1 < e && p[0] == '-' && p[1] == '-') {
                while (p < e && !ek_is_space((u8)*p)) {
                    p++;
                }
                continue;
            }
            break;
        }
        cb = p;
        while (p < e && !ek_is_space((u8)*p)) {
            p++;
        }
        ce = p;
        while (p < e && ek_is_space((u8)*p)) {
            p++;
        }
        if (cb >= ce || p >= e) {
            skip_line(R, line, "bindsym needs a chord and a command", NULL, 0);
            continue;
        }
        n = vars_expand(&V, cb, ce, work, sizeof(work));
        if (n < 0) {
            skip_line(R, line, "line too long", NULL, 0);
            continue;
        }
        memset(&c, 0, sizeof(c));
        why = step_parse(L, work, (size_t)n, &mods, &key);
        if (why != NULL) {
            skip_line(R, line, why, work, (size_t)n);
            continue;
        }
        c.nsteps = 1;
        c.mods[0] = mods;
        c.key[0] = key;
        if (!entries_add(a, E, p, (size_t)(e - p), &c, 1.0, 0, line)) {
            return 0;
        }
    }
    return 1;
}

/* ---- dwl config.h: { MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit, {0} } and TAGKEYS(KEY, SKEY, TAG) ---- */

/* WLR_MODIFIER_* and MODKEY words joined by '|' (or 0). Returns NULL on success. */
static const char *dwl_mods(const char *b, const char *e, u8 modkey, u8 *mask, const char **bad, size_t *badn)
{
    *mask = 0;
    while (b < e) {
        const char *w = b, *we;

        we = find_char(b, e, '|');
        b = we < e ? we + 1 : we;
        ek_trim(&w, &we);
        if (w >= we) {
            continue;
        }
        if ((size_t)(we - w) == 1u && *w == '0') {
            continue;
        }
        if (ek_ieq(w, (size_t)(we - w), "modkey")) {
            *mask |= modkey;
        } else if (ek_ieq(w, (size_t)(we - w), "wlr_modifier_shift")) {
            *mask |= M_SHIFT;
        } else if (ek_ieq(w, (size_t)(we - w), "wlr_modifier_ctrl")) {
            *mask |= M_CTRL;
        } else if (ek_ieq(w, (size_t)(we - w), "wlr_modifier_alt")) {
            *mask |= M_ALT;
        } else if (ek_ieq(w, (size_t)(we - w), "wlr_modifier_logo")) {
            *mask |= M_SUPER;
        } else {
            *bad = w;
            *badn = (size_t)(we - w);
            return "unknown modifier";
        }
    }
    return NULL;
}

/* XKB_KEY_name -> key. A single uppercase letter is the letter with Shift. Returns NULL on success. */
static const char *dwl_key(const Layout *L, const char *b, const char *e, u8 *key, u8 *shift)
{
    ek_trim(&b, &e);
    if (!starts_with(b, e, "XKB_KEY_")) {
        return "key is not an XKB_KEY_ name";
    }
    b += 8;
    if (e - b == 1 && *b >= 'A' && *b <= 'Z') {
        char low = (char)ek_lower((u8)*b);
        int k = layout_find(L, &low, 1);

        if (k < 0) {
            return "unknown key";
        }
        *key = (u8)k;
        *shift = 1;
        return NULL;
    }
    return key_resolve(L, b, (size_t)(e - b), key, shift) ? NULL : "unknown key";
}

static int import_dwl(Arena *a, const Layout *L, const char *buf, size_t len, Entries *E, Report *R)
{
    size_t pos = 0;
    const char *b, *e;
    u32 line = 0;
    u8 modkey = M_ALT;

    while (next_line(buf, len, &pos, &b, &e)) {
        line++;
        R->lines = line;
        if (b >= e) {
            continue;
        }
        if (starts_with(b, e, "#define MODKEY")) {
            const char *p = b + 14, *bad = NULL;
            size_t badn = 0;

            if (e - p > 0 && !ek_is_space((u8)*p)) {
                continue;
            }
            ek_trim(&p, &e);
            if (dwl_mods(p, e, M_ALT, &modkey, &bad, &badn) != NULL || modkey == 0) {
                modkey = M_ALT;
            }
            continue;
        }
        if (starts_with(b, e, "TAGKEYS(")) {
            const char *p = b + 8, *a1e, *a2b, *a2e, *a3b, *a3e, *close;
            static const char *const act[4] = {"view", "toggleview", "tag", "toggletag"};
            static const u8 add[4] = {0, M_CTRL, M_SHIFT, M_CTRL | M_SHIFT};
            u32 i;
            Chord c;
            u8 key1, sh1, key2, sh2;
            const char *why1, *why2;

            R->bindings++;
            close = find_char(p, e, ')');
            a1e = find_char(p, close, ',');
            a2b = a1e < close ? a1e + 1 : close;
            a2e = find_char(a2b, close, ',');
            a3b = a2e < close ? a2e + 1 : close;
            a3e = close;
            if (a1e >= close || a2e >= close) {
                skip_line(R, line, "TAGKEYS needs KEY, SKEY, TAG", NULL, 0);
                continue;
            }
            why1 = dwl_key(L, p, a1e, &key1, &sh1);
            why2 = dwl_key(L, a2b, a2e, &key2, &sh2);
            if (why1 != NULL || why2 != NULL) {
                skip_line(R, line, why1 != NULL ? why1 : why2, NULL, 0);
                continue;
            }
            ek_trim(&a3b, &a3e);
            for (i = 0; i < 4u; i++) {
                char text[64];
                size_t o = strlen(act[i]);

                memset(&c, 0, sizeof(c));
                c.nsteps = 1;
                c.mods[0] = (u8)(modkey | add[i]);
                c.key[0] = i < 2u ? key1 : key2;
                if (i < 2u ? sh1 : sh2) {
                    c.mods[0] |= M_SHIFT;
                }
                memcpy(text, act[i], o);
                text[o++] = ' ';
                if ((size_t)(a3e - a3b) < sizeof(text) - o) {
                    memcpy(text + o, a3b, (size_t)(a3e - a3b));
                    o += (size_t)(a3e - a3b);
                }
                if (!entries_add(a, E, text, o, &c, 1.0, 0, line)) {
                    return 0;
                }
            }
            continue;
        }
        if (*b == '{') {
            const char *ib = b + 1, *ie = e, *m1, *k0, *k1, *rest;
            const char *bad = NULL, *why;
            size_t badn = 0;
            u8 mods, key, shift;
            Chord c;

            m1 = find_char(ib, ie, ',');
            if (m1 >= ie) {
                continue;
            }
            k0 = m1 + 1;
            k1 = find_char(k0, ie, ',');
            if (k1 >= ie) {
                continue;
            }
            {
                const char *kb = k0, *ke = k1;

                ek_trim(&kb, &ke);
                if (!starts_with(kb, ke, "XKB_KEY_")) {
                    continue; /* button tables and other C */
                }
            }
            R->bindings++;
            why = dwl_mods(ib, m1, modkey, &mods, &bad, &badn);
            if (why != NULL) {
                skip_line(R, line, why, bad, badn);
                continue;
            }
            why = dwl_key(L, k0, k1, &key, &shift);
            if (why != NULL) {
                const char *kb = k0, *ke = k1;

                ek_trim(&kb, &ke);
                skip_line(R, line, why, kb + 8, (size_t)(ke - kb) > 8u ? (size_t)(ke - kb) - 8u : 0);
                continue;
            }
            rest = k1 + 1;
            ie = e;
            while (ie > rest && (ek_is_space((u8)ie[-1]) || ie[-1] == ',')) {
                ie--;
            }
            if (ie > rest && ie[-1] == '}') {
                ie--;
            }
            memset(&c, 0, sizeof(c));
            c.nsteps = 1;
            c.mods[0] = (u8)(mods | (shift ? M_SHIFT : 0));
            c.key[0] = key;
            if (!entries_add(a, E, rest, (size_t)(ie - rest), &c, 1.0, 0, line)) {
                return 0;
            }
        }
    }
    return 1;
}

/* ---- format detection and dispatch ---- */

static int detect_format(const char *buf, size_t len)
{
    size_t pos = 0;
    const char *b, *e, *rhs;
    u32 hypr = 0, i3 = 0, dwl = 0, tsv = 0;

    while (next_line(buf, len, &pos, &b, &e)) {
        if (b >= e || *b == '#') {
            if (b < e && starts_with(b, e, "#define MODKEY")) {
                dwl++;
            }
            continue;
        }
        if (starts_with(b, e, "bindsym") || starts_with(b, e, "bindcode")) {
            i3++;
        } else if (is_hypr_bind(b, e, &rhs)) {
            hypr++;
        } else if (starts_with(b, e, "TAGKEYS(") || (*b == '{' && contains(b, e, "XKB_KEY_"))) {
            dwl++;
        } else if (find_char(b, e, '\t') < e) {
            tsv++;
        }
    }
    if (i3 > hypr && i3 > dwl && i3 > tsv) {
        return FMT_I3;
    }
    if (hypr > dwl && hypr > tsv) {
        return FMT_HYPR;
    }
    if (dwl > tsv) {
        return FMT_DWL;
    }
    return FMT_TSV;
}

static int import_any(int fmt, Arena *a, const Layout *L, const char *buf, size_t len, Entries *E, Report *R)
{
    switch (fmt) {
    case FMT_HYPR: return import_hyprland(a, L, buf, len, E, R);
    case FMT_I3: return import_i3(a, L, buf, len, E, R);
    case FMT_DWL: return import_dwl(a, L, buf, len, E, R);
    default: return import_tsv(a, L, buf, len, E, R);
    }
}

/* upper bound of entries a text can yield: every line, TAGKEYS lines four times */
static u32 import_capacity(const char *buf, size_t len)
{
    size_t i, lines = 1;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            lines++;
        }
    }
    return (u32)(lines * 4u + 4u);
}
