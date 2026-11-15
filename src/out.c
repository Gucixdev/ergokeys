/* out.c - conflict detection, ranking, and the text and JSON printers. */

#define EK_MAXCONF 256
#define EK_WORST 10

typedef struct {
    u32 a[EK_MAXCONF];
    u32 b[EK_MAXCONF];
    u8 prefix[EK_MAXCONF]; /* 0 same chord, 1 one is the start of the other */
    u32 stored;
    u32 total;
} Conflicts;

/* Two shortcuts conflict when they are equal or one is the start of the other. Both share their first step, so
 * only shortcuts in the same first-step bucket are compared. Returns 0 when the arena is full. */
static int conflicts_find(Arena *a, const Layout *L, const Entries *E, Conflicts *Cf)
{
    size_t mark = arena_mark(a);
    u32 ncell = L->nkeys * EK_NMASK, i, j, c;
    u32 *head = EK_NEW(a, u32, ncell + 1u);
    u32 *next = EK_NEW(a, u32, E->n + 1u);

    memset(Cf, 0, sizeof(*Cf));
    if (head == NULL || next == NULL) {
        arena_reset(a, mark);
        return 0;
    }
    for (i = E->n; i-- > 0;) {
        c = (u32)E->chord[i].mods[0] * L->nkeys + E->chord[i].key[0];
        next[i] = head[c];
        head[c] = i + 1u;
    }
    for (c = 0; c < ncell; c++) {
        for (i = head[c]; i != 0; i = next[i - 1u]) {
            for (j = next[i - 1u]; j != 0; j = next[j - 1u]) {
                const Chord *x = &E->chord[i - 1u], *y = &E->chord[j - 1u];
                int same = chord_equal(x, y);

                if (same || chord_is_prefix(x, y) || chord_is_prefix(y, x)) {
                    if (Cf->stored < EK_MAXCONF) {
                        Cf->a[Cf->stored] = i - 1u;
                        Cf->b[Cf->stored] = j - 1u;
                        Cf->prefix[Cf->stored] = (u8)!same;
                        Cf->stored++;
                    }
                    Cf->total++;
                }
            }
        }
    }
    arena_reset(a, mark);
    return 1;
}

/* indices of the k largest scores, largest first, ties to the lower index; returns how many */
static u32 top_k(const f64 *score, u32 n, u32 *idx, u32 k)
{
    u32 cnt = 0, i, p, q, last;

    for (i = 0; i < n; i++) {
        p = cnt;
        while (p > 0 && score[idx[p - 1u]] < score[i]) {
            p--;
        }
        if (p >= k) {
            continue;
        }
        last = cnt < k ? cnt : k - 1u;
        for (q = last; q > p; q--) {
            idx[q] = idx[q - 1u];
        }
        idx[p] = i;
        if (cnt < k) {
            cnt++;
        }
    }
    return cnt;
}

/* ---- JSON helpers ---- */

