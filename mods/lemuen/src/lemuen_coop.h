/* Lemuen 0.2.0: co-op through HellForge's mod channel (SDK 16: send, HF_ON_MESSAGE, entity_of_network, session).
 *
 * In co-op the host's machine owns the enemies, so the hits of a joiner's Lemuen are applied (and their damage events
 * raised) on the host's machine, where her skills do not run. So:
 *   - a joiner's Lemuen sends her state to the host (STATE: Salve Etiam, Modus Extraditio, Honesta Missio's lock and its
 *     build-up, her marks, each enemy by its network id, with her own multipliers);
 *   - the host's Lemuen (whichever kit the host plays) applies that state to the joiner's hits (the hit's creditor is
 *     the joiner's peer id, the sender of her STATE), and tells her what each hit dealt (HIT: her damage numbers, and
 *     Honesta Missio's build-up spent);
 *   - Memento Mori on a joiner asks the host for the shells (SHELLS: her marked enemies), since only the host may spawn.
 * Solo with coop_loopback = 1 runs her own kit through the same path (encoded, decoded, applied, echoed) on one machine.
 *
 * Included by lemuen.c after its skills and before the damage event. */
#ifndef LEMUEN_COOP_H
#define LEMUEN_COOP_H

#define CO_MAGIC 0x4Cu   /* 'L' */
#define CO_VER 1u
enum { CO_STATE = 1, CO_HIT = 2, CO_SHELLS = 3 };
#define CO_NETS 200      /* marked enemies (and riders) a STATE carries */
#define CO_PEERS 4
#define CO_LOOP 1ull     /* the loopback's peer id */

/* the session: solo, host or joiner (SDK 16's session; older: solo) */
static uint32_t g_role;
static int g_coopApi;   /* send, entity_of_network and session are there */
static void RoleFrame(void)
{
    if (!g_coopApi) { g_role = HF_SESSION_SOLO; return; }
    HfSession s;
    memset(&s, 0, sizeof s);
    hf->session(self, &s);
    if (s.role != g_role) Log("co-op: this machine is now %s", s.role == HF_SESSION_HOST ? "the host" : s.role == HF_SESSION_JOINER ? "a joiner" : "solo");
    g_role = s.role;
}
static int Joiner(void) { return s_coopOn && g_coopApi && g_role == HF_SESSION_JOINER; }
static int Loop(void) { return s_coopOn && s_coopLoop && g_role == HF_SESSION_SOLO; }

/* entity <-> network id, from the spawn events (the same network id names an enemy on every machine) */
#define NETMAP 8192
static uint32_t g_nmE[NETMAP], g_nmN[NETMAP];
static void NetPut(uint32_t e, uint32_t n)
{
    if (!e) return;
    unsigned h = (e * 2654435761u) & (NETMAP - 1);
    for (unsigned i = 0; i < NETMAP; i++)
    {
        unsigned k = (h + i) & (NETMAP - 1);
        if (g_nmE[k] == e || !g_nmE[k]) { g_nmE[k] = e; g_nmN[k] = n; return; }
    }
}
static uint32_t NetOf(uint32_t e)
{
    if (!e) return 0;
    unsigned h = (e * 2654435761u) & (NETMAP - 1);
    for (unsigned i = 0; i < NETMAP; i++)
    {
        unsigned k = (h + i) & (NETMAP - 1);
        if (g_nmE[k] == e) return g_nmN[k];
        if (!g_nmE[k]) return 0;
    }
    return 0;
}
static uint32_t EntityOfNet(uint32_t n)
{
    if (!n) return 0;
    if (g_coopApi)
    {
        uint32_t e = hf->entity_of_network(self, n);
        if (e != 0xFFFFFFFFu) return e;
    }
    for (unsigned k = 0; k < NETMAP; k++) if (g_nmE[k] && g_nmN[k] == n) return g_nmE[k];
    return 0;
}

/* ---- the wire --------------------------------------------------------------------------------------------------- */
typedef struct CoState
{
    uint8_t flags;            /* 1 Salve Etiam, 2 Modus Extraditio */
    float s1Mult, modusMult, markMult;
    uint32_t s2Net, s2PrevNet;
    float s2Mult, s2PrevMult; /* the build-up her next shot on it deals (0: none) */
    uint32_t spends;          /* Honesta Missio build-ups the host has told her were spent (its HITs) */
    uint32_t n;
    uint32_t net[CO_NETS];
} CoState;
typedef struct Wr { uint8_t* p; uint32_t n, cap; } Wr;
static void W(Wr* w, const void* v, uint32_t b) { if (w->n + b <= w->cap) memcpy(w->p + w->n, v, b); w->n += b; }
typedef struct Rd { const uint8_t* p; uint32_t n, at; int bad; } Rd;
static void R(Rd* r, void* v, uint32_t b) { if (r->at + b > r->n) { r->bad = 1; memset(v, 0, b); return; } memcpy(v, r->p + r->at, b); r->at += b; }
static uint32_t Header(Wr* w, uint8_t type) { uint8_t h[3] = {CO_MAGIC, CO_VER, type}; W(w, h, 3); return 3; }

