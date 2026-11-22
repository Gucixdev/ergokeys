/* ergokeys - measure the ergonomics of keyboard shortcuts and propose cheaper bindings.
 *
 * HELP-BEGIN
 * usage: ergokeys SUBCOMMAND [OPTIONS] [ARGS]
 *
 * subcommands:
 *   simulate [FILE|PROGRAM]  cost and finger travel (mm) of every shortcut in FILE, or in the config of PROGRAM
 *                   (hyprland, sway, i3, dwl; no argument: the first of them found), the total, the worst ten
 *   score KEYS...   the same for shortcuts given on the command line; quote a sequence: "C-x C-s"
 *   suggest [FILE|PROGRAM]  propose rebindings that lower the total cost; no two actions share a chord
 *   layout [NAME]   show a keyboard layout and its finger map (default qwerty-ansi)
 *   span KEY KEY    distance in mm between two keys of the layout: measure your hand with it (hand_span, finger_reach)
 *   config          print every weight with its default and meaning, as a config file
 *   help            this text
 *
 * input formats (FILE, detected from the content unless --format is given):
 *   tsv        action TAB chord [TAB weight [TAB fixed]]; '#' comments; chords like Ctrl+Shift+t, Super+Return,
 *              C-x C-s (a sequence of steps separated by blanks)
 *   hyprland   bind[flags] = MODS, KEY, dispatcher, args   ($variables are expanded)
 *   i3         bindsym [--flags] $mod+Shift+q command      (i3 and sway; set $var is expanded)
 *   dwl        { MODKEY, XKB_KEY_p, spawn, {...} } and TAGKEYS(...) in config.h
 * Lines that look like bindings but cannot be used are skipped and listed on stderr with the reason.
 *
 * options:
 *   -f, --format NAME   tsv, hyprland, i3, dwl or auto (default auto)
 *   -l, --layout NAME   layout name or path to a .layout file (default qwerty-ansi)
 *   -L, --layouts DIR   directory searched first for NAME.layout
 *   -c, --config FILE   weights file; see `ergokeys config`
 *   -o, --out FILE      suggest: write the proposed bindings as TSV (refuses to overwrite)
 *       --seed N        suggest: seed of the search
 *       --iters N       suggest: annealing steps, 0 for greedy swaps only
 *       --mods keep|any suggest: keep each action's modifiers (default) or allow any modifier set from the input
 *       --json          machine-readable output on stdout
 *       --strict        exit 3 when lines were skipped or chords conflict
 *   -h, --help          this text
 *       --version       print the version
 *
 * environment: ERGOKEYS_LAYOUTS is searched for layouts after -L and before ./layouts.
 *
 * exit status: 0 done, 1 bad command line, 2 a file could not be read or is invalid, 3 --strict found skipped
 * lines or conflicts, 4 suggest found no conflict-free assignment.
 * HELP-END
 */

#include "ek.c"
#include "help.inc"

#define EK_VERSION "0.1.0"

#ifndef EK_DATADIR
#define EK_DATADIR "/usr/local/share/ergokeys"
#endif

static void print_help(FILE *f)
{
    int i;

    for (i = 0; ek_help_lines[i] != NULL; i++) {
        fputs(ek_help_lines[i], f);
        fputc('\n', f);
    }
}

static f64 g_mem[EK_ARENA_BYTES / sizeof(f64)];

typedef struct {
    int json;
    int strict;
    int fmt;
    int mods;      /* -1 unset */
    long iters;    /* -1 unset */
    long seed;     /* -1 unset */
    const char *layout;
    const char *ldir;
    const char *conf;
    const char *out;
    const char *pos[256];
    int npos;
} Args;

static int usage_err(const char *msg, const char *arg)
{
    fprintf(stderr, "ergokeys: %s%s%s\ntry 'ergokeys help'\n", msg, arg != NULL ? ": " : "", arg != NULL ? arg : "");
    return EK_USAGE;
}

