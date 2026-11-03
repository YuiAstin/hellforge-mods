/* kitbase damage pipeline: hooks on the game's hit code, and what happens to the hits she deals and takes.
 *
 * Code shared by the character kits. Source of truth: kitbase/ at the repo root. The copy in a kit's src/kitbase/ is
 * written by tools/kitbase_sync.py, so edit kitbase/, never the copy. See docs/kit-base.md.
 * Include after the C headers and the HellForge SDK headers. Everything is static and marked unused, so a kit only
 * pays for what it calls.
 *
 * Three hooks (hit calculation, explosion dispatch, emission) run every hit in the game through one pipeline:
 *
 *   hits she deals (calculation and explosion only):
 *     Observe             records her hits for the kit (kb_player.h)
 *     kb_hit_before       kit: change the hit before the game reads it (armour penetration: set restore)
 *     the game's own code
 *     restore             her armour penetration put back (Unshred)
 *     kb_hit_after        kit: change the result (extra damage, demolition)
 *   hits she takes (all three):
 *     her own blasts      a blast from within 14 m of one she set off in the last 1.2 s does nothing to her
 *     kb_hit_taken        kit: its own handling; nonzero stops here (Voymastina: Predation armour, her Hellbomb)
 *     knockdown guard     while s_steady, or inside the knockdown window (g_shieldUntilMs): no knockdown;
 *                         with s_unshaken no stagger, push or flinch either
 *
 * Kit defaults, defined before including: KB_OWN_SLOTS (her blasts remembered, 8), KB_STEADY (s_steady's default, 0),
 * KB_UNSHAKEN (s_unshaken's default, 0). */
#ifndef KB_HITS_H
#define KB_HITS_H

#include "kb_player.h"

#ifndef KB_OWN_SLOTS
#define KB_OWN_SLOTS 8
#endif
#ifndef KB_STEADY
#define KB_STEADY 0
#endif
#ifndef KB_UNSHAKEN
#define KB_UNSHAKEN 0
#endif

/* Settings: never knocked down (s_steady), and no stagger, push or flinch either while steady (s_unshaken) */
KB_VAR int s_steady = KB_STEADY, s_unshaken = KB_UNSHAKEN;

/* The knockdown window (GetTickCount64 ms; a kit opens it for a skill) */
KB_VAR atomic_ullong g_shieldUntilMs;

/* Her blasts lately, for the hit hooks (another thread): when, where, empowered. The kit records each one it sets off. */
KB_VAR _Atomic unsigned long long g_ownMs[KB_OWN_SLOTS];
KB_VAR float g_ownPos[KB_OWN_SLOTS][3];
KB_VAR atomic_int g_ownEmp[KB_OWN_SLOTS];
KB_VAR unsigned g_ownNext;

#define RESULT_SIZE 0x24u
KB_VAR _Atomic unsigned g_kdStopped;
KB_VAR _Atomic unsigned g_ownStopped;

/* One hit through the hooks. kind: KB_HIT_CALC (a hit calculation) or KB_HIT_BLAST (an explosion's). */
enum { KB_HIT_CALC = 1, KB_HIT_BLAST = 2 };
typedef struct KbHitCall
{
    int kind;
    const void* hit;
    void* out;
    float saved[3];   /* the hit's armour penetration (+0x9C), kept by kb_hit_before when it changes it */
    int restore;      /* nonzero: put saved back after the game's code */
    int flags;        /* the kit's own, from kb_hit_before to kb_hit_after */
} KbHitCall;

/* Kit callbacks, set in hellforge_mod_init (all optional) */
KB_VAR void (*kb_hit_before)(KbHitCall* c);
KB_VAR void (*kb_hit_after)(KbHitCall* c);
KB_VAR int (*kb_hit_taken)(const uint8_t* h, void* result, unsigned long long now);

/* Her armour penetration put back after the game's calculation */
KB_FN void Unshred(const void* hit, const float saved[3]) { memcpy((uint8_t*)hit + 0x9C, saved, 12); }

/* A hit from one of her own blasts: an explosion (kind 4) from within 14 m of one she set off in the last 1.2 s.
 * emp: set when one of those blasts was empowered. */
KB_FN int OwnBlast(const uint8_t* h, int* emp)
{
    if (Word(h) != 4) return 0;
    float origin[3];
    memcpy(origin, h + 0x40, sizeof origin);
    unsigned long long now = GetTickCount64();
    int any = 0;
    for (int i = 0; i < KB_OWN_SLOTS; i++)
    {
        unsigned long long at = atomic_load(&g_ownMs[i]);
        if (!at || now - at > 1200) continue;
        float dx = origin[0] - g_ownPos[i][0], dy = origin[1] - g_ownPos[i][1], dz = origin[2] - g_ownPos[i][2];
        if (dx * dx + dy * dy + dz * dz <= 14.0f * 14.0f)
        {
            if (!emp) return 1;
            any = 1;
            if (atomic_load(&g_ownEmp[i])) *emp = 1;
        }
    }
    return any;
}

