/* Net Probe (yui.net-probe): multiplayer research. Can a joined player's mod ask the host's mod to spawn something?
 * Both players run it (HellForge's mod sync brings it to the joiner) and send their logs. Build 25480438 only.
 *
 *   always    the session (solo, host, joined) and the game state; every player's peer id (HF_ON_PLAYER_SPAWN), and
 *             this machine's SteamID, to see whether peer ids are SteamIDs; Steam friends in Helldivers 2 (the fallback
 *             address book for the Steam channel); stratagems and explosions by other players (their creditor), and a
 *             count of spawns this machine simulates against the ones it only receives
 *   F5        Steam ping: a message to every known player over ISteamNetworkingMessages (Steam's own peer-to-peer,
 *             outside the game's netcode); replies are logged with the round trip
 *   F6        ask the host over Steam: "set off my last explosion 15 m above me"; the host's probe does it with
 *             HellForge's spawn_explosion and answers with the result (solo or hosting: done here)
 *   F7        try it from this machine: the game's own explosion request (Beelzebub's method) and HellForge's
 *             spawn_explosion, 15 m above you; the host's log shows whether either reached it
 *
 * The explosion is the type this machine last saw credited to you (throw a grenade or fire a launcher first), so it is
 * always one the game has loaded; 15 m up keeps it off everyone. Keys only count while the game window has focus.
 *
 * Methods: entity_resolve (Khi's signature, as Apache - Rise Above checks it), the unit API at [game+0x3326308]+0x18
 * (EXO Beacon Launcher, MIT), the explosion request (Beelzebub's Strat Pack, reused with his permission). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include "hellforge_mod.h"

#define BUILD "25480438"
#define VERSION "0.1.0"
static const HfApi* hf;
static HfMod* self;
static uintptr_t game, exe;
static int g_ready;            /* 1 bound, -1 refused */
static double g_now;

#define Log(...) hf->log(self, __VA_ARGS__)
static int RD(uintptr_t a, void* out, size_t n) { return a >= 0x10000 && a < 0x7FFFFFFF0000ull && hf->read(a, out, n) == 0; }
static uintptr_t P(uintptr_t a) { uintptr_t v = 0; return RD(a, &v, 8) && v >= 0x10000 && v < 0x7FFFFFFF0000ull ? v : 0; }
static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int Has(size_t off) { return hf->size >= off + sizeof(void*); }
#define HAS(f) Has(offsetof(HfApi, f))

