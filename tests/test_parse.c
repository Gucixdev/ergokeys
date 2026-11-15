/* test_parse.c - chord parsing, the layout and weight parsers, and the four importers, on small inline texts
 * and on the real sample configs in tests/data. */
#include "../src/ek.c"
#include "t.h"

static f64 g_mem[(16u * 1024u * 1024u) / sizeof(f64)];
static Arena arena;
static Layout L;

/* canonical text of a parsed chord, or the parse error prefixed with '!' */
static const char *fmt(const char *in)
{
    static char out[160];
    Chord c;
    const char *why = t_chord(&L, in, &c);

    if (why != NULL) {
        out[0] = '!';
        strncpy(out + 1, why, sizeof(out) - 2u);
        out[sizeof(out) - 1u] = '\0';
        return out;
    }
    chord_fmt(&L, &c, out, sizeof(out));
    return out;
}

static void test_chords(void)
{
    CHECK(strcmp(fmt("Ctrl+c"), "Ctrl+c") == 0);
    CHECK(strcmp(fmt("ctrl+C"), "Ctrl+c") == 0);
    CHECK(strcmp(fmt("Super+Shift+1"), "Super+Shift+1") == 0);
    CHECK(strcmp(fmt("Shift+Super+1"), "Super+Shift+1") == 0);
    CHECK(strcmp(fmt("Mod4+Return"), "Super+enter") == 0);
    CHECK(strcmp(fmt("Mod1+Tab"), "Alt+tab") == 0);
    CHECK(strcmp(fmt("C-x C-s"), "Ctrl+x Ctrl+s") == 0);
    CHECK(strcmp(fmt("M-x"), "Alt+x") == 0);
    CHECK(strcmp(fmt("s-S-q"), "Super+Shift+q") == 0);
    CHECK(strcmp(fmt("Ctrl-Alt-t"), "Ctrl+Alt+t") == 0);
    CHECK(strcmp(fmt("g"), "g") == 0);
    CHECK(strcmp(fmt("Super+comma"), "Super+,") == 0);
    CHECK(strcmp(fmt("Super+period"), "Super+.") == 0);
    CHECK(strcmp(fmt("Super+minus"), "Super+-") == 0);
    CHECK(strcmp(fmt("Super+plus"), "Super+Shift+=") == 0);
    CHECK(strcmp(fmt("Super+exclam"), "Super+Shift+1") == 0);
    CHECK(strcmp(fmt("Super+!"), "Super+Shift+1") == 0);
    CHECK(strcmp(fmt("Ctrl++"), "Ctrl+Shift+=") == 0);
    CHECK(strcmp(fmt("Ctrl+-"), "Ctrl+-") == 0);
    CHECK(strcmp(fmt("Super+Left"), "Super+left") == 0);
    /* a modifier alone is not a chord */
    CHECK(fmt("Ctrl")[0] == '!');
    /* errors */
    CHECK(fmt("")[0] == '!');
    CHECK(fmt("   ")[0] == '!');
    CHECK(fmt("Ctrl+nosuchkey")[0] == '!');
    CHECK(fmt("Hyper+x")[0] == '!');
    CHECK(fmt("a b c d")[0] != '!');
    CHECK(fmt("a b c d e")[0] == '!');
    CHECK(fmt("Ctrl+")[0] == '!');
    CHECK(strcmp(fmt("+"), "Shift+=") == 0);
    CHECK(strcmp(fmt("Ctrl+Ctrl+a"), "Ctrl+a") == 0);
}

static void test_chord_relations(void)
{
    Chord a, b, c;

    CHECK(t_chord(&L, "Ctrl+x Ctrl+s", &a) == NULL);
    CHECK(t_chord(&L, "Ctrl+x", &b) == NULL);
    CHECK(t_chord(&L, "Ctrl+x Ctrl+c", &c) == NULL);
    CHECK(chord_is_prefix(&b, &a) && !chord_is_prefix(&a, &b));
    CHECK(!chord_equal(&a, &c) && !chord_is_prefix(&a, &c) && !chord_is_prefix(&c, &a));
    CHECK(chord_equal(&a, &a));
}

