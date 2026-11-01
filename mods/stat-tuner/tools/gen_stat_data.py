"""Generates src/stat_data.h for Stat Tuner from HellForge's editor catalogue (mods/hellforge-editor/editor-catalog.json).

Every item of the editor's categories becomes a menu item; each menu slider ("slot") is a set of catalogue fields picked
by regular expressions over the catalogue's field keys, so one slider can drive several fields (both spreads, all four
recoil inputs, every fire mode's damage)."""
import json, re, sys, math

CATALOG, OUT = sys.argv[1], sys.argv[2]
d = json.load(open(CATALOG, encoding="utf-8"))

PCT, DELTA, USES = 0, 1, 2
NUMERIC = {"u8": 1, "u16": 2, "u32": 3, "i32": 4, "f32": 5}

AP = r"^(explosion\.([a-z_]+\.)?)?damage\.([a-z_]+\.)?ap_(direct|slight|large|extreme)$"
S = {  # key: (label, mode, regex, description)
    "cooldown": ("Cooldown", PCT, r"^stratagem\.cooldown$", "Seconds before it can be called again. Lower is faster. Applies from the next mission."),
    "callin": ("Call-in time", PCT, r"^stratagem\.call_in_time$", "Seconds from the throw to the arrival. Applies from the next mission."),
    "uses": ("Uses per mission", USES, r"^stratagem\.max_uses$", "Uses per mission. 1000% means unlimited. Applies from the next mission."),
    "fire_rate": ("Fire rate", PCT, r"^(weapon|beam)\.fire_rate$", "Rounds per minute. Applies when the weapon is next equipped or called in."),
    "mag": ("Magazine size", PCT, r"^(weapon|magazine)\.capacity$", "Rounds per magazine. Applies when the weapon is next equipped."),
    "spare": ("Spare magazines", PCT, r"^(magazine\.(spare|starting)_magazines|heatsink\.(spare|starting))$", "Magazines carried (and the starting count)."),
    "reload": ("Reload time", PCT, r"^reload\.duration$", "Lower is a faster reload."),
    "dmg": ("Damage", PCT, r"^damage\.([a-z_]+\.)?standard_damage$", "Direct-hit damage (every fire mode). Damage rows are often shared: weapons that fire the same round change too."),
    "dur": ("Durable damage", PCT, r"^damage\.([a-z_]+\.)?durable_damage$", "Damage against durable parts. Shared rows change every weapon that uses them."),
    "ap": ("Armor penetration +/-", DELTA, AP, "Steps added to (or taken from) the game's armor penetration, -6 to +6. The result stays within 0 to 10."),
    "xdmg": ("Explosion damage", PCT, r"^explosion\.([a-z_]+\.)?damage\.(standard|durable)_damage$", "Blast damage (standard and durable)."),
    "xrad": ("Explosion radius", PCT, r"^explosion\.([a-z_]+\.)?(inner|outer|shockwave)_radius$", "Inner, outer and shockwave radius together. Large blasts can kill you and teammates."),
    "demo": ("Demolition", PCT, r"^(explosion\.([a-z_]+\.)?)?damage\.([a-z_]+\.)?demolition$", "What it can destroy (doors, fabricators, bug holes)."),
    "stagger": ("Stagger and push", PCT, r"^(explosion\.([a-z_]+\.)?)?damage\.([a-z_]+\.)?(stagger|push_force)$", "How much hits stagger and push enemies."),
    "vel": ("Projectile speed", PCT, r"^projectile\.([a-z_]+\.)?velocity$", "Muzzle velocity."),
    "pel": ("Pellets per shot", PCT, r"^projectile\.([a-z_]+\.)?pellet_count$", "Projectiles per shot (rounded)."),
    "spread": ("Spread", PCT, r"^weapon\.(horizontal|vertical)_spread$", "Horizontal and vertical spread together. Lower is tighter."),
    "recoil": ("Recoil", PCT, r"^weapon\.recoil_", "All four recoil inputs together. 0% is no recoil."),
    "ergo": ("Ergonomics", PCT, r"^weapon\.ergonomics$", "Handling: how fast it turns and aims."),
    "heat": ("Heat capacity", PCT, r"^heat\.capacity$", "Heat it holds before it overheats."),
    "cool": ("Cooling rate", PCT, r"^heat\.cool_per_second$", "Heat lost per second."),
    "tcount": ("Grenades carried", PCT, r"^throwable\.(max_count|starting_count)$", "How many you carry and start with (rounded)."),
    "tsupply": ("Grenades from supply", PCT, r"^throwable\.count_from_supply$", "How many a resupply gives."),
    "fuse": ("Fuse time", PCT, r"^throwable\.explosion_delay$", "Seconds before it explodes."),
    "arming": ("Arming delay", PCT, r"^throwable\.arming_delay$", "Seconds before it arms."),
    "throw": ("Throw distance", PCT, r"^throwable\.launch_distance_(min|max)$", "How far it is thrown."),
    "sdur": ("Effect duration", PCT, r"^status\.duration$", "How long its status effect lasts."),
    "shrap": ("Shrapnel count", PCT, r"^explosion\.([a-z_]+\.)?shrapnel_count$", "Fragments released (rounded)."),
    "eagleuses": ("Eagle uses per rearm", PCT, r"^eagle\.uses_per_rearm$", "Uses before the Eagle has to rearm."),
    "eaglerearm": ("Eagle rearm time", PCT, r"^eagle\.rearm_time$", "Seconds to rearm. Lower is faster."),
    "salvos": ("Salvos", PCT, r"^orbital\.salvos$", "Number of salvos (rounded)."),
    "shells": ("Shells per salvo", PCT, r"^orbital\.shells_per_salvo$", "Shells in each salvo (rounded)."),
    "scatter": ("Scatter", PCT, r"^orbital\.(salvo_)?scatter$", "How far shells land from the target. Lower is more accurate."),
    "health": ("Health", PCT, r"^(entity|zone(_\d+)?)\.health$", "Health of the entity and its parts. Applies when it is next called in."),
    "armor": ("Armor +/-", DELTA, r"^(entity|zone(_\d+)?)\.armor$", "Steps added to (or taken from) the game's armor value, -6 to +6. The result stays within 0 to 10."),
    "xtaken": ("Explosive damage taken", PCT, r"^(entity|zone(_\d+)?)\.explosive_damage_percentage$", "Share of explosive damage it takes. Lower is tougher."),
    "range": ("Targeting range", PCT, r"^targeting\.range$", "How far it finds targets."),
    "lifetime": ("Lifetime", PCT, r"^payload\.lifetime$", "Seconds before it leaves or self-destructs."),
    "windup": ("Wind-up time", PCT, r"^windup\.wind_(up|down)_seconds$", "Seconds to spin up (and down). Lower is faster."),
    "mspeed": ("Missile speed", PCT, r"^missile\.(starting_speed|minimum_speed|preferred_speed|acceleration)$", "Missile launch speed, cruise speed and acceleration."),
    "mturn": ("Missile tracking", PCT, r"^missile\.(turn_rate_at_max_angle|turn_rate_aligned|max_angle_to_target)$", "How hard missiles turn toward the target."),
    "rfuse": ("Rocket arming and fuse", PCT, r"^fired\.(arming_delay|explosion_delay)$", "Seconds before a fired rocket arms and explodes."),
    "steer": ("Steering response", PCT, r"^vehicle\.steering_response_speed$", "How quickly the vehicle steers."),
    "turn": ("Turn speed", PCT, r"^turret\.(yaw|pitch)_speed$", "How fast the turret turns."),
    "capacity": ("Backpack capacity", PCT, r"^deposit\.(capacity|start_amount)$", "What the backpack holds (and starts with)."),
    "refill": ("Backpack refill", PCT, r"^deposit\.refill_amount$", "What a resupply adds to it."),
    "shield": ("Shield durability", PCT, r"^shield\.durability$", "Damage the shield takes before it breaks."),
    "shieldrate": ("Shield recharge rate", PCT, r"^shield\.recharge_rate$", "How fast the shield recovers."),
    "shielddelay": ("Shield recharge delay", PCT, r"^shield\.(broken_)?recharge_delay$", "Seconds before the shield starts to recover. Lower is sooner."),
    "jump": ("Jump strength", PCT, r"^jump\.(vertical_launch_velocity|sustain_thrust)$", "Jump pack launch and thrust."),
    "recharge": ("Recharge time", PCT, r"^recharge\.time$", "Seconds between uses. Lower is sooner."),
}
WEAPON = ["fire_rate", "mag", "spare", "reload", "dmg", "dur", "ap", "xdmg", "xrad", "demo", "stagger", "vel", "pel", "spread", "recoil", "ergo", "heat", "cool"]
STRAT = ["cooldown", "callin", "uses"]
TEMPLATES = {
    "weapon": WEAPON,
    "support": STRAT + WEAPON,
    "throwable": ["tcount", "tsupply", "dmg", "xdmg", "xrad", "ap", "demo", "stagger", "fuse", "arming", "throw", "sdur", "shrap", "vel"],
    "offensive": STRAT + ["dmg", "dur", "xdmg", "xrad", "ap", "demo", "stagger", "vel", "pel", "eagleuses", "salvos", "shells", "scatter", "sdur"],
    "defensive": STRAT + ["health", "armor", "xtaken", "mag", "spare", "fire_rate", "reload", "dmg", "xdmg", "xrad", "ap", "range", "lifetime", "turn", "windup", "mspeed", "mturn", "rfuse"],
    "backpack": STRAT + ["capacity", "refill", "shield", "shieldrate", "shielddelay", "jump", "recharge", "health", "armor"],
    # a vehicle and each of its mounted weapons (its own item): 27 sliders, the item and reset rows make 29 of 31
    "vehicle": STRAT + ["health", "armor", "xtaken", "steer", "fire_rate", "mag", "spare", "reload", "dmg", "dur", "ap", "xdmg", "xrad",
                        "demo", "stagger", "vel", "pel", "spread", "recoil", "turn", "windup", "mspeed", "mturn", "rfuse"],
}
HELLDIVER = [
    ("walk", "Walk speed", PCT, r"^helldiver\.speed\.walk$", "Walking speed."),
    ("jog", "Jog speed", PCT, r"^helldiver\.speed\.jog$", "Jogging speed."),
    ("sprint", "Sprint speed", PCT, r"^helldiver\.speed\.sprint(_exhausted)?$", "Sprint speed (and when out of stamina)."),
    ("aim", "Speed while aiming", PCT, r"^helldiver\.speed\.(aim|crouch_aim)$", "Movement speed while aiming."),
    ("crouch", "Crouch speed", PCT, r"^helldiver\.speed\.crouch_(walk|jog|sprint)$", "Movement speed while crouched."),
    ("prone", "Prone speed", PCT, r"^helldiver\.speed\.prone$", "Crawling speed."),
    ("swim", "Swim speed", PCT, r"^helldiver\.speed\.swim$", "Swimming speed."),
    ("stamina", "Sprint duration", PCT, r"^helldiver\.stamina\.sprint_duration$", "Seconds you can sprint."),
    ("recover", "Stamina recovery time", PCT, r"^helldiver\.stamina\.recover_time_", "Seconds to recover stamina. Lower is faster."),
    ("recdelay", "Stamina recovery delay", PCT, r"^helldiver\.stamina\.recover_delay$", "Pause before stamina recovers. Lower is sooner."),
    ("costs", "Stamina costs", PCT, r"^helldiver\.stamina\.cost_", "Stamina used by jumps, dives, climbs and slides."),
    ("stims", "Stims from supply", PCT, r"^helldiver\.supply\.stims_from_supply$", "Stims a resupply gives (rounded)."),
    ("limbs", "Limb health", PCT, r"^[a-z_]+:zone\.health$", "Health of the head, body, arms and legs."),
    ("xtaken", "Explosive damage taken", PCT, r"^entity\.explosive_damage_percentage$", "Share of explosive damage you take. Lower is tougher."),
]
# Category, page title prefix, section, template
PLAN = [
    ("primary", "Primaries", "Weapons", "weapon"),
    ("secondary", "Secondaries", "Weapons", "weapon"),
    ("throwables", "Throwables", "Weapons", "throwable"),
    ("support", "Support weapons", "Stratagems", "support"),
    ("offensive", "Offensive", "Stratagems", "offensive"),
    ("defensive", "Defensive", "Stratagems", "defensive"),
    ("backpacks", "Backpacks", "Stratagems", "backpack"),
    ("vehicles", "Vehicles", "Stratagems", "vehicle"),
]