/* A hit on her: see the pipeline above */
KB_FN void Steady(const void* hit, void* result)
{
    if (!atomic_load(&g_hooked) || !hit || !result) return;
    uint8_t h[HIT_SIZE], r[RESULT_SIZE];
    uint32_t me = atomic_load(&g_avatar);
    if (!me || !ReadAt((uintptr_t)hit, h, sizeof h) || Word(h + 0x20) != me) return;
    unsigned long long now = GetTickCount64();
    if (OwnBlast(h, NULL))
    {
        memset(result, 0, RESULT_SIZE);
        atomic_fetch_add(&g_ownStopped, 1);
        return;
    }
    if (kb_hit_taken && kb_hit_taken(h, result, now)) return;
    if (!s_steady && now > atomic_load(&g_shieldUntilMs)) return;
    if (!ReadAt((uintptr_t)result, r, sizeof r)) return;
    if (r[0x1E]) { ((uint8_t*)result)[0x1E] = 0; atomic_fetch_add(&g_kdStopped, 1); }
    if (s_unshaken)
    {
        static const uint8_t zero[8] = {0};
        memcpy((uint8_t*)result + 0x0C, zero, 4);   /* stagger */
        memcpy((uint8_t*)result + 0x10, zero, 4);   /* push */
        ((uint8_t*)result)[0x1F] = 0;               /* hit reaction (the flinch) */
    }
}

typedef uintptr_t (*CalculateFn)(uintptr_t, void*, const void*);
typedef void (*BlastFn)(uintptr_t, void*, const void*);
typedef uintptr_t (*EmitFn)(uintptr_t, void*, const void*, uint32_t);
KB_VAR CalculateFn original_calculate;
KB_VAR BlastFn original_blast;
KB_VAR EmitFn original_emit;

KB_FN uintptr_t Calculate(uintptr_t a, void* out, const void* hit)
{
    Observe(hit);
    KbHitCall c = {KB_HIT_CALC, hit, out, {0, 0, 0}, 0, 0};
    if (kb_hit_before) kb_hit_before(&c);
    uintptr_t r = original_calculate(a, out, hit);
    if (c.restore) Unshred(hit, c.saved);
    if (kb_hit_after) kb_hit_after(&c);
    Steady(hit, out);
    return r;
}
KB_FN void Blast(uintptr_t a, void* out, const void* hit)
{
    Observe(hit);
    KbHitCall c = {KB_HIT_BLAST, hit, out, {0, 0, 0}, 0, 0};
    if (kb_hit_before) kb_hit_before(&c);
    original_blast(a, out, hit);
    if (c.restore) Unshred(hit, c.saved);
    if (kb_hit_after) kb_hit_after(&c);
    Steady(hit, out);
}
KB_FN uintptr_t Emit(uintptr_t a, void* result, const void* hit, uint32_t mode) { Steady(hit, result); return original_emit(a, result, hit, mode); }

/* The hooked code, as reviewed: a game update that changes it leaves the hooks off */
KB_VAR const struct { uint32_t rva; const char* hex; } kPins[] = {
    {0x129DC30u, "48 89 5c 24 08 57 48 83 ec 30 33 c0 0f 57 c0 0f 11 02 49 8b f8 48 8b da 0f 11 42 10 89 42 20 41"},
    {0x129D180u, "48 89 4c 24 08 53 55 56 57 41 55 48 83 ec 60 41 8b 40 20 33 f6 3b 05 85 6a 1e 02 49 8b f8 44 0f"},
    {0x129DD30u, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 ec a0 00 00 00 44"},
};

/* Places the three hooks once game is known; what: the kit's name for them in the log ("knockdown guard").
 * 1: hooked (g_hooked set); 0: off, and the log says why. */
KB_FN int HitsInstall(const char* what)
{
    for (unsigned i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (!hf->code_matches(game + kPins[i].rva, kPins[i].hex)) { Log("%s off: game.dll+0x%X is not the reviewed code", what, kPins[i].rva); return 0; }
    if (hf->hook(self, game + 0x129DC30, (void*)Calculate, (void**)&original_calculate) ||
        hf->hook(self, game + 0x129D180, (void*)Blast, (void**)&original_blast) ||
        hf->hook(self, game + 0x129DD30, (void*)Emit, (void**)&original_emit))
    { Log("%s off: a hook could not be placed", what); return 0; }
    atomic_store(&g_hooked, 1);
    Log("%s ready (hit calculation, explosion dispatch and emission hooked)", what);
    return 1;
}

#endif