static uint32_t EncodeState(const CoState* s, uint8_t* out, uint32_t cap)
{
    Wr w = {out, 0, cap};
    Header(&w, CO_STATE);
    W(&w, &s->flags, 1); W(&w, &s->s1Mult, 4); W(&w, &s->modusMult, 4); W(&w, &s->markMult, 4);
    W(&w, &s->s2Net, 4); W(&w, &s->s2Mult, 4); W(&w, &s->s2PrevNet, 4); W(&w, &s->s2PrevMult, 4); W(&w, &s->spends, 4);
    uint16_t n = (uint16_t)s->n;
    W(&w, &n, 2);
    W(&w, s->net, 4u * n);
    return w.n <= cap ? w.n : 0;
}
static int DecodeState(Rd* r, CoState* s)
{
    memset(s, 0, sizeof *s);
    R(r, &s->flags, 1); R(r, &s->s1Mult, 4); R(r, &s->modusMult, 4); R(r, &s->markMult, 4);
    R(r, &s->s2Net, 4); R(r, &s->s2Mult, 4); R(r, &s->s2PrevNet, 4); R(r, &s->s2PrevMult, 4); R(r, &s->spends, 4);
    uint16_t n = 0;
    R(r, &n, 2);
    if (n > CO_NETS) { r->bad = 1; return 0; }
    s->n = n;
    for (uint32_t i = 0; i < n; i++) R(r, &s->net[i], 4);
    /* sane numbers only: a broken or hostile message must not make anyone a god */
    float* f[] = {&s->s1Mult, &s->modusMult, &s->markMult, &s->s2Mult, &s->s2PrevMult};
    for (unsigned i = 0; i < 5; i++) if (!isfinite(*f[i]) || *f[i] < 0 || *f[i] > 50) *f[i] = 1;
    return !r->bad;
}