static int parse_args(int argc, char **argv, Args *A)
{
    int i;

    memset(A, 0, sizeof(*A));
    A->fmt = FMT_AUTO;
    A->mods = -1;
    A->iters = -1;
    A->seed = -1;
    A->layout = "qwerty-ansi";
    for (i = 2; i < argc; i++) {
        const char *s = argv[i];
        const char *val = NULL;
        int takes = 0;

        if (strcmp(s, "--") == 0) {
            for (i++; i < argc; i++) {
                if (A->npos < 256) {
                    A->pos[A->npos++] = argv[i];
                }
            }
            break;
        }
        if (s[0] != '-' || s[1] == '\0') {
            if (A->npos >= 256) {
                return usage_err("too many arguments", NULL);
            }
            A->pos[A->npos++] = s;
            continue;
        }
        if (strcmp(s, "--json") == 0) {
            A->json = 1;
            continue;
        }
        if (strcmp(s, "--strict") == 0) {
            A->strict = 1;
            continue;
        }
        if (strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0) {
            print_help(stdout);
            return -1;
        }
        if (strcmp(s, "--version") == 0) {
            printf("ergokeys %s\n", EK_VERSION);
            return -1;
        }
        takes = strcmp(s, "-f") == 0 || strcmp(s, "--format") == 0 || strcmp(s, "-l") == 0 ||
                strcmp(s, "--layout") == 0 || strcmp(s, "-L") == 0 || strcmp(s, "--layouts") == 0 ||
                strcmp(s, "-c") == 0 || strcmp(s, "--config") == 0 || strcmp(s, "-o") == 0 ||
                strcmp(s, "--out") == 0 || strcmp(s, "--seed") == 0 || strcmp(s, "--iters") == 0 ||
                strcmp(s, "--mods") == 0;
        if (!takes) {
            return usage_err("unknown option", s);
        }
        if (i + 1 >= argc) {
            return usage_err("option needs a value", s);
        }
        val = argv[++i];
        if (strcmp(s, "-f") == 0 || strcmp(s, "--format") == 0) {
            int f;

            A->fmt = -1;
            if (strcmp(val, "auto") == 0) {
                A->fmt = FMT_AUTO;
            }
            for (f = 0; f < 4; f++) {
                if (strcmp(val, ek_format_name[f]) == 0) {
                    A->fmt = f;
                }
            }
            if (strcmp(val, "sway") == 0) {
                A->fmt = FMT_I3;
            }
            if (A->fmt < 0) {
                return usage_err("unknown format", val);
            }
        } else if (strcmp(s, "-l") == 0 || strcmp(s, "--layout") == 0) {
            A->layout = val;
        } else if (strcmp(s, "-L") == 0 || strcmp(s, "--layouts") == 0) {
            A->ldir = val;
        } else if (strcmp(s, "-c") == 0 || strcmp(s, "--config") == 0) {
            A->conf = val;
        } else if (strcmp(s, "-o") == 0 || strcmp(s, "--out") == 0) {
            A->out = val;
        } else if (strcmp(s, "--mods") == 0) {
            if (strcmp(val, "keep") == 0) {
                A->mods = 0;
            } else if (strcmp(val, "any") == 0) {
                A->mods = 1;
            } else {
                return usage_err("--mods takes keep or any", val);
            }
        } else {
            char *end;
            long v = strtol(val, &end, 10);

            if (*end != '\0' || v < 0 || v > 1000000000L) {
                return usage_err("not a non-negative number", val);
            }
            if (strcmp(s, "--seed") == 0) {
                A->seed = v;
            } else {
                A->iters = v;
            }
        }
    }
    return 0;
}

/* whole file into the arena, NUL-terminated; NULL with a message on stderr */
static char *read_file(Arena *a, const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;

    if (f == NULL) {
        fprintf(stderr, "ergokeys: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "ergokeys: cannot read %s\n", path);
        fclose(f);
        return NULL;
    }
    if ((size_t)sz > a->cap / 8u) {
        fprintf(stderr, "ergokeys: %s is too large (limit %lu bytes)\n", path, (unsigned long)(a->cap / 8u));
        fclose(f);
        return NULL;
    }
    buf = (char *)arena_alloc(a, (size_t)sz + 1u);
    if (buf == NULL || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "ergokeys: cannot read %s\n", path);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

/* dir + "/" + name + ".layout" into out; 0 when it does not fit */
static int layout_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);

    if (dl + nl + 9u > cap) {
        return 0;
    }
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1u, name, nl);
    memcpy(out + dl + 1u + nl, ".layout", 8);
    return 1;
}