static void test_layout_parser(void)
{
    static const char *const bad[] = {"", "name\n", "pitch 3\nrow 0 0 a\n", "row 0 0\n", "key a 0 0\n",
                                      "frobnicate 1\n", "row 0 0 a a\nfinger LP a\n",
                                      "row 0 0 a\nfinger XX a\n", "row 0 0 a\nfinger LP b\n"};
    Layout T;
    LayoutErr e;
    u32 i;
    static const char good[] = "name tiny\n"
                               "row 0 0 lc la ls lsh a b\n"
                               "finger LP lc la ls lsh a\nfinger RP b\n"
                               "home LP a\nhome RP b\n"
                               "mod ctrl lc\nmod alt la\nmod super ls\nmod shift lsh\n";

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!layout_parse(bad[i], strlen(bad[i]), &T, &e));
        CHECK(e.why != NULL);
    }
    CHECK(layout_parse(good, sizeof(good) - 1u, &T, &e));
    CHECK(strcmp(T.name, "tiny") == 0 && T.nkeys == 6u);
    CHECK(!T.chordable[0] && T.chordable[4] && T.chordable[5]);
    /* the shipped layouts parse and give every key a finger */
    {
        static const char *const names[] = {"layouts/qwerty-ansi.layout", "layouts/ortho-5x12.layout",
                                            "layouts/split-4x6.layout"};

        for (i = 0; i < 3u; i++) {
            size_t n;
            char *buf = t_read(&arena, names[i], &n);

            CHECK(buf != NULL && layout_parse(buf, n, &T, &e));
        }
    }
}

static void test_weights_parser(void)
{
    Weights W;
    u32 line = 0;
    const char *why = NULL;
    static const char ok[] = "# c\ntravel 2\npen_mod_pinky = 9.5\n\nseed 7\n";
    static const char *const bad[] = {"nosuch 1\n", "travel\n", "travel abc\n", "travel 1 2\n", "travel -1\n"};
    u32 i;

    weights_defaults(&W);
    CHECK(weights_parse(ok, sizeof(ok) - 1u, &W, &line, &why));
    CHECK_NEAR(W.v[C_TRAVEL], 2.0, 0.0);
    CHECK_NEAR(W.v[C_MOD_PINKY], 9.5, 0.0);
    CHECK_NEAR(W.v[C_SEED], 7.0, 0.0);
    CHECK_NEAR(W.v[C_VERTICAL], 1.3, 0.0);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        weights_defaults(&W);
        CHECK(!weights_parse(bad[i], strlen(bad[i]), &W, &line, &why));
        CHECK(line == 1u && why != NULL);
    }
}

typedef struct {
    Entries E;
    Report R;
    int fmt;
} Imp;

static int run_import(const char *text, int fmt, Imp *I)
{
    size_t n = strlen(text);
    char *buf = (char *)arena_alloc(&arena, n + 1u);

    memcpy(buf, text, n + 1u);
    report_init(&I->R);
    I->fmt = fmt == FMT_AUTO ? detect_format(buf, n) : fmt;
    return entries_init(&arena, &I->E, import_capacity(buf, n)) && import_any(I->fmt, &arena, &L, buf, n, &I->E, &I->R) >= 0;
}

static const char *entry(const Imp *I, u32 i)
{
    static char out[160];

    chord_fmt(&L, &I->E.chord[i], out, sizeof(out));
    return out;
}

static void test_tsv(void)
{
    Imp I;
    static const char text[] = "# comment\n"
                               "close\tSuper+q\n"
                               "save\tC-x C-s\t3\n"
                               "pinned\tCtrl+Alt+Delete\n"
                               "pinned2\tCtrl+t\t1\tfixed\n"
                               "no tab here\n"
                               "badkey\tCtrl+nosuch\n"
                               "badweight\tCtrl+y\tabc\n"
                               "\n"
                               "last\tAlt+F4\n";

    CHECK(run_import(text, FMT_TSV, &I));
    CHECK(I.E.n == 4u);
    CHECK(strcmp(entry(&I, 0), "Super+q") == 0 && strcmp(I.E.action[0], "close") == 0);
    CHECK(strcmp(entry(&I, 1), "Ctrl+x Ctrl+s") == 0 && I.E.weight[1] == 3.0);
    CHECK(I.E.fixed[2] == 1 && I.E.fixed[0] == 0);
    CHECK(I.R.total == 4u);
    CHECK(I.R.item[0].line == 4u || I.R.item[0].line == 5u);
    CHECK(I.R.lines == 10u);
}

static void test_hyprland(void)
{
    Imp I;
    static const char text[] = "$m = SUPER\n"
                               "$t = kitty\n"
                               "bind = $m, Q, exec, $t\n"
                               "bind = $m SHIFT, 1, workspace, 1\n"
                               "bindm = $m, mouse:272, movewindow\n"
                               "bindl = , XF86AudioMute, exec, x\n"
                               "bind = $m, code:10, exec, y\n"
                               "binde = $m, left, resizeactive, -10 0\n"
                               "bind = SUPER, Q\n"
                               "general {\n  gaps_in = 5\n}\n";

    CHECK(run_import(text, FMT_AUTO, &I));
    CHECK(I.fmt == FMT_HYPR);
    CHECK(I.E.n == 3u);
    CHECK(strcmp(entry(&I, 0), "Super+q") == 0 && strcmp(I.E.action[0], "exec kitty") == 0);
    CHECK(strcmp(entry(&I, 1), "Super+Shift+1") == 0);
    CHECK(strcmp(entry(&I, 2), "Super+left") == 0);
    CHECK(I.R.total == 4u);
}