/* ---- the joiner's side (and the loopback's): her state out, the host's answers in ------------------------------ */
static uint32_t g_spends;           /* HITs that spent her Honesta Missio build-up */
static double g_stateAt = -100;
static uint8_t g_stateLast[HF_MESSAGE_MAX];
static uint32_t g_stateLastN;
static unsigned g_coSent, g_coRefused, g_coHits, g_coShells;
static void Deliver(uint64_t from, const uint8_t* data, uint32_t bytes);   /* the host's side, below */
static void Send(uint64_t to, const uint8_t* data, uint32_t n)
{
    if (Loop()) { Deliver(CO_LOOP, data, n); g_coSent++; return; }
    if (hf->send(self, to, data, n)) { if (g_coRefused++ < 5) Log("co-op: a message (%u bytes) refused (the log above says why)", n); }
    else g_coSent++;
}
static void StateOf(CoState* s)
{
    memset(s, 0, sizeof *s);
    s->flags = (uint8_t)((g_s1On ? 1 : 0) | (g_landedAt >= 0 && g_now - g_landedAt >= s_modusAfter ? 2 : 0));
    s->s1Mult = s_s1Mult; s->modusMult = s_modusMult; s->markMult = s_markMult;
    if (g_s2On && g_s2Target && g_s2Held > 0) { s->s2Net = NetOf(g_s2Target); s->s2Mult = (float)S2Mult(); }
    if (g_s2On && g_s2PrevTarget && g_now - g_s2PrevAt < 1.5 && g_s2PrevHeld > 0)
    {
        double keep = g_s2Held; g_s2Held = g_s2PrevHeld;
        s->s2PrevNet = NetOf(g_s2PrevTarget); s->s2PrevMult = (float)S2Mult();
        g_s2Held = keep;
    }
    s->spends = g_spends;
    for (int i = 0; i < MARKS && s->n < CO_NETS; i++)
    {
        const Mark* m = &g_marks[i];
        if (!m->e || m->until <= g_now) continue;
        uint32_t n = NetOf(m->e);
        if (n) s->net[s->n++] = n;
        for (int j = 0; j < MATES && s->n < CO_NETS; j++) if (m->mate[j] && (n = NetOf(m->mate[j]))) s->net[s->n++] = n;
    }
}
/* her state, when it changed (at most 10 a second) or once a second */
static void StateFrame(void)
{
    if (!Joiner() && !Loop()) return;
    if (g_now - g_stateAt < 0.1) return;
    CoState s;
    StateOf(&s);
    uint8_t buf[HF_MESSAGE_MAX];
    uint32_t n = EncodeState(&s, buf, sizeof buf);
    if (!n) return;
    if (n == g_stateLastN && !memcmp(buf, g_stateLast, n) && g_now - g_stateAt < 1.0) return;
    memcpy(g_stateLast, buf, n); g_stateLastN = n;
    g_stateAt = g_now;
    Send(HF_TO_HOST, buf, n);
}
/* Memento Mori on a joiner: the host drops the shells */
static int AskShells(const uint32_t* e, int n)
{
    if (!Joiner() && !Loop()) return 0;
    uint8_t buf[HF_MESSAGE_MAX];
    Wr w = {buf, 0, sizeof buf};
    Header(&w, CO_SHELLS);
    float h = s_s3Height, every = s_s3Every;
    int32_t shell = s_s3Shell;
    W(&w, &h, 4); W(&w, &every, 4); W(&w, &shell, 4);
    uint8_t m = 0;
    uint32_t nets[32];
    for (int i = 0; i < n && m < 32; i++) if ((nets[m] = NetOf(e[i]))) m++;
    W(&w, &m, 1);
    W(&w, nets, 4u * m);
    Send(HF_TO_HOST, buf, w.n);
    Log("Memento Mori (co-op): the host is asked for %u shells", m);
    return 1;
}
/* a HIT from the host: what her shot dealt */
static void OnHit(Rd* r)
{
    uint32_t net; int32_t base, dealt; uint8_t usedS2; float pos[3], xMark, xS2;
    R(r, &net, 4); R(r, &base, 4); R(r, &dealt, 4); R(r, &usedS2, 1); R(r, pos, 12); R(r, &xMark, 4); R(r, &xS2, 4);
    if (r->bad) return;
    g_coHits++;
    if (usedS2)
    {
        g_spends++;
        uint32_t e = EntityOfNet(net);
        if (e && e == g_s2Target) g_s2Held = 0;
        if (e && e == g_s2PrevTarget) g_s2PrevTarget = 0;
        g_held++;
    }
    if (kb_kit_on && g_inMission && xMark * xS2 >= s_numbersAt - 0.0001)
    {
        HfDamageEvent d;
        memset(&d, 0, sizeof d);
        memcpy(d.position, pos, 12);
        d.amount = dealt;
        NoteHit(&d, xS2 != 1);
    }
    static unsigned told;
    if (told++ < 12) Log("co-op: the host says her hit on network %u dealt %d (from %d; mark x%.2f, Honesta Missio x%.2f)", net, dealt, base, (double)xMark, (double)xS2);
}

/* ---- the host's side: joiners' states, their hits, their shells --------------------------------------------------- */
typedef struct Peer
{
    uint64_t id;
    double at;          /* when its last STATE came */
    CoState s;
    uint32_t spent;     /* Honesta Missio build-ups spent here (HITs sent with it) */
} Peer;
static Peer g_peer[CO_PEERS];
static SRWLOCK g_peerLock = SRWLOCK_INIT;
static Peer* PeerOf(uint64_t id, int make)
{
    Peer* free_ = NULL;
    for (int i = 0; i < CO_PEERS; i++)
    {
        if (g_peer[i].id == id && id) return &g_peer[i];
        if (!free_ && (!g_peer[i].id || g_now - g_peer[i].at > 60)) free_ = &g_peer[i];
    }
    if (!make || !free_) return NULL;
    memset(free_, 0, sizeof *free_);
    free_->id = id;
    return free_;
}
/* the shells joiners asked for: network ids, dropped one at a time */
static struct { uint32_t net; float h, every; int32_t shell; } g_coShell[64];
static int g_ncoShell;
static double g_coShellNext;
/* HITs to send back (the damage event's thread queues them; the game thread sends) */
typedef struct { uint64_t to; uint32_t net; int32_t base, dealt; uint8_t usedS2; float pos[3], xMark, xS2; } HitBack;
static HitBack g_hb[64];
static volatile LONG g_hbHead, g_hbTail;

