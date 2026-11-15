/* chord.c - turning text into chords: modifier names, key names, one step (Ctrl+Shift+t, C-x), a sequence of steps.
 *
 * A step is modifiers plus one key. Separators are '+' (Ctrl+Shift+t, Super+Return) or '-' (C-x, Ctrl-Alt-Delete).
 * With '-' the Emacs letters C M s S also count as modifiers (C control, M alt, s super, S shift). A sequence is
 * steps separated by whitespace ("C-x C-s"). Key names are the layout labels plus the usual X11 and
 * Hyprland names (Return, Escape, comma, bracketleft, ...); shifted symbols (exclam, plus, '!') map to their base
 * key and add Shift. */

#define EK_MAXSTEPS 4

typedef struct {
    u8 nsteps;
    u8 mods[EK_MAXSTEPS];
    u8 key[EK_MAXSTEPS];
} Chord;

typedef struct {
    const char *name;
    const char *label;
    u8 shift;
} KeyAlias;

static const KeyAlias ek_aliases[] = {
    {"return", "enter", 0},       {"kp_enter", "enter", 0},     {"escape", "esc", 0},
    {"backspace", "bksp", 0},     {"bs", "bksp", 0},            {"comma", ",", 0},
    {"period", ".", 0},           {"dot", ".", 0},              {"slash", "/", 0},
    {"backslash", "\\", 0},       {"semicolon", ";", 0},        {"apostrophe", "'", 0},
    {"quoteright", "'", 0},       {"grave", "`", 0},            {"minus", "-", 0},
    {"equal", "=", 0},            {"bracketleft", "[", 0},      {"bracketright", "]", 0},
    {"caps_lock", "caps", 0},     {"capslock", "caps", 0},      {"exclam", "1", 1},
    {"at", "2", 1},               {"numbersign", "3", 1},       {"dollar", "4", 1},
    {"percent", "5", 1},          {"asciicircum", "6", 1},      {"ampersand", "7", 1},
    {"asterisk", "8", 1},         {"parenleft", "9", 1},        {"parenright", "0", 1},
    {"underscore", "-", 1},       {"plus", "=", 1},             {"braceleft", "[", 1},
    {"braceright", "]", 1},       {"bar", "\\", 1},             {"colon", ";", 1},
    {"quotedbl", "'", 1},         {"less", ",", 1},             {"greater", ".", 1},
    {"question", "/", 1},         {"asciitilde", "`", 1}};

static const char ek_shifted_chars[] = "!@#$%^&*()_+{}|:\"<>?~";
static const char ek_shifted_base[] = "1234567890-=[]\\;',./`";

/* modifier word -> mask, 0 when unknown. The words used by i3, sway, Hyprland, dwl and plain text. */
static u8 mod_from_word(const char *s, size_t n)
{
    if (ek_ieq(s, n, "ctrl") || ek_ieq(s, n, "control") || ek_ieq(s, n, "ctl")) {
        return M_CTRL;
    }
    if (ek_ieq(s, n, "alt") || ek_ieq(s, n, "mod1") || ek_ieq(s, n, "meta")) {
        return M_ALT;
    }
    if (ek_ieq(s, n, "super") || ek_ieq(s, n, "win") || ek_ieq(s, n, "windows") || ek_ieq(s, n, "mod4") ||
        ek_ieq(s, n, "logo") || ek_ieq(s, n, "gui") || ek_ieq(s, n, "cmd")) {
        return M_SUPER;
    }
    if (ek_ieq(s, n, "shift")) {
        return M_SHIFT;
    }
    return 0;
}

/* Resolve a key name to a layout key. *shift is set when the name implies Shift. Returns 0 when unknown. */
static int key_resolve(const Layout *L, const char *s, size_t n, u8 *key, u8 *shift)
{
    char low[EK_LABEL * 3];
    size_t i;
    int k;

    *shift = 0;
    if (n == 0 || n >= sizeof(low)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        low[i] = (char)ek_lower((u8)s[i]);
    }
    low[n] = '\0';
    if (n == 1u) {
        const char *p = strchr(ek_shifted_chars, low[0]);

        if (p != NULL && low[0] != '\0') {
            k = layout_find(L, ek_shifted_base + (p - ek_shifted_chars), 1);
            if (k >= 0) {
                *key = (u8)k;
                *shift = 1;
                return 1;
            }
            return 0;
        }
    }
    for (i = 0; i < sizeof(ek_aliases) / sizeof(ek_aliases[0]); i++) {
        if (strcmp(low, ek_aliases[i].name) == 0) {
            k = layout_find(L, ek_aliases[i].label, strlen(ek_aliases[i].label));
            if (k < 0) {
                return 0;
            }
            *key = (u8)k;
            *shift = ek_aliases[i].shift;
            return 1;
        }
    }
    k = layout_find(L, low, n);
    if (k < 0) {
        return 0;
    }
    *key = (u8)k;
    return 1;
}

