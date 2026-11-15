/* opt.c - suggest: rebind actions to chords that cost less, with no two actions on one chord.
 *
 * What moves. A single-step action that is not marked fixed has a modifier set and a key. With suggest_mods = 0
 * the modifier set stays and only the key changes; with 1 the action may also take any modifier set that appears
 * in the input. Sequences and fixed actions stay where they are but keep their first step reserved, so nothing
 * moved onto it. The set of actions never changes.
 * Cost. A single chord costs the same wherever it appears (fingers start at home), so every (modifiers, key)
 * cost is computed once into a table; the search then only adds and subtracts table entries. The objective is
 * the sum of weight * cost over the movable actions.
 * Search. Conflicts in the input are resolved first (an action whose chord is taken goes to the cheapest free
 * chord). Then greedy passes (best relocation or swap per action while it lowers the total), simulated annealing
 * with a fixed seed over relocations and swaps, and greedy passes again. The best state seen is kept, and the
 * result is never above the cost it started from. All tables live in the arena; the search allocates nothing. */

typedef struct {
    u32 seed;
    u32 iters;
    u32 passes;
    f64 t0;
    f64 t1;
    u8 anymods;
    u8 verify;     /* recompute the total after every greedy move and count moves that did not lower it */
} OptCfg;

typedef struct {
    u32 displaced;        /* actions moved off a chord they shared with another action */
    u32 pinned_conflicts; /* conflicts between fixed actions or sequences, which cannot be resolved here */
    u32 greedy_moves;
    u32 greedy_bad;       /* greedy moves that did not lower the total (verify mode only; must stay 0) */
    u32 anneal_accepted;
    f64 start;            /* total over movable actions once conflicts are resolved */
    f64 after;
} OptStat;

typedef struct {
    const Layout *L;
    const Entries *E;
    u32 n, nk, nmov, ncand, nml;
    u8 *mask, *key;
    u8 *bmask, *bkey;
    u32 *mv;
    u8 *cand;
    u8 ml[EK_NMASK];
    f64 *ct;
    u32 *occ;
    u8 anymods;
} Prob;

static f64 p_cost(const Prob *P, u32 i, u32 m, u32 k)
{
    return P->E->weight[i] * P->ct[m * P->nk + k];
}

static f64 p_total(const Prob *P)
{
    f64 t = 0.0;
    u32 ii;

    for (ii = 0; ii < P->nmov; ii++) {
        u32 i = P->mv[ii];

        t += p_cost(P, i, P->mask[i], P->key[i]);
    }
    return t;
}

static int p_movable(const Entries *E, u32 i)
{
    return E->chord[i].nsteps == 1u && !E->fixed[i];
}

/* reserve the first step of every action that does not move, then the current cell of every one that does */
static u32 p_occ_rebuild(Prob *P)
{
    u32 i, bad = 0;

    memset(P->occ, 0, sizeof(u32) * P->nk * EK_NMASK);
    for (i = 0; i < P->n; i++) {
        if (!p_movable(P->E, i)) {
            u32 c = (u32)P->E->chord[i].mods[0] * P->nk + P->E->chord[i].key[0];

            if (P->occ[c] == 0) {
                P->occ[c] = i + 1u;
            } else {
                bad++;
            }
        }
    }
    for (i = 0; i < P->nmov; i++) {
        u32 a = P->mv[i];

        P->occ[(u32)P->mask[a] * P->nk + P->key[a]] = a + 1u;
    }
    return bad;
}

static void p_put(Prob *P, u32 i, u32 m, u32 k)
{
    P->mask[i] = (u8)m;
    P->key[i] = (u8)k;
    P->occ[m * P->nk + k] = i + 1u;
}

/* masks an action may take */
static u32 p_masks(const Prob *P, u32 i, const u8 **list, u8 *own)
{
    if (P->anymods) {
        *list = P->ml;
        return P->nml;
    }
    *own = P->mask[i];
    *list = own;
    return 1;
}