static void j_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s != '\0'; s++) {
        u8 c = (u8)*s;

        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c < 0x20u) {
            fprintf(f, "\\u%04x", (unsigned)c);
        } else {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

static void j_num(FILE *f, f64 v)
{
    fprintf(f, "%.4f", v);
}

static void print_skips(FILE *f, const Report *R, int json, const char *file)
{
    u32 i;

    if (json) {
        fputs("\"skipped\":[", f);
        for (i = 0; i < R->nstored; i++) {
            fprintf(f, "%s{\"line\":%u,\"why\":", i ? "," : "", (unsigned)R->item[i].line);
            j_str(f, R->item[i].why);
            fputs(",\"text\":", f);
            j_str(f, R->item[i].ctx);
            fputc('}', f);
        }
        fputc(']', f);
        return;
    }
    for (i = 0; i < R->nstored; i++) {
        fprintf(stderr, "%s:%u: skipped: %s%s%s%s\n", file, (unsigned)R->item[i].line, R->item[i].why,
                R->item[i].ctx[0] ? " '" : "", R->item[i].ctx, R->item[i].ctx[0] ? "'" : "");
    }
    if (R->total > R->nstored) {
        fprintf(stderr, "%s: %u more skipped lines not listed\n", file, (unsigned)(R->total - R->nstored));
    }
}

static void print_conflicts(FILE *f, const Layout *L, const Entries *E, const Conflicts *Cf, int json)
{
    u32 i;
    char t[96];

    if (json) {
        fputs("\"conflicts\":[", f);
        for (i = 0; i < Cf->stored; i++) {
            chord_fmt(L, &E->chord[Cf->a[i]], t, sizeof(t));
            fprintf(f, "%s{\"chord\":", i ? "," : "");
            j_str(f, t);
            fprintf(f, ",\"a\":%u,\"b\":%u,\"prefix\":%s}", (unsigned)Cf->a[i], (unsigned)Cf->b[i],
                    Cf->prefix[i] ? "true" : "false");
        }
        fputc(']', f);
        return;
    }
    for (i = 0; i < Cf->stored; i++) {
        chord_fmt(L, &E->chord[Cf->a[i]], t, sizeof(t));
        fprintf(f, "conflict: %s%s: \"%s\" (line %u) and \"%s\" (line %u)\n", t,
                Cf->prefix[i] ? " (one is the start of the other)" : "", E->action[Cf->a[i]],
                (unsigned)E->line[Cf->a[i]], E->action[Cf->b[i]], (unsigned)E->line[Cf->b[i]]);
    }
    if (Cf->total > Cf->stored) {
        fprintf(f, "conflict: %u more not listed\n", (unsigned)(Cf->total - Cf->stored));
    }
}

/* ---- simulate ---- */

typedef struct {
    f64 *mm;
    f64 *cost;
    f64 mm_total;    /* sum of weight * mm */
    f64 cost_total;  /* sum of weight * cost */
    f64 mm_plain;    /* sum of mm, every shortcut counted once */
    f64 cost_plain;
} Sim;

static int simulate_run(Arena *a, const Layout *L, const Weights *W, const Entries *E, const Chord *ch, Sim *S)
{
    u32 i;

    S->mm = EK_NEW(a, f64, E->n + 1u);
    S->cost = EK_NEW(a, f64, E->n + 1u);
    S->mm_total = 0.0;
    S->cost_total = 0.0;
    S->mm_plain = 0.0;
    S->cost_plain = 0.0;
    if (S->mm == NULL || S->cost == NULL) {
        return 0;
    }
    for (i = 0; i < E->n; i++) {
        Cost c = shortcut_cost(L, W, &ch[i]);

        S->mm[i] = c.mm;
        S->cost[i] = c.cost;
        S->mm_total += E->weight[i] * c.mm;
        S->cost_total += E->weight[i] * c.cost;
        S->mm_plain += c.mm;
        S->cost_plain += c.cost;
    }
    return 1;
}

/* an action for a table cell: at most 56 bytes, cut on a character boundary, "..." when cut (JSON output is never cut) */
static const char *act_cell(const char *a, char *buf)
{
    size_t n = strlen(a);

    if (n <= 56u) {
        return a;
    }
    n = 53u;
    while (n > 0 && ((unsigned char)a[n] & 0xC0u) == 0x80u) {
        n--;
    }
    memcpy(buf, a, n);
    memcpy(buf + n, "...", 4u);
    return buf;
}

static void print_simulate(Arena *a, FILE *f, const Layout *L, const Entries *E, const Sim *S, const Conflicts *Cf,
                           const Report *R, int json, const char *file, int fmt)
{
    u32 i, nw, idx[EK_WORST];
    f64 *score = EK_NEW(a, f64, E->n + 1u);
    char t[96], cell[64];

    for (i = 0; score != NULL && i < E->n; i++) {
        score[i] = E->weight[i] * S->cost[i];
    }
    nw = score != NULL ? top_k(score, E->n, idx, EK_WORST) : 0;
    if (json) {
        fputs("{\"layout\":", f);
        j_str(f, L->name);
        fprintf(f, ",\"format\":\"%s\",\"entries\":[", ek_format_name[fmt]);
        for (i = 0; i < E->n; i++) {
            chord_fmt(L, &E->chord[i], t, sizeof(t));
            fprintf(f, "%s{\"action\":", i ? "," : "");
            j_str(f, E->action[i]);
            fputs(",\"chord\":", f);
            j_str(f, t);
            fputs(",\"weight\":", f);
            j_num(f, E->weight[i]);
            fputs(",\"mm\":", f);
            j_num(f, S->mm[i]);
            fputs(",\"cost\":", f);
            j_num(f, S->cost[i]);
            fputc('}', f);
        }
        fputs("],\"total\":{\"mm\":", f);
        j_num(f, S->mm_total);
        fputs(",\"cost\":", f);
        j_num(f, S->cost_total);
        fputs(",\"mm_unweighted\":", f);
        j_num(f, S->mm_plain);
        fputs(",\"cost_unweighted\":", f);
        j_num(f, S->cost_plain);
        fputs("},\"worst\":[", f);
        for (i = 0; i < nw; i++) {
            fprintf(f, "%s%u", i ? "," : "", (unsigned)idx[i]);
        }
        fputs("],", f);
        print_conflicts(f, L, E, Cf, 1);
        fprintf(f, ",\"counts\":{\"lines\":%u,\"bindings\":%u,\"skipped\":%u,\"shortcuts\":%u},", (unsigned)R->lines,
                (unsigned)R->bindings, (unsigned)R->total, (unsigned)E->n);
        print_skips(f, R, 1, file);
        fputs("}\n", f);
        return;
    }
    fprintf(f, "layout %s, %u shortcuts from %s (%s), %u lines skipped\n\n", L->name, (unsigned)E->n, file,
            ek_format_name[fmt], (unsigned)R->total);
    fprintf(f, "%8s %9s %6s  %-26s %s\n", "mm", "cost", "weight", "shortcut", "action");
    for (i = 0; i < E->n; i++) {
        chord_fmt(L, &E->chord[i], t, sizeof(t));
        fprintf(f, "%8.2f %9.2f %6.4g  %-26s %s\n", S->mm[i], S->cost[i], E->weight[i], t, act_cell(E->action[i], cell));
    }
    fprintf(f, "\ntotal weighted by use: %.2f mm, cost %.2f\ntotal of each shortcut once: %.2f mm, cost %.2f\n", S->mm_total,
            S->cost_total, S->mm_plain, S->cost_plain);
    if (nw > 0) {
        fprintf(f, "\nworst %u by weight x cost:\n", (unsigned)nw);
        for (i = 0; i < nw; i++) {
            chord_fmt(L, &E->chord[idx[i]], t, sizeof(t));
            fprintf(f, "%3u. %-26s cost %8.2f  mm %7.2f  %s\n", (unsigned)(i + 1u), t, S->cost[idx[i]],
                    S->mm[idx[i]], act_cell(E->action[idx[i]], cell));
        }
    }
    if (Cf->total > 0) {
        fputc('\n', f);
        print_conflicts(f, L, E, Cf, 0);
    }
}

/* ---- suggest ---- */

static void print_suggest(FILE *f, const Layout *L, const Entries *E, const Chord *nw, const Sim *Sb, const Sim *Sa,
                          const OptStat *St, const Conflicts *Cf, const Report *R, int json, const char *file,
                          int fmt, u32 seed, int anymods)
{
    u32 i, changed = 0;
    char t0[96], t1[96], cell[64];
    f64 pct = Sb->cost_total > 0.0 ? 100.0 * (Sb->cost_total - Sa->cost_total) / Sb->cost_total : 0.0;

    for (i = 0; i < E->n; i++) {
        changed += !chord_equal(&E->chord[i], &nw[i]);
    }
    if (json) {
        fputs("{\"layout\":", f);
        j_str(f, L->name);
        fprintf(f, ",\"format\":\"%s\",\"seed\":%u,\"mods\":\"%s\",\"changes\":[", ek_format_name[fmt],
                (unsigned)seed, anymods ? "any" : "keep");
        for (i = 0; i < E->n; i++) {
            chord_fmt(L, &E->chord[i], t0, sizeof(t0));
            chord_fmt(L, &nw[i], t1, sizeof(t1));
            fprintf(f, "%s{\"action\":", i ? "," : "");
            j_str(f, E->action[i]);
            fputs(",\"from\":", f);
            j_str(f, t0);
            fputs(",\"to\":", f);
            j_str(f, t1);
            fprintf(f, ",\"changed\":%s,\"weight\":", chord_equal(&E->chord[i], &nw[i]) ? "false" : "true");
            j_num(f, E->weight[i]);
            fputs(",\"cost_before\":", f);
            j_num(f, Sb->cost[i]);
            fputs(",\"cost_after\":", f);
            j_num(f, Sa->cost[i]);
            fputs(",\"mm_before\":", f);
            j_num(f, Sb->mm[i]);
            fputs(",\"mm_after\":", f);
            j_num(f, Sa->mm[i]);
            fputc('}', f);
        }
        fputs("],\"before\":{\"mm\":", f);
        j_num(f, Sb->mm_total);
        fputs(",\"cost\":", f);
        j_num(f, Sb->cost_total);
        fputs("},\"after\":{\"mm\":", f);
        j_num(f, Sa->mm_total);
        fputs(",\"cost\":", f);
        j_num(f, Sa->cost_total);
        fprintf(f, "},\"saved_percent\":%.2f,\"changed\":%u,\"displaced\":%u,", pct, (unsigned)changed,
                (unsigned)St->displaced);
        print_conflicts(f, L, E, Cf, 1);
        fprintf(f, ",\"counts\":{\"lines\":%u,\"bindings\":%u,\"skipped\":%u},", (unsigned)R->lines,
                (unsigned)R->bindings, (unsigned)R->total);
        print_skips(f, R, 1, file);
        fputs("}\n", f);
        return;
    }
    fprintf(f, "layout %s, %u shortcuts from %s (%s), modifiers %s, seed %u\n", L->name, (unsigned)E->n, file,
            ek_format_name[fmt], anymods ? "may change" : "kept", (unsigned)seed);
    if (Cf->total > 0) {
        print_conflicts(f, L, E, Cf, 0);
        fprintf(f, "%u actions were on a chord another action already had and were moved first\n",
                (unsigned)St->displaced);
    }
    fprintf(f, "\nbefore: cost %.2f, %.2f mm\nafter:  cost %.2f, %.2f mm  (%.1f%% lower)\n", Sb->cost_total,
            Sb->mm_total, Sa->cost_total, Sa->mm_total, pct);
    if (changed == 0) {
        fputs("\nno rebinding lowers the cost\n", f);
        return;
    }
    fprintf(f, "\n%u of %u actions change:\n", (unsigned)changed, (unsigned)E->n);
    fprintf(f, "%-26s %-26s %9s %9s  %s\n", "from", "to", "was", "now", "action");
    for (i = 0; i < E->n; i++) {
        if (chord_equal(&E->chord[i], &nw[i])) {
            continue;
        }
        chord_fmt(L, &E->chord[i], t0, sizeof(t0));
        chord_fmt(L, &nw[i], t1, sizeof(t1));
        fprintf(f, "%-26s %-26s %9.2f %9.2f  %s\n", t0, t1, Sb->cost[i], Sa->cost[i], act_cell(E->action[i], cell));
    }
}

/* ---- layout ---- */

#define EK_CANVAS_W 160
#define EK_CANVAS_H 20

/* Row of key k in the text map, counted from the topmost key. Adding a quarter pitch before truncating keeps
 * column stagger of up to half a pitch inside its row, so staggered boards are drawn as straight rows. */
static int canvas_row(const Layout *L, u32 k)
{
    f64 ymin = L->y[0];
    u32 j;

    for (j = 1; j < L->nkeys; j++) {
        if (L->y[j] < ymin) {
            ymin = L->y[j];
        }
    }
    return (int)((L->y[k] - ymin) / L->pitch + 0.25);
}

static void print_layout(FILE *f, const Layout *L, int json)
{
    u32 k;
    int fg;

    if (json) {
        fputs("{\"name\":", f);
        j_str(f, L->name);
        fputs(",\"pitch_mm\":", f);
        j_num(f, L->pitch);
        fputs(",\"keys\":[", f);
        for (k = 0; k < L->nkeys; k++) {
            fprintf(f, "%s{\"label\":", k ? "," : "");
            j_str(f, L->label[k]);
            fputs(",\"x_mm\":", f);
            j_num(f, L->x[k]);
            fputs(",\"y_mm\":", f);
            j_num(f, L->y[k]);
            fprintf(f, ",\"finger\":\"%s\",\"chordable\":%s}", ek_finger_name[L->finger[k]],
                    L->chordable[k] ? "true" : "false");
        }
        fputs("],\"fingers\":[", f);
        for (fg = 0; fg < EK_NF; fg++) {
            fprintf(f, "%s{\"finger\":\"%s\",\"home_x_mm\":", fg ? "," : "", ek_finger_name[fg]);
            j_num(f, L->hx[fg]);
            fputs(",\"home_y_mm\":", f);
            j_num(f, L->hy[fg]);
            fputc('}', f);
        }
        fputs("],\"modifiers\":{", f);
        for (fg = 0; fg < 4; fg++) {
            u32 q;

            fprintf(f, "%s\"%s\":[", fg ? "," : "", ek_mod_name[fg]);
            for (q = 0; q < L->nmod[fg]; q++) {
                fputs(q ? "," : "", f);
                j_str(f, L->label[L->modkey[fg][q]]);
            }
            fputc(']', f);
        }
        fputs("}}\n", f);
        return;
    }
    {
        char canvas[EK_CANVAS_H][EK_CANVAS_W + 1];
        int r, c;

        for (r = 0; r < EK_CANVAS_H; r++) {
            memset(canvas[r], ' ', EK_CANVAS_W);
            canvas[r][EK_CANVAS_W] = '\0';
        }
        for (k = 0; k < L->nkeys; k++) {
            f64 xu = L->x[k] / L->pitch;
            int row = canvas_row(L, k) * 2;
            int col = (int)(xu * 7.0 + 0.5);
            size_t ll = strlen(L->label[k]);
            int start = col - (int)(ll / 2u);
            size_t q;

            if (row + 1 >= EK_CANVAS_H) {
                continue;
            }
            for (q = 0; q < ll; q++) {
                if (start + (int)q >= 0 && start + (int)q < EK_CANVAS_W) {
                    canvas[row][start + (int)q] = L->label[k][q];
                }
            }
            for (q = 0; q < 2u; q++) {
                int cc = col - 1 + (int)q;

                if (cc >= 0 && cc < EK_CANVAS_W) {
                    canvas[row + 1][cc] = (char)ek_lower((u8)ek_finger_name[L->finger[k]][q]);
                }
            }
        }
        fprintf(f, "layout %s, pitch %.2f mm, %u keys; lower line = finger (l/r + p r m i t)\n\n", L->name, L->pitch,
                (unsigned)L->nkeys);
        for (r = 0; r < EK_CANVAS_H; r++) {
            c = EK_CANVAS_W;
            while (c > 0 && canvas[r][c - 1] == ' ') {
                c--;
            }
            canvas[r][c] = '\0';
            if (c > 0) {
                fprintf(f, "%s\n", canvas[r]);
            }
        }
        fprintf(f, "\n%-4s %-14s %s\n", "finger", "home (mm)", "keys");
        for (fg = 0; fg < EK_NF; fg++) {
            fprintf(f, "%-6s (%6.1f,%6.1f)  ", ek_finger_name[fg], L->hx[fg], L->hy[fg]);
            for (k = 0; k < L->nkeys; k++) {
                if (L->finger[k] == fg) {
                    fprintf(f, "%s ", L->label[k]);
                }
            }
            fputc('\n', f);
        }
        fputs("\nmodifiers:", f);
        for (fg = 0; fg < 4; fg++) {
            u32 q;

            fprintf(f, " %s=", ek_mod_name[fg]);
            for (q = 0; q < L->nmod[fg]; q++) {
                fprintf(f, "%s%s", q ? "/" : "", L->label[L->modkey[fg][q]]);
            }
        }
        fputc('\n', f);
    }
}