/* ---- where the local Helldiver is ---------------------------------------------------------------------------------- */
typedef void* (*ResolveFn)(uint32_t);
typedef const float* (*LocalPosFn)(uint32_t, uint32_t);
static ResolveFn g_resolve;
static LocalPosFn g_localPos;
static atomic_uint g_avatar;
static uint64_t g_myPeer;
static int Hex(const char* h, uint8_t* out)
{
    int n = 0;
    for (; h[0] && h[1]; h += 2) { unsigned v; sscanf(h, "%2x", &v); out[n++] = (uint8_t)v; }
    return n;
}
static int MyPosition(float out[3])
{
    uint32_t me = atomic_load(&g_avatar);
    if (!me || !g_resolve || !g_localPos) return 0;
    void* o = g_resolve(me);
    uint8_t h[24];
    if (!o || !RD((uintptr_t)o, h, 24) || U32(h + 8) != me) return 0;
    const float* p = g_localPos(U32(h + 12), 0);
    if (!p || !RD((uintptr_t)p, out, 12)) return 0;
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

/* ---- the game's explosion request (Beelzebub's Strat Pack 1.8.3, with his permission) ---------------------------- */
#define XQ_GLOBAL 54973784u
#define XQ_COUNT 32u
#define XQ_CAPACITY 256u
#define XQ_REQUEST 20712064u
typedef void (*XqRequestFn)(void* queue, const float* position, uint32_t type, uint32_t source, uint32_t owner, uint64_t peer,
    uint32_t a7, const float* a8, uint8_t a9, uint32_t a10, const uint32_t* a11, const float* a12, const float* a13,
    uint8_t a14, uint32_t a15);
static const struct { uint32_t rva; const char* hex; } kXqPins[] = {
    {20712064, "40534883ec308b41204c8bda488bd93d000100000f8369020000"},
    {20712079, "3d00010000"},
    {20712128, "4969fa98000000"},
    {20712145, "f20f11440f28"},
    {20712155, "4489440f34"},
    {20712207, "4889441f40"},
    {20712229, "44894c1f38"},
    {20712244, "894c1f3c"},
    {9220491, "488b0dc623ba02"},
};
static XqRequestFn g_xq;
static int XqBind(void)
{
    for (size_t i = 0; i < sizeof kXqPins / sizeof kXqPins[0]; i++)
    {
        uint8_t want[32], got[32];
        int n = Hex(kXqPins[i].hex, want);
        if (hf->read_code(game + kXqPins[i].rva, got, n) || memcmp(got, want, n)) return 0;
    }
    g_xq = (XqRequestFn)(game + XQ_REQUEST);
    return 1;
}
static int XqRequest(uint32_t type, const float at[3], uint32_t owner, uint64_t peer)
{
    uint64_t queue = 0;
    uint32_t n = 0;
    if (!g_xq || hf->read(game + XQ_GLOBAL, &queue, 8) || queue < 0x10000 || hf->read(queue + XQ_COUNT, &n, 4) || n >= XQ_CAPACITY - 8)
        return 0;
    g_xq((void*)queue, at, type, 0, owner, peer, 0, NULL, 1, 0, NULL, NULL, NULL, 0, 0);
    return 1;
}

/* ---- Steam (steam_api64.dll's flat API, looked up at run time) ---------------------------------------------------- */
#define APP_HD2 553850u
#define CHANNEL 0x594E5031  /* "YNP1": this probe's channel; the game's traffic never uses it */
#define SEND_RELIABLE 8
typedef struct { int32_t type; int32_t size; uint64_t steam; uint8_t rest[120]; } SteamIdentity; /* SteamNetworkingIdentity, 136 bytes */
typedef void* (*AccessorFn)(void);
typedef int32_t (*GetHUserFn)(void);
typedef void* (*FindIfaceFn)(int32_t, const char*);
typedef uint64_t (*GetSteamIdFn)(void*);
typedef int (*SendFn)(void*, const SteamIdentity*, const void*, uint32_t, int, int);
typedef int (*ReceiveFn)(void*, int, void**, int);
typedef uint8_t (*AcceptFn)(void*, const SteamIdentity*);
typedef int (*SessionInfoFn)(void*, const SteamIdentity*, void*, void*);
typedef void (*ReleaseFn)(void*);
typedef int (*FriendCountFn)(void*, int);
typedef uint64_t (*FriendByIndexFn)(void*, int, int);
typedef uint8_t (*FriendGameFn)(void*, uint64_t, void*);
static struct {
    HMODULE dll;
    void *user, *messages, *friends;
    GetSteamIdFn get_id;
    SendFn send;
    ReceiveFn receive;
    AcceptFn accept;
    SessionInfoFn session;
    ReleaseFn release;
    FriendCountFn friend_count;
    FriendByIndexFn friend_at;
    FriendGameFn friend_game;
    uint64_t me;
    int state;   /* 0 not tried, 1 ready, -1 off */
} st;
static void* Iface(const char* const* flat, const char* const* versioned)
{
    for (; *flat; flat++) { AccessorFn f = (AccessorFn)GetProcAddress(st.dll, *flat); void* p = f ? f() : NULL; if (p) return p; }
    GetHUserFn hu = (GetHUserFn)GetProcAddress(st.dll, "SteamAPI_GetHSteamUser");
    FindIfaceFn fi = (FindIfaceFn)GetProcAddress(st.dll, "SteamInternal_FindOrCreateUserInterface");
    if (!hu || !fi) return NULL;
    for (; *versioned; versioned++) { void* p = fi(hu(), *versioned); if (p) { Log("steam: %s", *versioned); return p; } }
    return NULL;
}
static int SteamOpen(void)
{
    if (st.state) return st.state > 0;
    st.state = -1;
    st.dll = GetModuleHandleA("steam_api64.dll");
    if (!st.dll) { Log("steam: steam_api64.dll is not loaded in this game: the Steam channel is off"); return 0; }
    static const char* uf[] = {"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", "SteamAPI_SteamUser_v020", NULL};
    static const char* uv[] = {"SteamUser023", "SteamUser022", "SteamUser021", "SteamUser020", NULL};
    static const char* mf[] = {"SteamAPI_SteamNetworkingMessages_SteamAPI_v002", "SteamAPI_SteamNetworkingMessages_v002", NULL};
    static const char* mv[] = {"SteamNetworkingMessages002", NULL};
    static const char* ff[] = {"SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v015", NULL};
    static const char* fv[] = {"SteamFriends017", "SteamFriends015", NULL};
    st.user = Iface(uf, uv);
    st.messages = Iface(mf, mv);
    st.friends = Iface(ff, fv);
    st.get_id = (GetSteamIdFn)GetProcAddress(st.dll, "SteamAPI_ISteamUser_GetSteamID");
    st.send = (SendFn)GetProcAddress(st.dll, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    st.receive = (ReceiveFn)GetProcAddress(st.dll, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    st.accept = (AcceptFn)GetProcAddress(st.dll, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    st.session = (SessionInfoFn)GetProcAddress(st.dll, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
    st.release = (ReleaseFn)GetProcAddress(st.dll, "SteamAPI_SteamNetworkingMessage_t_Release");
    st.friend_count = (FriendCountFn)GetProcAddress(st.dll, "SteamAPI_ISteamFriends_GetFriendCount");
    st.friend_at = (FriendByIndexFn)GetProcAddress(st.dll, "SteamAPI_ISteamFriends_GetFriendByIndex");
    st.friend_game = (FriendGameFn)GetProcAddress(st.dll, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    if (st.user && st.get_id) st.me = st.get_id(st.user);
    Log("steam: user %s (SteamID %llu), messages %s (send %s, receive %s, accept %s, release %s), friends %s", st.user ? "yes" : "NO",
        (unsigned long long)st.me, st.messages ? "yes" : "NO", st.send ? "yes" : "no", st.receive ? "yes" : "no", st.accept ? "yes" : "no",
        st.release ? "yes" : "no", st.friends && st.friend_count && st.friend_at && st.friend_game ? "yes" : "NO");
    if (!st.messages || !st.send || !st.receive) { Log("steam: no peer-to-peer messages: the Steam channel is off"); return 0; }
    st.state = 1;
    return 1;
}
static void Ident(SteamIdentity* id, uint64_t steam) { memset(id, 0, sizeof *id); id->type = 16; id->size = 8; id->steam = steam; }

/* ---- the players this probe knows (peer ids, SteamIDs) ------------------------------------------------------------ */
typedef struct { uint64_t id; int from_peer, from_friend, from_message; double last_heard; } Peer;
static Peer g_peers[16];
static int g_npeers;
static Peer* Know(uint64_t id, int how)
{
    if (!id || id == st.me) return NULL;
    for (int i = 0; i < g_npeers; i++)
        if (g_peers[i].id == id)
        {
            if (how == 0) g_peers[i].from_peer = 1; else if (how == 1) g_peers[i].from_friend = 1; else g_peers[i].from_message = 1;
            return &g_peers[i];
        }
    if (g_npeers >= 16) return NULL;
    Peer* p = &g_peers[g_npeers++];
    memset(p, 0, sizeof *p);
    p->id = id;
    if (how == 0) p->from_peer = 1; else if (how == 1) p->from_friend = 1; else p->from_message = 1;
    Log("players: now knows %llu (%s)%s", (unsigned long long)id, how == 0 ? "a peer id from the game" : how == 1 ? "a Steam friend in Helldivers 2" :
        "it messaged us", (id >> 52) == 0x011 ? ", shaped like a SteamID" : "");
    return p;
}
static void FriendsInGame(void)
{
    if (st.state != 1 || !st.friends || !st.friend_count || !st.friend_at || !st.friend_game) return;
    int n = st.friend_count(st.friends, 4), playing = 0;
    for (int i = 0; i < n && i < 2000; i++)
    {
        uint64_t f = st.friend_at(st.friends, i, 4);
        _Alignas(8) uint8_t info[64] = {0};
        if (!f || !st.friend_game(st.friends, f, info)) continue;
        uint64_t gameId; memcpy(&gameId, info, 8);
        if ((uint32_t)(gameId & 0xFFFFFF) != APP_HD2) continue;
        uint64_t lobby; memcpy(&lobby, info + 16, 8);
        playing++;
        Peer* p = Know(f, 1);
        static uint64_t lastLobby[16];
        if (p && lastLobby[p - g_peers] != lobby) { lastLobby[p - g_peers] = lobby; Log("friends: %llu plays Helldivers 2, lobby %llu", (unsigned long long)f, (unsigned long long)lobby); }
    }
    static int lastPlaying = -1;
    if (playing != lastPlaying) { lastPlaying = playing; Log("friends: %d of %d play Helldivers 2 now", playing, n); }
}

/* ---- the messages ---------------------------------------------------------------------------------------------------- */
#define MSG_HELLO 1
#define MSG_PING 2
#define MSG_PONG 3
#define MSG_SPAWN 4
#define MSG_SPAWN_DONE 5
typedef struct {
    uint32_t magic;     /* 'YNP1' */
    uint32_t type, seq, sent_ms;
    uint32_t role;      /* the sender's session role */
    uint32_t explosion;
    float position[3];
    int32_t result;
    uint64_t peer;      /* the sender's game peer id */
} Msg;
static uint32_t g_seq;
static int Send(uint64_t to, Msg* m)
{
    if (st.state != 1) return 0;
    HfSession s = {0};
    hf->session(self, &s);
    m->magic = 0x31504E59u; m->role = s.role; m->peer = g_myPeer;
    if (!m->seq) m->seq = ++g_seq;
    if (!m->sent_ms) m->sent_ms = GetTickCount();
    SteamIdentity id;
    Ident(&id, to);
    return st.send(st.messages, &id, m, sizeof *m, SEND_RELIABLE, CHANNEL);
}
static void SpawnHere(uint32_t type, const float at[3], const char* why, int32_t* result)
{
    int r = HAS(spawn_explosion) ? hf->spawn_explosion(self, type, at) : -1;
    *result = r;
    Log("%s: spawn_explosion type %u at %.1f %.1f %.1f -> %d (%s)", why, type, at[0], at[1], at[2], r,
        r == 1 ? "done" : r == 2 ? "requested" : r == 3 ? "waiting for packages" : r == 4 ? "queued" : r == 0 ? "refused" : "not in this SDK");
}
static void Receive(void)
{
    if (st.state != 1) return;
    void* in[16];
    int n = st.receive(st.messages, CHANNEL, in, 16);
    for (int i = 0; i < n; i++)
    {
        uint8_t* raw = in[i];
        void* data; int32_t size; uint64_t from;
        memcpy(&data, raw, 8); memcpy(&size, raw + 8, 4); memcpy(&from, raw + 24, 8);
        Msg m;
        if (size == (int32_t)sizeof m && data) memcpy(&m, data, sizeof m); else memset(&m, 0, sizeof m);
        if (m.magic != 0x31504E59u) Log("steam: %d bytes from %llu, not ours", size, (unsigned long long)from);
        else
        {
            Peer* p = Know(from, 2);
            if (p) p->last_heard = g_now;
            uint32_t late = GetTickCount() - m.sent_ms;
            static const char* roles[] = {"solo", "host", "joined"};
            const char* role = m.role < 3 ? roles[m.role] : "?";
            if (m.type == MSG_HELLO)
            {
                static double quiet[16];
                if (p && g_now >= quiet[p - g_peers]) { quiet[p - g_peers] = g_now + 30; Log("steam: hello from %llu (%s, peer %llu)", (unsigned long long)from, role, (unsigned long long)m.peer); }
            }
            else if (m.type == MSG_PING)
            {
                Log("steam: ping %u from %llu (%s, peer %llu): answering", m.seq, (unsigned long long)from, role, (unsigned long long)m.peer);
                Msg r = m; r.type = MSG_PONG; r.peer = 0;
                Send(from, &r);
            }
            else if (m.type == MSG_PONG)
                Log("steam: pong %u from %llu (%s): round trip %u ms", m.seq, (unsigned long long)from, role, late);
            else if (m.type == MSG_SPAWN)
            {
                HfSession s = {0};
                hf->session(self, &s);
                Log("steam: %llu (%s, peer %llu) asks for explosion %u at %.1f %.1f %.1f, %u ms after sending; this machine is %s",
                    (unsigned long long)from, role, (unsigned long long)m.peer, m.explosion, m.position[0], m.position[1], m.position[2], late,
                    s.role < 3 ? roles[s.role] : "?");
                Msg r = m; r.type = MSG_SPAWN_DONE;
                if (s.role == HF_SESSION_JOINER) { r.result = -2; Log("  not the host: refused"); }
                else SpawnHere(m.explosion, m.position, "  for them", &r.result);
                r.sent_ms = m.sent_ms;
                Send(from, &r);
            }
            else if (m.type == MSG_SPAWN_DONE)
                Log("steam: the host %llu answered request %u: %d (%s), %u ms round trip", (unsigned long long)from, m.seq, m.result,
                    m.result == 1 ? "done" : m.result == 2 ? "requested: look 15 m above you" : m.result == -2 ? "it is not the host" : m.result == 0 ? "refused" : "see its log", late);
            else Log("steam: unknown message type %u from %llu", m.type, (unsigned long long)from);
        }
        if (st.release) st.release(raw);
        else { ReleaseFn rel; memcpy(&rel, raw + 184, 8); if (rel) rel(raw); }
    }
}
static void Hello(void)
{
    if (st.state != 1) return;
    HfSession s = {0};
    hf->session(self, &s);
    static int lastR[16], lastState[16], primed;
    if (!primed) { primed = 1; for (int i = 0; i < 16; i++) lastR[i] = lastState[i] = -99; }
    for (int i = 0; i < g_npeers; i++)
    {
        /* friends who are only friends (not a peer of this game, never messaged us) hear from us only in a lobby */
        if (!g_peers[i].from_peer && !g_peers[i].from_message && s.role == HF_SESSION_SOLO) continue;
        SteamIdentity id;
        Ident(&id, g_peers[i].id);
        if (st.accept) st.accept(st.messages, &id);
        Msg m = {0};
        m.type = MSG_HELLO;
        int r = Send(g_peers[i].id, &m);
        int state = st.session ? st.session(st.messages, &id, NULL, NULL) : -1;
        if (r != lastR[i] || state != lastState[i])
        {
            lastR[i] = r; lastState[i] = state;
            Log("steam: to %llu: send result %d (1 ok), session state %d (1 connecting, 2 finding route, 3 connected, 4/5 closed or failed)",
                (unsigned long long)g_peers[i].id, r, state);
        }
    }
}

/* ---- the game's side: events ---------------------------------------------------------------------------------------- */
static uint32_t g_lastBoom;    /* the last explosion type credited to this machine's player */
static double g_watchUntil;    /* after F7: log every explosion for a few seconds */
static unsigned g_spawnLocal, g_spawnRemote;
static void OnPlayer(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfPlayerEvent* p = (const HfPlayerEvent*)info;
    if (p->local)
    {
        if (p->entity) atomic_store(&g_avatar, p->entity);
        if (p->peer != g_myPeer)
            Log("you: peer %llu, entity %u; this machine's SteamID %llu: %s", (unsigned long long)p->peer, p->entity, (unsigned long long)st.me,
                p->peer == st.me ? "THE SAME (peer ids are SteamIDs)" : "different");
        g_myPeer = p->peer;
    }
    else
    {
        static uint64_t seen[16];
        int known = 0;
        for (int i = 0; i < 16; i++) if (seen[i] == p->peer) known = 1;
        if (!known) { for (int i = 0; i < 16; i++) if (!seen[i]) { seen[i] = p->peer; break; } Log("player: peer %llu, entity %u (another machine's)", (unsigned long long)p->peer, p->entity); }
        if ((p->peer >> 52) == 0x011) Know(p->peer, 0);
    }
}
static void OnExplosion(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfExplosionEvent* e = (const HfExplosionEvent*)info;
    if (g_myPeer && e->creditor == g_myPeer)
    {
        if (e->type != g_lastBoom) Log("explosion: your type %u (F6/F7 will use it)", e->type);
        g_lastBoom = e->type;
    }
    static unsigned lines;
    int other = e->creditor && e->creditor != g_myPeer;
    if ((other || g_now < g_watchUntil) && lines < 300)
    {
        lines++;
        Log("explosion: type %u at %.1f %.1f %.1f, source %u owner %u, credited to %llu%s", e->type, e->position[0], e->position[1], e->position[2],
            e->source, e->owner, (unsigned long long)e->creditor, other ? " (another player)" : "");
    }
}
static void OnStratagem(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStratagemEvent* s = (const HfStratagemEvent*)info;
    Log("stratagem: type %u thrown by %u at %.1f %.1f %.1f%s", s->type, s->thrower, s->position[0], s->position[1], s->position[2],
        s->thrower == atomic_load(&g_avatar) ? " (you)" : "");
}
static void OnSpawn(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfSpawnEvent* s = (const HfSpawnEvent*)info;
    if (s->authority) g_spawnLocal++; else g_spawnRemote++;
}
static void OnState(HfMod* mod, const HfEventInfo* info, void* user)
{
    const HfStateEvent* s = (const HfStateEvent*)info;
    static const char* names[] = {"none", "splash", "title", "ship", "mission", "prepare ship", "prepare mission"};
    Log("state: %s -> %s", s->from < 7 ? names[s->from] : "?", s->to < 7 ? names[s->to] : "?");
}

/* ---- keys ------------------------------------------------------------------------------------------------------------ */
static int Focused(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
static int Above(float at[3])
{
    if (!MyPosition(at)) { Log("  your position is not known (wait until you are on the ground)"); return 0; }
    at[2] += 15.0f;
    return 1;
}
static void KeyPing(void)
{
    if (st.state != 1) { Log("F5: the Steam channel is off"); return; }
    if (!g_npeers) { Log("F5: no other player known yet (no peer id shaped like a SteamID, no friend in Helldivers 2)"); return; }
    for (int i = 0; i < g_npeers; i++)
    {
        Msg m = {0};
        m.type = MSG_PING;
        int r = Send(g_peers[i].id, &m);
        Log("F5: ping %u to %llu: send result %d", m.seq, (unsigned long long)g_peers[i].id, r);
    }
}
static void KeyAsk(void)
{
    HfSession s = {0};
    hf->session(self, &s);
    float at[3];
    if (!g_lastBoom) { Log("F6: no explosion of yours seen yet: throw a grenade first"); return; }
    if (!Above(at)) return;
    if (s.role != HF_SESSION_JOINER) { int32_t r; SpawnHere(g_lastBoom, at, "F6 (this machine hosts)", &r); return; }
    if (st.state != 1) { Log("F6: the Steam channel is off"); return; }
    int sent = 0;
    for (int i = 0; i < g_npeers; i++)
    {
        Msg m = {0};
        m.type = MSG_SPAWN; m.explosion = g_lastBoom; memcpy(m.position, at, 12);
        int r = Send(g_peers[i].id, &m);
        Log("F6: asked %llu for explosion %u at %.1f %.1f %.1f (request %u): send result %d", (unsigned long long)g_peers[i].id, g_lastBoom,
            at[0], at[1], at[2], m.seq, r);
        sent++;
    }
    if (!sent) Log("F6: nobody to ask (no other player known)");
}
static void KeyDirect(void)
{
    float at[3];
    if (!g_lastBoom) { Log("F7: no explosion of yours seen yet: throw a grenade first"); return; }
    if (!Above(at)) return;
    g_watchUntil = g_now + 3;
    int q = XqRequest(g_lastBoom, at, atomic_load(&g_avatar), g_myPeer);
    Log("F7: the game's explosion request, type %u at %.1f %.1f %.1f credited to %llu: %s", g_lastBoom, at[0], at[1], at[2],
        (unsigned long long)g_myPeer, q ? "queued here" : g_xq ? "queue full" : "not bound");
    float b[3] = {at[0] + 4, at[1], at[2]};
    int32_t r;
    SpawnHere(g_lastBoom, b, "F7 HellForge's spawn_explosion (4 m east of it)", &r);
}

static void Tick(HfMod* mod, float dt, void* user)
{
    g_now += dt > 0 && dt < 1 ? dt : 0;
    if (g_ready != 1) return;
    SteamOpen();
    Receive();
    static double nextHello, nextFriends, nextCount;
    if (g_now >= nextFriends) { nextFriends = g_now + 15; FriendsInGame(); }
    if (g_now >= nextHello) { nextHello = g_now + 2; Hello(); }
    HfSession s = {0};
    hf->session(self, &s);
    static uint32_t lastRole = 99, lastVersion;
    if (s.role != lastRole || s.version != lastVersion)
    {
        static const char* roles[] = {"solo", "hosting", "joined"};
        Log("session: %s%s%s (version %u)", s.role < 3 ? roles[s.role] : "?", s.role == HF_SESSION_JOINER ? ", host " : "",
            s.role == HF_SESSION_JOINER ? s.host : "", s.version);
        lastRole = s.role; lastVersion = s.version;
    }
    if (g_now >= nextCount && (g_spawnLocal || g_spawnRemote))
    {
        nextCount = g_now + 30;
        Log("spawns in the last 30 s: %u simulated here, %u received from another machine", g_spawnLocal, g_spawnRemote);
        g_spawnLocal = g_spawnRemote = 0;
    }
    static int prev[3];
    int keys[3] = {VK_F5, VK_F6, VK_F7};
    int focus = Focused();
    for (int k = 0; k < 3; k++)
    {
        int d = focus && (GetAsyncKeyState(keys[k]) & 0x8000);
        if (d && !prev[k]) (k == 0 ? KeyPing : k == 1 ? KeyAsk : KeyDirect)();
        prev[k] = d;
    }
}

static void Loaded(HfMod* mod, HfEvent event, void* user)
{
    size_t gs = 0, xs = 0;
    game = hf->module_base("game.dll", &gs);
    exe = hf->module_base("helldivers2.exe", &xs);
    if (!game || !exe || gs != 74727424u || xs != 60719104u) { g_ready = -1; Log("REFUSED: not build " BUILD "'s modules"); return; }
    uint8_t want[32], got[32];
    int n = Hex("4883ec083b0d029f4a020f849b0000004c8b15412249024533c048895c241048", want);
    if (!hf->read_code(game + 0xfd9d40u, got, n) && !memcmp(got, want, n)) g_resolve = (ResolveFn)(game + 0xfd9d40u);
    uintptr_t script = P(game + 0x3326308), units = script ? P(script + 0x18) : 0;
    if (units && P(units + 0x00) == exe + 0x1fc450) g_localPos = (LocalPosFn)(exe + 0x1fc450);
    int xq = XqBind();
    Log("bound: position %s, explosion request %s, spawn_explosion %s", g_resolve && g_localPos ? "yes" : "NO (F6/F7 off)", xq ? "yes" : "NO",
        HAS(spawn_explosion) ? "yes" : "no");
    for (uint32_t i = 0; HAS(mods); i++)
    {
        HfModInfo m = {sizeof m};
        if (!hf->mods(self, i, &m)) break;
        Log("mod %u: %s (%s)%s", i, m.id ? m.id : "?", m.kind == HF_MOD_CODE ? "code" : m.kind == HF_MOD_STATS ? "stats" : m.kind == HF_MOD_LUA ? "lua" : "editor",
            m.loaded ? "" : " NOT LOADED");
    }
    g_ready = 1;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    hf = api;
    self = mod;
    if (!api || api->version < 15 || !api->build_id() || strcmp(api->build_id(), BUILD)) return 1;
    Log("Net Probe " VERSION ": multiplayer research. F5 Steam ping, F6 ask the host over Steam, F7 try from here");
    if (api->on(mod, HF_EVENT_GAME_LOADED, Loaded, NULL)) return 1;
    int ok = api->subscribe(mod, HF_ON_PLAYER_SPAWN, OnPlayer, NULL, 0) && api->subscribe(mod, HF_ON_EXPLOSION, OnExplosion, NULL, 0) &&
             api->subscribe(mod, HF_ON_STRATAGEM, OnStratagem, NULL, 0) && api->subscribe(mod, HF_ON_SPAWN, OnSpawn, NULL, 0) &&
             api->subscribe(mod, HF_ON_STATE, OnState, NULL, 0) && api->on_game_thread(mod, Tick, NULL);
    if (!ok) Log("events or the game thread are unavailable");
    return 0;
}