/* Parse "mods... key" with '+' or '-' separators. Returns NULL on success, else the reason. */
static const char *step_parse(const Layout *L, const char *s, size_t n, u8 *mods, u8 *key)
{
    size_t pos = 0;
    u8 m = 0, shift = 0;

    for (;;) {
        size_t j;
        u8 add = 0;
        int found = 0;

        for (j = pos; j + 1u < n; j++) {
            if (s[j] != '+' && s[j] != '-') {
                continue;
            }
            add = mod_from_word(s + pos, j - pos);
            if (add == 0 && s[j] == '-' && j - pos == 1u) {
                switch (s[pos]) {
                case 'C': add = M_CTRL; break;
                case 'M': case 'A': add = M_ALT; break;
                case 's': add = M_SUPER; break;
                case 'S': add = M_SHIFT; break;
                default: break;
                }
            }
            found = add != 0;
            break;
        }
        if (!found) {
            break;
        }
        m |= add;
        pos = j + 1u;
    }
    if (!key_resolve(L, s + pos, n - pos, key, &shift)) {
        return "unknown key";
    }
    *mods = (u8)(m | (shift ? M_SHIFT : 0));
    return NULL;
}

/* a chord is whitespace-separated steps; returns NULL on success, else the reason */
static const char *chord_parse(const Layout *L, const char *s, size_t n, Chord *c)
{
    size_t p = 0;

    memset(c, 0, sizeof(*c));
    while (p < n) {
        size_t b;
        const char *why;

        while (p < n && ek_is_space((u8)s[p])) {
            p++;
        }
        if (p >= n) {
            break;
        }
        b = p;
        while (p < n && !ek_is_space((u8)s[p])) {
            p++;
        }
        if (c->nsteps >= EK_MAXSTEPS) {
            return "more than 4 steps in a sequence";
        }
        why = step_parse(L, s + b, p - b, &c->mods[c->nsteps], &c->key[c->nsteps]);
        if (why != NULL) {
            return why;
        }
        c->nsteps++;
    }
    if (c->nsteps == 0) {
        return "empty chord";
    }
    return NULL;
}

static int chord_equal(const Chord *a, const Chord *b)
{
    u32 i;

    if (a->nsteps != b->nsteps) {
        return 0;
    }
    for (i = 0; i < a->nsteps; i++) {
        if (a->mods[i] != b->mods[i] || a->key[i] != b->key[i]) {
            return 0;
        }
    }
    return 1;
}

/* a shortcut that is the start of another one cannot coexist with it */
static int chord_is_prefix(const Chord *a, const Chord *b)
{
    u32 i;

    if (a->nsteps > b->nsteps) {
        return 0;
    }
    for (i = 0; i < a->nsteps; i++) {
        if (a->mods[i] != b->mods[i] || a->key[i] != b->key[i]) {
            return 0;
        }
    }
    return 1;
}

/* canonical text: "Ctrl+Alt+Super+Shift+key", steps separated by a space; returns the length written */
static size_t chord_fmt(const Layout *L, const Chord *c, char *out, size_t cap)
{
    size_t w = 0;
    u32 i;
    int m;

    if (cap == 0) {
        return 0;
    }
    for (i = 0; i < c->nsteps; i++) {
        const char *lab = L->label[c->key[i]];
        size_t ll = strlen(lab);

        if (i > 0 && w + 1u < cap) {
            out[w++] = ' ';
        }
        for (m = 0; m < 4; m++) {
            if (c->mods[i] & (1 << m)) {
                size_t nl = strlen(ek_mod_name[m]);

                if (w + nl + 1u < cap) {
                    memcpy(out + w, ek_mod_name[m], nl);
                    w += nl;
                    out[w++] = '+';
                }
            }
        }
        if (w + ll < cap) {
            memcpy(out + w, lab, ll);
            w += ll;
        }
    }
    out[w] = '\0';
    return w;
}