/* best strictly improving relocation or swap for action i; applies it and returns 1, or returns 0 */
static int p_improve(Prob *P, u32 i, f64 *gain)
{
    const u8 *ms;
    u8 own = 0;
    u32 nm = p_masks(P, i, &ms, &own);
    f64 cur = p_cost(P, i, P->mask[i], P->key[i]);
    f64 best = -1e-9;
    u32 kind = 0, bm = 0, bk = 0, bj = 0, mi, ci, jj;

    for (mi = 0; mi < nm; mi++) {
        for (ci = 0; ci < P->ncand; ci++) {
            u32 m = ms[mi], k = P->cand[ci];
            f64 d;

            if (P->occ[m * P->nk + k] != 0) {
                continue;
            }
            d = p_cost(P, i, m, k) - cur;
            if (d < best) {
                best = d;
                kind = 1;
                bm = m;
                bk = k;
            }
        }
    }
    for (jj = 0; jj < P->nmov; jj++) {
        u32 j = P->mv[jj];
        u32 nim, njm, ni, nj;
        f64 d;

        if (j == i) {
            continue;
        }
        nim = P->anymods ? P->mask[j] : P->mask[i];
        njm = P->anymods ? P->mask[i] : P->mask[j];
        ni = nim * P->nk + P->key[j];
        nj = njm * P->nk + P->key[i];
        if (ni == nj || (P->occ[ni] != 0 && P->occ[ni] != i + 1u && P->occ[ni] != j + 1u) ||
            (P->occ[nj] != 0 && P->occ[nj] != i + 1u && P->occ[nj] != j + 1u)) {
            continue;
        }
        d = p_cost(P, i, nim, P->key[j]) + p_cost(P, j, njm, P->key[i]) - cur - p_cost(P, j, P->mask[j], P->key[j]);
        if (d < best) {
            best = d;
            kind = 2;
            bj = j;
        }
    }
    if (kind == 0) {
        return 0;
    }
    if (kind == 1) {
        P->occ[(u32)P->mask[i] * P->nk + P->key[i]] = 0;
        p_put(P, i, bm, bk);
    } else {
        u32 nim = P->anymods ? P->mask[bj] : P->mask[i];
        u32 njm = P->anymods ? P->mask[i] : P->mask[bj];
        u32 ki = P->key[bj], kj = P->key[i];

        P->occ[(u32)P->mask[i] * P->nk + P->key[i]] = 0;
        P->occ[(u32)P->mask[bj] * P->nk + P->key[bj]] = 0;
        p_put(P, i, nim, ki);
        p_put(P, bj, njm, kj);
    }
    *gain = -best;
    return 1;
}