static Layout *load_layout(Arena *a, const Args *A)
{
    Layout *L = EK_NEW(a, Layout, 1);
    LayoutErr e;
    char path[512];
    const char *shown = path; /* the file named in a parse error */
    const char *dirs[4];
    const char *env = getenv("ERGOKEYS_LAYOUTS");
    size_t len = 0, nl = strlen(A->layout);
    char *buf = NULL;
    int i, nd = 0, found = 0;

    if (L == NULL) {
        fprintf(stderr, "ergokeys: out of memory\n");
        return NULL;
    }
    if (strchr(A->layout, '/') != NULL || (nl > 7u && strcmp(A->layout + nl - 7u, ".layout") == 0)) {
        buf = read_file(a, A->layout, &len);
        if (buf == NULL) {
            return NULL;
        }
        found = 1;
        shown = A->layout; /* any length: never copied into path */
    } else {
        if (A->ldir != NULL) {
            dirs[nd++] = A->ldir;
        }
        if (env != NULL && env[0] != '\0') {
            dirs[nd++] = env;
        }
        dirs[nd++] = "layouts";
        dirs[nd++] = EK_DATADIR "/layouts";
        for (i = 0; i < nd && !found; i++) {
            FILE *t;

            if (!layout_path(path, sizeof(path), dirs[i], A->layout)) {
                continue;
            }
            t = fopen(path, "rb");
            if (t != NULL) {
                fclose(t);
                buf = read_file(a, path, &len);
                if (buf == NULL) {
                    return NULL;
                }
                found = 1;
            }
        }
        if (!found) {
            fprintf(stderr, "ergokeys: layout '%s' not found (looked in -L, $ERGOKEYS_LAYOUTS, ./layouts, %s/layouts)\n",
                    A->layout, EK_DATADIR);
            return NULL;
        }
    }
    if (!layout_parse(buf, len, L, &e)) {
        fprintf(stderr, "ergokeys: %s:%u: %s%s%s%s\n", shown, (unsigned)e.line, e.why, e.ctx[0] ? " '" : "", e.ctx,
                e.ctx[0] ? "'" : "");
        return NULL;
    }
    return L;
}

static int load_weights(Arena *a, const Args *A, Weights *W)
{
    weights_defaults(W);
    if (A->conf != NULL) {
        size_t len = 0;
        char *buf = read_file(a, A->conf, &len);
        u32 line = 0;
        const char *why = NULL;

        if (buf == NULL) {
            return 0;
        }
        if (!weights_parse(buf, len, W, &line, &why)) {
            fprintf(stderr, "ergokeys: %s:%u: %s\n", A->conf, (unsigned)line, why);
            return 0;
        }
    }
    if (A->seed >= 0) {
        W->v[C_SEED] = (f64)A->seed;
    }
    if (A->iters >= 0) {
        W->v[C_ANNEAL_ITERS] = (f64)A->iters;
    }
    if (A->mods >= 0) {
        W->v[C_SUGGEST_MODS] = (f64)A->mods;
    }
    return 1;
}

/* A program name instead of a file: "ergokeys simulate hyprland" reads the config the program itself reads. With no
 * argument the first program found in the table below is used. Only when the argument is not a readable file. */
static char g_prog_path[512];

/* dst = a b c (any may be ""), bounded; length, or -1 when it does not fit */
static int join3(char *dst, size_t cap, const char *a, const char *b, const char *c)
{
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);

    if (la + lb + lc + 1u > cap) {
        return -1;
    }
    memcpy(dst, a, la);
    memcpy(dst + la, b, lb);
    memcpy(dst + la + lb, c, lc + 1u);
    return (int)(la + lb + lc);
}