def is_part(title):
    return title.startswith("Weapon \u00b7 ") or title.startswith("Shield \u00b7 ")


def parts(o):
    return [p["title"] for p in o["page"] if is_part(p["title"])]


def rows(o, part=None):
    """The object's rows: its own pages (part None), or one mounted weapon's page (a vehicle's "Weapon · Main cannon")."""
    for p in o["page"]:
        if (part is None and is_part(p["title"])) or (part is not None and p["title"] != part):
            continue
        for s in p.get("subs", []) or []:
            for r in s.get("rows", []) or []:
                yield r
        for r in p.get("rows", []) or []:
            yield r


def ours(o, r):
    t = r.get("target") or ""
    return t == o["name"] or t.startswith(o["name"] + " / ")


def usable(r):
    return (r.get("component") and r.get("type") in NUMERIC and r.get("ref") not in (None, "") and not r.get("pick")
            and isinstance(r.get("offset"), int))


comps, fields, slots, templates, items, ranges, pages = [], [], [], [], [], [], []


def comp_index(c):
    if c not in comps:
        comps.append(c)
    return comps.index(c)


def ref_value(ref):
    return int(ref, 16) if str(ref).lower().startswith("0x") else int(ref)


def add_template(slotdefs):
    first = len(slots)
    for key, label, mode, rx, desc in slotdefs:
        slots.append((key, label, mode, desc, re.compile(rx)))
    templates.append((first, len(slotdefs)))
    return len(templates) - 1