static void p_greedy(Prob *P, const OptCfg *C, OptStat *S)
{
    u32 pass, ii;

    for (pass = 0; pass < C->passes; pass++) {
        int changed = 0;

        for (ii = 0; ii < P->nmov; ii++) {
            f64 gain, before = C->verify ? p_total(P) : 0.0;

            if (p_improve(P, P->mv[ii], &gain)) {
                changed = 1;
                S->greedy_moves++;
                if (C->verify && !(p_total(P) < before)) {
                    S->greedy_bad++;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
}

/* one random relocation or swap; accepted by the Metropolis rule at temperature t (already scaled) */
static void p_anneal(Prob *P, const OptCfg *C, OptStat *S, f64 wscale, f64 *cur, f64 *best)
{
    Rng rng;
    u32 it;

    if (P->nmov < 2u) {
        return;
    }
    rng_seed(&rng, C->seed);
    for (it = 0; it < C->iters; it++) {
        f64 frac = 1.0 - (f64)it / (f64)C->iters;
        f64 t = (C->t1 + (C->t0 - C->t1) * frac * frac * frac) * wscale;
        u32 i = P->mv[rng_below(&rng, P->nmov)];
        f64 d;
        int accept;

        if ((rng_next(&rng) & 1u) == 0u) {
            u32 m = P->anymods ? P->ml[rng_below(&rng, P->nml)] : P->mask[i];
            u32 k = P->cand[rng_below(&rng, P->ncand)];

            if (P->occ[m * P->nk + k] != 0) {
                continue;
            }
            d = p_cost(P, i, m, k) - p_cost(P, i, P->mask[i], P->key[i]);
            accept = d <= 0.0 || rng_unit(&rng) < ek_expneg(d / t);
            if (accept) {
                P->occ[(u32)P->mask[i] * P->nk + P->key[i]] = 0;
                p_put(P, i, m, k);
            }
        } else {
            u32 j = P->mv[rng_below(&rng, P->nmov)];
            u32 nim, njm, ni, nj;

            if (j == i) {
                continue;
            }
            nim = P->anymods ? P->mask[j] : P->mask[i];
            njm = P->anymods ? P->mask[i] : P->mask[j];
            ni = nim * P->nk + P->key[j];
            nj = njm * P->nk + P->key[i];
            if (ni == nj || (P->occ[ni] != 0 && P->occ[ni] != i + 1u && P->occ[ni] != j + 1u) ||
                (P->occ[nj] != 0 && P->occ[nj] != i + 1u && P->occ[nj] != j + 1u)) {
                continue;
            }
            d = p_cost(P, i, nim, P->key[j]) + p_cost(P, j, njm, P->key[i]) - p_cost(P, i, P->mask[i], P->key[i]) -
                p_cost(P, j, P->mask[j], P->key[j]);
            accept = d <= 0.0 || rng_unit(&rng) < ek_expneg(d / t);
            if (accept) {
                u32 ki = P->key[j], kj = P->key[i];

                P->occ[(u32)P->mask[i] * P->nk + P->key[i]] = 0;
                P->occ[(u32)P->mask[j] * P->nk + P->key[j]] = 0;
                p_put(P, i, nim, ki);
                p_put(P, j, njm, kj);
            }
        }
        if (accept) {
            S->anneal_accepted++;
            *cur += d;
            if (*cur < *best - 1e-9) {
                *best = *cur;
                memcpy(P->bmask, P->mask, P->n);
                memcpy(P->bkey, P->key, P->n);
            }
        }
    }
}

/* Returns 0 and fills out[0..n) with the proposed chords; EK_NOSOL when some action has no free chord left;
 * -1 when the arena is full. */
static int suggest(Arena *a, const Layout *L, const Weights *W, const Entries *E, const OptCfg *C, Chord *out,
                   OptStat *S)
{
    Prob P;
    size_t mark = arena_mark(a);
    u32 i, k, m, seen = 0;
    f64 cur, best, wsum = 0.0, wscale;
    u32 ncell;
    int rc = 0;

    memset(&P, 0, sizeof(P));
    memset(S, 0, sizeof(*S));
    P.L = L;
    P.E = E;
    P.n = E->n;
    P.nk = L->nkeys;
    P.anymods = C->anymods;
    ncell = P.nk * EK_NMASK;
    P.mask = EK_NEW(a, u8, E->n + 1u);
    P.key = EK_NEW(a, u8, E->n + 1u);
    P.bmask = EK_NEW(a, u8, E->n + 1u);
    P.bkey = EK_NEW(a, u8, E->n + 1u);
    P.mv = EK_NEW(a, u32, E->n + 1u);
    P.cand = EK_NEW(a, u8, P.nk);
    P.ct = EK_NEW(a, f64, ncell);
    P.occ = EK_NEW(a, u32, ncell);
    if (!P.mask || !P.key || !P.bmask || !P.bkey || !P.mv || !P.cand || !P.ct || !P.occ) {
        arena_reset(a, mark);
        return -1;
    }

    for (k = 0; k < P.nk; k++) {
        if (L->chordable[k]) {
            P.cand[P.ncand++] = (u8)k;
        }
    }
    for (i = 0; i < E->n; i++) {
        P.mask[i] = E->chord[i].mods[0];
        P.key[i] = E->chord[i].key[0];
        if (p_movable(E, i)) {
            P.mv[P.nmov++] = i;
            wsum += E->weight[i];
            if (!(seen & (1u << P.mask[i]))) {
                seen |= 1u << P.mask[i];
                P.ml[P.nml++] = P.mask[i];
            }
        }
    }
    wscale = P.nmov > 0 ? wsum / (f64)P.nmov : 1.0;
    for (m = 0; m < EK_NMASK; m++) {
        for (k = 0; k < P.nk; k++) {
            Chord c;

            memset(&c, 0, sizeof(c));
            c.nsteps = 1;
            c.mods[0] = (u8)m;
            c.key[0] = (u8)k;
            P.ct[m * P.nk + k] = shortcut_cost(L, W, &c).cost;
        }
    }

    /* conflicts: fixed actions and sequences claim their cell first, then movable ones in input order */
    memset(P.occ, 0, sizeof(u32) * ncell);
    for (i = 0; i < E->n; i++) {
        if (!p_movable(E, i)) {
            u32 c = (u32)E->chord[i].mods[0] * P.nk + E->chord[i].key[0];

            if (P.occ[c] == 0) {
                P.occ[c] = i + 1u;
            } else {
                S->pinned_conflicts++;
            }
        }
    }
    {
        u32 *disp = EK_NEW(a, u32, E->n + 1u);
        u32 nd = 0, di;

        if (disp == NULL) {
            arena_reset(a, mark);
            return -1;
        }
        for (i = 0; i < P.nmov; i++) {
            u32 x = P.mv[i];
            u32 c = (u32)P.mask[x] * P.nk + P.key[x];

            if (P.occ[c] == 0) {
                P.occ[c] = x + 1u;
            } else {
                disp[nd++] = x;
            }
        }
        S->displaced = nd;
        for (di = 0; di < nd; di++) {
            u32 x = disp[di], bm = 0, bk = 0, mi, ci;
            f64 bc = 0.0;
            int have = 0;
            const u8 *ms;
            u8 own = 0;
            u32 nm = p_masks(&P, x, &ms, &own);

            for (mi = 0; mi < nm; mi++) {
                for (ci = 0; ci < P.ncand; ci++) {
                    u32 mm = ms[mi], kk = P.cand[ci];
                    f64 c = P.ct[mm * P.nk + kk];

                    if (P.occ[mm * P.nk + kk] == 0 && (!have || c < bc)) {
                        have = 1;
                        bc = c;
                        bm = mm;
                        bk = kk;
                    }
                }
            }
            if (!have) {
                rc = EK_NOSOL;
                break;
            }
            p_put(&P, x, bm, bk);
        }
    }

    if (rc == 0) {
        cur = p_total(&P);
        S->start = cur;
        best = cur;
        memcpy(P.bmask, P.mask, P.n);
        memcpy(P.bkey, P.key, P.n);
        p_greedy(&P, C, S);
        cur = p_total(&P);
        if (cur < best) {
            best = cur;
            memcpy(P.bmask, P.mask, P.n);
            memcpy(P.bkey, P.key, P.n);
        }
        if (C->iters > 0 && P.nmov > 1u) {
            p_anneal(&P, C, S, wscale, &cur, &best);
            memcpy(P.mask, P.bmask, P.n);
            memcpy(P.key, P.bkey, P.n);
            p_occ_rebuild(&P);
            p_greedy(&P, C, S);
        }
        S->after = p_total(&P);
        if (S->after > S->start + 1e-9) {
            /* cannot happen with exact arithmetic; the guard keeps the promise under rounding */
            memcpy(P.mask, P.bmask, P.n);
            memcpy(P.key, P.bkey, P.n);
            S->after = p_total(&P);
        }
        for (i = 0; i < E->n; i++) {
            out[i] = E->chord[i];
            if (p_movable(E, i)) {
                out[i].mods[0] = P.mask[i];
                out[i].key[0] = P.key[i];
            }
        }
    }
    arena_reset(a, mark);
    return rc;
}