static void Deliver(uint64_t from, const uint8_t* data, uint32_t bytes)
{
    Rd r = {data, bytes, 0, 0};
    uint8_t h[3];
    R(&r, h, 3);
    if (r.bad || h[0] != CO_MAGIC || h[1] != CO_VER) { static unsigned told; if (told++ < 5) Log("co-op: a message of another version or kind (%u bytes) left alone", bytes); return; }
    if (h[2] == CO_HIT) { if (from == CO_LOOP || Joiner()) OnHit(&r); return; }
    if (g_role != HF_SESSION_HOST && !(from == CO_LOOP && Loop())) return;   /* states and shells are for the host */
    if (h[2] == CO_STATE)
    {
        CoState s;
        if (!DecodeState(&r, &s)) return;
        AcquireSRWLockExclusive(&g_peerLock);
        Peer* p = PeerOf(from, 1);
        if (p)
        {
            int first = p->at == 0;
            p->s = s; p->at = g_now;
            if (first) Log("co-op: a joiner's Lemuen (peer %016llx) reports in: her hits on this machine take her skills", (unsigned long long)from);
        }
        ReleaseSRWLockExclusive(&g_peerLock);
    }
    else if (h[2] == CO_SHELLS)
    {
        float hh, every; int32_t shell; uint8_t m;
        R(&r, &hh, 4); R(&r, &every, 4); R(&r, &shell, 4); R(&r, &m, 1);
        if (r.bad || !isfinite(hh) || !isfinite(every) || hh < 5 || hh > 500 || every < 0.05f || every > 10 || shell <= 0) return;
        int added = 0;
        for (uint8_t i = 0; i < m && g_ncoShell < 64; i++)
        {
            uint32_t n;
            R(&r, &n, 4);
            if (r.bad) break;
            g_coShell[g_ncoShell].net = n; g_coShell[g_ncoShell].h = hh; g_coShell[g_ncoShell].every = every; g_coShell[g_ncoShell].shell = shell;
            g_ncoShell++; added++;
        }
        if (g_coShellNext < g_now) g_coShellNext = g_now;
        Log("co-op: Memento Mori from peer %016llx: %d shells", (unsigned long long)from, added);
    }
}
static void OnMessage(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    const HfMessageEvent* m = (const HfMessageEvent*)info;
    if (!s_coopOn) return;
    Deliver(m->from, m->data, m->bytes < HF_MESSAGE_MAX ? m->bytes : HF_MESSAGE_MAX);
}
static int HasNet(const CoState* s, uint32_t n) { for (uint32_t i = 0; n && i < s->n; i++) if (s->net[i] == n) return 1; return 0; }
/* a joiner's hit on this machine (or hers, in the loopback): her skills by her state. 1 when it was hers */
static int PeerHit(HfDamageEvent* d, uint64_t peerId)
{
    if (d->amount <= 0) return 0;
    AcquireSRWLockExclusive(&g_peerLock);
    Peer* p = PeerOf(peerId, 0);
    if (!p || g_now - p->at > 5) { ReleaseSRWLockExclusive(&g_peerLock); return 0; }
    CoState* s = &p->s;
    uint32_t net = NetOf(d->target);
    double mult = 1, xMark = 1, xS2 = 1;
    int32_t base = d->amount;
    uint8_t usedS2 = 0;
    if (HasNet(s, net)) mult *= xMark = s->markMult;
    if (s->flags & 2) mult *= s->modusMult;
    int shot = d->kind == 0 || d->kind == 1 || d->kind == 3;
    if (shot)
    {
        if (s->flags & 1) mult *= s->s1Mult;
        if (net && s->spends == p->spent)   /* her last spend is known to her: a fresh build-up */
        {
            if (net == s->s2Net && s->s2Mult > 0) xS2 = s->s2Mult;
            else if (net == s->s2PrevNet && s->s2PrevMult > 0) xS2 = s->s2PrevMult;
            if (xS2 != 1) { mult *= xS2; usedS2 = 1; p->spent++; }
        }
    }
    ReleaseSRWLockExclusive(&g_peerLock);
    if (mult != 1) ScaleDamage(d, mult);
    /* tell her what it dealt */
    LONG head = g_hbHead;
    if (head - g_hbTail < 64)
    {
        HitBack* b = &g_hb[head & 63];
        b->to = peerId; b->net = net; b->base = base; b->dealt = d->amount; b->usedS2 = usedS2;
        memcpy(b->pos, d->position, 12); b->xMark = (float)xMark; b->xS2 = (float)xS2;
        InterlockedIncrement(&g_hbHead);
    }
    static unsigned told;
    if (told++ < 12) Log("co-op: peer %016llx's hit on entity %u (network %u, kind %u): %d -> %d (x%.2f)", (unsigned long long)peerId, d->target, net, d->kind, base, d->amount, mult);
    return 1;
}
/* the game thread: HITs back (at most 30 a second), the joiners' shells */
static void HostFrame(void)
{
    static double tokens = 30;
    static double last;
    tokens += (g_now - last) * 30; last = g_now;
    if (tokens > 30) tokens = 30;
    while (g_hbTail != g_hbHead)
    {
        HitBack* b = &g_hb[g_hbTail & 63];
        if (tokens >= 1 || b->usedS2)   /* a spend always goes: her build-up must not stay armed */
        {
            uint8_t buf[64];
            Wr w = {buf, 0, sizeof buf};
            Header(&w, CO_HIT);
            W(&w, &b->net, 4); W(&w, &b->base, 4); W(&w, &b->dealt, 4); W(&w, &b->usedS2, 1); W(&w, b->pos, 12); W(&w, &b->xMark, 4); W(&w, &b->xS2, 4);
            if (b->to == CO_LOOP) Deliver(CO_LOOP, buf, w.n);
            else if (!hf->send(self, b->to, buf, w.n)) g_coSent++;
            tokens -= 1;
        }
        InterlockedIncrement(&g_hbTail);
    }
    while (g_ncoShell > 0 && g_now >= g_coShellNext)
    {
        int i = g_ncoShell - 1;
        uint32_t e = EntityOfNet(g_coShell[i].net);
        float q[3];
        uint64_t res = e ? ResOf(e) : 0;
        if (!e || !res || !TargetPos(e, res, q)) { g_ncoShell--; continue; }
        float from[3] = {q[0] + ((float)rand() / RAND_MAX - 0.5f) * 2, q[1] + ((float)rand() / RAND_MAX - 0.5f) * 2, q[2] + g_coShell[i].h};
        float down[3] = {0, 0, -1};
        int r = hf->spawn_projectile(self, (uint32_t)g_coShell[i].shell, from, down);
        if (r == HF_ACT_WAITING) { g_coShellNext = g_now + 0.1; return; }
        if (Act("a joiner's Memento Mori shell", r)) g_coShells++;
        g_coShellNext = g_now + g_coShell[i].every;
        g_ncoShell--;
    }
}
/* every hit this machine applies: a joiner's (by its creditor), whatever kit this machine plays */
static void OnDamageCoop(HfMod* mod, const HfEventInfo* info, void* user)
{
    (void)mod; (void)user;
    HfDamagePreEvent* e = (HfDamagePreEvent*)(void*)info;
    HfDamageEvent* d = &e->damage;
    if (!s_coopOn || !g_inMission || e->cancel || g_role != HF_SESSION_HOST || !d->creditor) return;
    if (d->target == atomic_load(&g_avatar)) return;
    if (PeerHit(d, d->creditor)) return;
    /* a credited hit no joiner's Lemuen reported: the first few, with who has (to check the creditor is the sender's id) */
    static unsigned told;
    if (told < 6 && (d->kind == 0 || d->kind == 1) && g_peer[0].id)
    { told++; Log("co-op: a hit credited to peer %016llx (kind %u), no Lemuen state from it (states from %016llx %016llx)", (unsigned long long)d->creditor, d->kind, (unsigned long long)g_peer[0].id, (unsigned long long)g_peer[1].id); }
}
static void CoopReset(const char* why)
{
    if (g_coSent || g_coHits || g_coShells || g_coRefused)
        Log("%s: co-op: %u messages sent (%u refused), %u hits told back, %u joiners' shells", why, g_coSent, g_coRefused, g_coHits, g_coShells);
    g_coSent = g_coRefused = g_coHits = g_coShells = 0;
    g_ncoShell = 0;
    g_spends = 0;
    g_stateLastN = 0;
    g_stateAt = -100;
    g_hbTail = g_hbHead;
    AcquireSRWLockExclusive(&g_peerLock);
    memset(g_peer, 0, sizeof g_peer);
    ReleaseSRWLockExclusive(&g_peerLock);
}
static void CoopInit(const HfApi* api, HfMod* mod)
{
    g_coopApi = api->size >= offsetof(HfApi, share_set) + sizeof api->share_set && api->send && api->entity_of_network && api->session;
    if (!g_coopApi) { api->log(mod, "co-op off: this HellForge has no mod channel (SDK 16 or later needed): solo or host as before"); return; }
    if (!api->subscribe(mod, HF_ON_MESSAGE, OnMessage, NULL, 0) || !api->subscribe(mod, HF_ON_DAMAGE_PRE, OnDamageCoop, NULL, 0))
    { g_coopApi = 0; api->log(mod, "co-op off: the mod channel's events are unavailable"); return; }
    api->log(mod, "co-op ready (the mod channel)%s", s_coopLoop ? "; coop_loopback 1: solo runs her kit through it" : "");
}

#endif