def add_item(name, objs, tpl, part=None):
    """objs: catalogue objects whose rows feed this item (one, or several boosters for the boosters page)."""
    first, count = templates[tpl]
    rstart = len(ranges)
    used = 0
    seen = set()  # a field belongs to the item's first slot that names it
    for si in range(first, first + count):
        rx = slots[si][4]
        fstart = len(fields)
        for o in objs:
            for r in rows(o, part):
                if not ours(o, r) or not usable(r) or not rx.search(r["key"]):
                    continue
                k = (r["component"], r["ref"], r["offset"])
                if k in seen:
                    continue
                seen.add(k)
                mn = r.get("min"); mx = r.get("max")
                fields.append((ref_value(r["ref"]), comp_index(r["component"]), NUMERIC[r["type"]], 1 if r.get("shared") else 0,
                               r["offset"], mn, mx))
        n = len(fields) - fstart
        used += n > 0
        ranges.append((fstart, n))
    items.append((name[:48], rstart))
    return used


tpl_ids = {k: add_template([(s,) + S[s] for s in v]) for k, v in TEMPLATES.items()}
objs = d["objects"]
for cat, title, section, tpl in PLAN:
    group = sorted((o for o in objs if o["category"] == cat), key=lambda o: o["name"])
    group = [o for o in group if any(usable(r) and ours(o, r) for r in rows(o))]
    group = [(o, part) for o in group for part in [None] + parts(o)]  # each mounted weapon is an item of its own
    n = len(group)
    if not n:
        continue
    npages = math.ceil(n / 16)
    per = math.ceil(n / npages)
    for p in range(npages):
        chunk = group[p * per:(p + 1) * per]
        ifirst = len(items)
        for o, part in chunk:
            if part is None:
                add_item(o["name"], [o], tpl_ids[tpl])
            else:
                add_item(o["name"] + ": " + part.split(" \u00b7 ", 1)[1], [o], tpl_ids[tpl], part)
        label = title if npages == 1 else f"{title} {p + 1}/{npages}"
        pages.append((label, section, tpl_ids[tpl], ifirst, len(chunk), 0))