static int path_readable(const char *p)
{
    FILE *f = fopen(p, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* fill g_prog_path with the first readable candidate for the program; 1 when found */
static int program_config(const char *name)
{
    static const char *const prog[] = {"hyprland", "hypr", "sway", "i3", "dwl", NULL};
    static const char *const rel[][3] = {{"hypr/hyprland.conf", NULL, NULL}, {"hypr/hyprland.conf", NULL, NULL},
                                         {"sway/config", NULL, NULL},        {"i3/config", NULL, NULL},
                                         {"dwl/config.h", NULL, NULL}};
    const char *home = getenv("HOME"), *xdg = getenv("XDG_CONFIG_HOME");
    unsigned i, j;

    for (i = 0; prog[i] != NULL; i++) {
        if (strcmp(prog[i], name) != 0) {
            continue;
        }
        for (j = 0; j < 4u; j++) {
            int n = -1;

            if (j == 0 && xdg != NULL && xdg[0] != '\0') {
                n = join3(g_prog_path, sizeof(g_prog_path), xdg, "/", rel[i][0]);
            } else if (j == 1 && home != NULL) {
                n = join3(g_prog_path, sizeof(g_prog_path), home, "/.config/", rel[i][0]);
            } else if (j == 2 && home != NULL && (strcmp(name, "i3") == 0 || strcmp(name, "sway") == 0)) {
                {
                char tail[16];

                if (join3(tail, sizeof(tail), "/.", name, "/config") > 0) {
                    n = join3(g_prog_path, sizeof(g_prog_path), home, tail, "");
                }
            }
            } else if (j == 3 && strcmp(name, "dwl") == 0) {
                n = join3(g_prog_path, sizeof(g_prog_path), "config.h", "", "");
            }
            if (n > 0 && path_readable(g_prog_path)) {
                return 1;
            }
        }
    }
    return 0;
}

/* simulate/suggest: turn "no argument" or a program name into the config file path in A->pos[0] */
static int resolve_input(Args *A)
{
    static const char *const order[] = {"hyprland", "sway", "i3", "dwl", NULL};
    unsigned i;

    if (A->npos == 0) {
        for (i = 0; order[i] != NULL; i++) {
            if (program_config(order[i])) {
                A->pos[0] = g_prog_path;
                A->npos = 1;
                return 1;
            }
        }
        return 0;
    }
    if (A->npos == 1 && !path_readable(A->pos[0]) && strchr(A->pos[0], '/') == NULL && program_config(A->pos[0])) {
        A->pos[0] = g_prog_path;
    }
    return 1;
}

static int load_entries(Arena *a, const Layout *L, const Args *A, Entries *E, Report *R, int *fmt)
{
    size_t len = 0;
    char *buf;

    if (A->npos != 1) {
        return usage_err("this subcommand takes exactly one FILE or program name", NULL);
    }
    buf = read_file(a, A->pos[0], &len);
    if (buf == NULL) {
        return EK_IO;
    }
    *fmt = A->fmt == FMT_AUTO ? detect_format(buf, len) : A->fmt;
    report_init(R);
    if (!entries_init(a, E, import_capacity(buf, len)) || !import_any(*fmt, a, L, buf, len, E, R)) {
        fprintf(stderr, "ergokeys: out of memory reading %s\n", A->pos[0]);
        return EK_IO;
    }
    if (E->n == 0) {
        print_skips(stderr, R, 0, A->pos[0]);
        fprintf(stderr, "ergokeys: no shortcuts found in %s (format %s); try --format\n", A->pos[0],
                ek_format_name[*fmt]);
        return EK_IO;
    }
    print_skips(stderr, R, 0, A->pos[0]);
    return 0;
}

static int cmd_simulate(Arena *a, const Args *A)
{
    Layout *L;
    Weights W;
    Entries E;
    Report *R = EK_NEW(a, Report, 1);
    Conflicts *Cf = EK_NEW(a, Conflicts, 1);
    Sim S;
    int fmt = 0, rc;

    if (R == NULL || Cf == NULL || !load_weights(a, A, &W) || (L = load_layout(a, A)) == NULL) {
        return EK_IO;
    }
    rc = load_entries(a, L, A, &E, R, &fmt);
    if (rc != 0) {
        return rc;
    }
    if (!conflicts_find(a, L, &E, Cf) || !simulate_run(a, L, &W, &E, E.chord, &S)) {
        fprintf(stderr, "ergokeys: out of memory\n");
        return EK_IO;
    }
    print_simulate(a, stdout, L, &E, &S, Cf, R, A->json, A->pos[0], fmt);
    return A->strict && (R->total > 0 || Cf->total > 0) ? EK_STRICT : EK_OK;
}

static int cmd_score(Arena *a, const Args *A)
{
    Layout *L;
    Weights W;
    int i, bad = 0;
    char t[96];

    if (A->npos == 0) {
        return usage_err("score needs at least one shortcut", NULL);
    }
    if (!load_weights(a, A, &W) || (L = load_layout(a, A)) == NULL) {
        return EK_IO;
    }
    if (A->json) {
        fputs("{\"layout\":", stdout);
        j_str(stdout, L->name);
        fputs(",\"scores\":[", stdout);
    } else {
        printf("layout %s\n%8s %9s  %s\n", L->name, "mm", "cost", "shortcut");
    }
    for (i = 0; i < A->npos; i++) {
        Chord c;
        const char *why = chord_parse(L, A->pos[i], strlen(A->pos[i]), &c);

        if (why != NULL) {
            fprintf(stderr, "ergokeys: %s: %s\n", A->pos[i], why);
            bad = 1;
            continue;
        }
        {
            Cost k = shortcut_cost(L, &W, &c);

            chord_fmt(L, &c, t, sizeof(t));
            if (A->json) {
                printf("%s{\"input\":", i ? "," : "");
                j_str(stdout, A->pos[i]);
                fputs(",\"chord\":", stdout);
                j_str(stdout, t);
                fputs(",\"mm\":", stdout);
                j_num(stdout, k.mm);
                fputs(",\"cost\":", stdout);
                j_num(stdout, k.cost);
                fputc('}', stdout);
            } else {
                printf("%8.2f %9.2f  %s\n", k.mm, k.cost, t);
            }
        }
    }
    if (A->json) {
        fputs("]}\n", stdout);
    }
    return bad ? EK_USAGE : EK_OK;
}

static int write_tsv(const char *path, const Layout *L, const Entries *E, const Chord *ch)
{
    FILE *f = fopen(path, "rb");
    u32 i;
    char t[96];

    if (f != NULL) {
        fclose(f);
        fprintf(stderr, "ergokeys: %s exists; not overwriting\n", path);
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "ergokeys: cannot write %s\n", path);
        return 0;
    }
    fputs("# proposed by ergokeys suggest: action TAB chord TAB weight [TAB fixed]\n", f);
    for (i = 0; i < E->n; i++) {
        chord_fmt(L, &ch[i], t, sizeof(t));
        fprintf(f, "%s\t%s\t%.6g%s\n", E->action[i], t, E->weight[i], E->fixed[i] ? "\tfixed" : "");
    }
    return fclose(f) == 0;
}

static int cmd_suggest(Arena *a, const Args *A)
{
    Layout *L;
    Weights W;
    Entries E;
    Report *R = EK_NEW(a, Report, 1);
    Conflicts *Cf = EK_NEW(a, Conflicts, 1);
    Sim Sb, Sa;
    OptCfg C;
    OptStat St;
    Chord *nw;
    int fmt = 0, rc;

    if (R == NULL || Cf == NULL || !load_weights(a, A, &W) || (L = load_layout(a, A)) == NULL) {
        return EK_IO;
    }
    rc = load_entries(a, L, A, &E, R, &fmt);
    if (rc != 0) {
        return rc;
    }
    nw = EK_NEW(a, Chord, E.n + 1u);
    if (nw == NULL || !conflicts_find(a, L, &E, Cf)) {
        fprintf(stderr, "ergokeys: out of memory\n");
        return EK_IO;
    }
    C.seed = (u32)W.v[C_SEED];
    C.iters = (u32)W.v[C_ANNEAL_ITERS];
    C.passes = (u32)W.v[C_GREEDY_PASSES];
    C.t0 = W.v[C_ANNEAL_T0];
    C.t1 = W.v[C_ANNEAL_T1];
    C.anymods = W.v[C_SUGGEST_MODS] > 0.5;
    C.verify = 0;
    rc = suggest(a, L, &W, &E, &C, nw, &St);
    if (rc == EK_NOSOL) {
        fprintf(stderr, "ergokeys: no conflict-free assignment: more actions than free chords for some modifier set\n");
        return EK_NOSOL;
    }
    if (rc != 0 || !simulate_run(a, L, &W, &E, E.chord, &Sb) || !simulate_run(a, L, &W, &E, nw, &Sa)) {
        fprintf(stderr, "ergokeys: out of memory\n");
        return EK_IO;
    }
    print_suggest(stdout, L, &E, nw, &Sb, &Sa, &St, Cf, R, A->json, A->pos[0], fmt, C.seed, C.anymods);
    if (A->out != NULL && !write_tsv(A->out, L, &E, nw)) {
        return EK_IO;
    }
    if (St.pinned_conflicts > 0) {
        fprintf(stderr, "ergokeys: %u conflicts between fixed actions or sequences were left as they are\n",
                (unsigned)St.pinned_conflicts);
    }
    return A->strict && (R->total > 0 || St.pinned_conflicts > 0) ? EK_STRICT : EK_OK;
}

static int cmd_layout(Arena *a, Args *A)
{
    Layout *L;

    if (A->npos > 1) {
        return usage_err("layout takes at most one NAME", NULL);
    }
    if (A->npos == 1) {
        A->layout = A->pos[0];
    }
    L = load_layout(a, A);
    if (L == NULL) {
        return EK_IO;
    }
    print_layout(stdout, L, A->json);
    return EK_OK;
}

/* span KEY KEY: the distance between the centres of two keys, and the same as the weights it can feed */
static int cmd_span(Arena *a, Args *A)
{
    Layout *L;
    int k0, k1;
    f64 d;

    if (A->npos != 2) {
        return usage_err("span takes two keys, for example: span a t", NULL);
    }
    L = load_layout(a, A);
    if (L == NULL) {
        return EK_IO;
    }
    k0 = layout_find(L, A->pos[0], strlen(A->pos[0]));
    k1 = layout_find(L, A->pos[1], strlen(A->pos[1]));
    if (k0 < 0 || k1 < 0) {
        return usage_err("key not in the layout", k0 < 0 ? A->pos[0] : A->pos[1]);
    }
    d = ek_dist(L->x[k0], L->y[k0], L->x[k1], L->y[k1]);
    if (A->json) {
        printf("{\"layout\": \"%s\", \"from\": \"%s\", \"to\": \"%s\", \"mm\": %.4f}\n", L->name, A->pos[0], A->pos[1], d);
    } else {
        printf("layout %s: %s to %s = %.2f mm\n", L->name, A->pos[0], A->pos[1], d);
        printf("as hand_span (two fingers of one hand): hand_span %.2f\n", d);
        printf("as finger_reach (one finger from its home key): finger_reach %.2f\n", d);
    }
    return EK_OK;
}

static int cmd_config(void)
{
    int i;

    puts("# ergokeys weights: name value, one per line; every line shown is the default.");
    puts("# Cost is in millimetres of weighted finger travel plus penalties.");
    for (i = 0; i < EK_NCONF; i++) {
        printf("\n# %s\n%s %g\n", ek_conf[i].doc, ek_conf[i].name, ek_conf[i].def);
    }
    return EK_OK;
}

int main(int argc, char **argv)
{
    Arena arena;
    Args A;
    const char *sub;
    int rc;

    if (argc < 2) {
        print_help(stderr);
        return EK_USAGE;
    }
    sub = argv[1];
    if (strcmp(sub, "help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0) {
        print_help(stdout);
        return EK_OK;
    }
    if (strcmp(sub, "--version") == 0) {
        printf("ergokeys %s\n", EK_VERSION);
        return EK_OK;
    }
    arena_init(&arena, g_mem, sizeof(g_mem));
    rc = parse_args(argc, argv, &A);
    if (rc < 0) {
        return EK_OK;
    }
    if (rc > 0) {
        return rc;
    }
    if (strcmp(sub, "simulate") == 0 || strcmp(sub, "suggest") == 0) {
        if (!resolve_input(&A)) {
            return usage_err("no FILE or program given and no hyprland, sway, i3 or dwl config found", NULL);
        }
    }
    if (strcmp(sub, "simulate") == 0) {
        return cmd_simulate(&arena, &A);
    }
    if (strcmp(sub, "score") == 0) {
        return cmd_score(&arena, &A);
    }
    if (strcmp(sub, "suggest") == 0) {
        return cmd_suggest(&arena, &A);
    }
    if (strcmp(sub, "layout") == 0) {
        return cmd_layout(&arena, &A);
    }
    if (strcmp(sub, "span") == 0) {
        return cmd_span(&arena, &A);
    }
    if (strcmp(sub, "config") == 0) {
        return cmd_config();
    }
    return usage_err("unknown subcommand", sub);
}