static void test_i3(void)
{
    Imp I;
    static const char text[] = "set $mod Mod4\n"
                               "bindsym $mod+Return exec term\n"
                               "bindsym --release $mod+Shift+s exec shot\n"
                               "bindsym --whole-window $mod+Mod1+x nop\n"
                               "bindcode 20 exec foo\n"
                               "mode \"resize\" {\n"
                               "  bindsym j resize shrink width 10 px\n"
                               "}\n"
                               "bindsym $mod+q kill\n"
                               "bindsym Print\n";

    CHECK(run_import(text, FMT_AUTO, &I));
    CHECK(I.fmt == FMT_I3);
    CHECK(I.E.n == 4u);
    CHECK(strcmp(entry(&I, 0), "Super+enter") == 0);
    CHECK(strcmp(entry(&I, 1), "Super+Shift+s") == 0);
    CHECK(strcmp(entry(&I, 2), "Alt+Super+x") == 0);
    CHECK(strcmp(entry(&I, 3), "Super+q") == 0);
    CHECK(I.R.total == 3u);
}

static void test_dwl(void)
{
    Imp I;
    static const char text[] = "#define MODKEY WLR_MODIFIER_ALT\n"
                               "#define TAGKEYS(KEY,SKEY,TAG) \\\n"
                               "\t{ MODKEY, KEY, view, {.ui = 1 << TAG} }, \\\n"
                               "\t{ MODKEY|WLR_MODIFIER_CTRL, KEY, toggleview, {.ui = 1 << TAG} }, \\\n"
                               "\t{ MODKEY|WLR_MODIFIER_SHIFT, SKEY, tag, {.ui = 1 << TAG} }, \\\n"
                               "\t{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }\n"
                               "static const Key keys[] = {\n"
                               "\t{ MODKEY, XKB_KEY_p, spawn, {.v = menucmd} },\n"
                               "\t{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit, {0} },\n"
                               "\t{ 0, XKB_KEY_XF86AudioMute, spawn, {0} },\n"
                               "\tTAGKEYS(XKB_KEY_1, XKB_KEY_exclam, 0),\n"
                               "};\n"
                               "static const Button buttons[] = {\n"
                               "\t{ MODKEY, BTN_LEFT, moveresize, {.ui = CurMove} },\n"
                               "};\n";

    CHECK(run_import(text, FMT_AUTO, &I));
    CHECK(I.fmt == FMT_DWL);
    CHECK(I.E.n == 6u);
    CHECK(strcmp(entry(&I, 0), "Alt+p") == 0);
    CHECK(strcmp(entry(&I, 1), "Alt+Shift+q") == 0);
    CHECK(strcmp(entry(&I, 2), "Alt+1") == 0);
    CHECK(strcmp(entry(&I, 3), "Ctrl+Alt+1") == 0);
    CHECK(strcmp(entry(&I, 4), "Alt+Shift+1") == 0);
    CHECK(strcmp(entry(&I, 5), "Ctrl+Alt+Shift+1") == 0);
    CHECK(I.R.total == 1u);
}

static void test_samples(void)
{
    static const struct {
        const char *path;
        int fmt;
        u32 n, skipped;
    } s[] = {{"tests/data/sample.tsv", FMT_TSV, 10, 2},
             {"tests/data/hyprland.conf", FMT_HYPR, 22, 12},
             {"tests/data/i3.conf", FMT_I3, 29, 6},
             {"tests/data/dwl-config.h", FMT_DWL, 31, 3}};
    u32 i;

    for (i = 0; i < sizeof(s) / sizeof(s[0]); i++) {
        size_t n;
        char *buf = t_read(&arena, s[i].path, &n);
        Entries E;
        Report R;
        size_t mark = arena_mark(&arena);

        CHECK(buf != NULL);
        if (buf == NULL) {
            continue;
        }
        CHECK(detect_format(buf, n) == s[i].fmt);
        report_init(&R);
        CHECK(entries_init(&arena, &E, import_capacity(buf, n)));
        CHECK(import_any(s[i].fmt, &arena, &L, buf, n, &E, &R) >= 0);
        CHECK(E.n == s[i].n);
        CHECK(R.total == s[i].skipped);
        arena_reset(&arena, mark);
    }
}

int main(void)
{
    arena_init(&arena, g_mem, sizeof(g_mem));
    if (!t_layout(&arena, "layouts/qwerty-ansi.layout", &L)) {
        return 2;
    }
    test_chords();
    test_chord_relations();
    test_layout_parser();
    test_weights_parser();
    test_tsv();
    test_hyprland();
    test_i3();
    test_dwl();
    test_samples();
    return t_report("test_parse");
}