# The Helldiver: one item, its sliders shown directly.
hd = [o for o in objs if o["category"] == "helldiver" and o["name"] == "Helldiver"]
tpl = add_template(HELLDIVER)
ifirst = len(items)
add_item("Helldiver", hd, tpl)
pages.append(("Helldiver", "Diver and presets", tpl, ifirst, 1, 1))

# Boosters: one slider per booster setting, shown directly.
bdefs = []
boosters = sorted((o for o in objs if o["category"] == "boosters"), key=lambda o: o["name"])
bmap = []
for o in boosters:
    groups = {}
    for r in rows(o):
        if not ours(o, r) or not usable(r):
            continue
        k = r["key"]
        if k.startswith("pod."):
            continue
        g = None
        for gk, lab, rx in (("xdmg", "explosion damage", S["xdmg"][2]), ("xrad", "explosion radius", S["xrad"][2]),
                            ("dmg", "damage", r"^damage\.(standard|durable)_damage$"), ("sstr", "effect strength", r"^status\.strength$"),
                            ("sdur", "effect duration", r"^status\.duration$"), ("fire", "fire rate", r"^weapon\.fire_rate$"),
                            ("mag", "magazine", r"^magazine\.capacity$"), ("uses", "uses", r"^stratagem\.max_uses$")):
            if re.search(rx, k):
                g = (gk, lab, rx)
                break
        if g is None and k.startswith("booster."):
            g = (k, r["label"], "^" + re.escape(k) + "$")
        if g and g[0] not in groups:
            groups[g[0]] = g
    for gk, lab, rx in groups.values():
        short = o["name"]
        label = f"{short}: {lab}"
        if len(label) > 58:  # the menu allows 64 characters, and " %" is added
            label = label[:58].rstrip()
        bdefs.append((f"b{len(bdefs)}", label, PCT, rx, f"{o['name']}: {lab}."))
        bmap.append(o)
bdefs = bdefs[:30]
bmap = bmap[:30]
tpl = add_template(bdefs)
# each booster slot must only read its own booster's rows: build ranges by hand
first, count = templates[tpl]
rstart = len(ranges)
for i in range(count):
    o = bmap[i]
    rx = slots[first + i][4]
    fstart = len(fields)
    seen = set()
    for r in rows(o):
        if not ours(o, r) or not usable(r) or not rx.search(r["key"]):
            continue
        k = (r["component"], r["ref"], r["offset"])
        if k in seen:
            continue
        seen.add(k)
        fields.append((ref_value(r["ref"]), comp_index(r["component"]), NUMERIC[r["type"]], 1 if r.get("shared") else 0,
                       r["offset"], r.get("min"), r.get("max")))
    ranges.append((fstart, len(fields) - fstart))
items.append(("Boosters", rstart))
pages.append(("Boosters", "Diver and presets", tpl, len(items) - 1, 1, 1))


def cstr(s):
    return json.dumps(s, ensure_ascii=False)


def cf(v):
    return "NAN" if v is None else repr(float(v)) + "f"


out = []
out.append("/* Generated by tools/gen_stat_data.py from HellForge's editor catalogue (game build %s). Do not edit. */" % d["build"])
out.append("#pragma once\n#include <stdint.h>\n#include <math.h>")
out.append("#define ST_PCT 0\n#define ST_DELTA 1\n#define ST_USES 2")
out.append("typedef struct StField { uint64_t ref; uint8_t comp, type, shared; uint16_t off; float min, max; } StField;")
out.append("typedef struct StSlot { const char* key; const char* label; uint8_t mode; const char* desc; } StSlot;")
out.append("typedef struct StTemplate { uint16_t first, count; } StTemplate;")
out.append("typedef struct StItem { const char* name; uint16_t ranges; } StItem;")
out.append("typedef struct StRange { uint16_t first, count; } StRange;")
out.append("typedef struct StPage { const char* title; const char* section; uint16_t tpl, item_first, item_count; uint8_t direct; } StPage;")
out.append("static const char* const kComps[] = {" + ", ".join(cstr(c) for c in comps) + "};")
out.append("static const StField kFields[] = {")
for ref, c, t, sh, off, mn, mx in fields:
    out.append(f"  {{0x{ref:016X}ull, {c}, {t}, {sh}, {off}, {cf(mn)}, {cf(mx)}}},")
out.append("};")
out.append("static const StSlot kSlots[] = {")
for key, label, mode, desc, _ in slots:
    out.append(f"  {{{cstr(key)}, {cstr(label)}, {mode}, {cstr(desc)}}},")
out.append("};")
out.append("static const StTemplate kTemplates[] = {" + ", ".join(f"{{{a}, {b}}}" for a, b in templates) + "};")
out.append("static const StRange kRanges[] = {")
for i in range(0, len(ranges), 8):
    out.append("  " + " ".join(f"{{{a}, {b}}}," for a, b in ranges[i:i + 8]))
out.append("};")
out.append("static const StItem kItems[] = {")
for name, r in items:
    out.append(f"  {{{cstr(name)}, {r}}},")
out.append("};")
out.append("static const StPage kPages[] = {")
for title, section, tpl, f, n, direct in pages:
    out.append(f"  {{{cstr(title)}, {cstr(section)}, {tpl}, {f}, {n}, {direct}}},")
out.append("};")
out.append(f"#define ST_FIELDS {len(fields)}\n#define ST_SLOTS {len(slots)}\n#define ST_ITEMS {len(items)}\n#define ST_PAGES {len(pages)}")
out.append(f"#define ST_MAX_TEMPLATE_SLOTS {max(b for a, b in templates)}")
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")

# Coverage report
print(f"{len(items)} items, {len(fields)} fields, {len(pages)} pages, max slots {max(b for a, b in templates)}")
for title, section, tpl, f, n, direct in pages:
    first, count = templates[tpl]
    cov = []
    for si in range(count):
        have = sum(1 for it in range(f, f + n) if ranges[items[it][1] + si][1] > 0)
        cov.append(f"{slots[first + si][0]}:{have}")
    print(f"[{section}] {title} ({n} items): " + " ".join(cov))
