"""Writes a character kit's part of the options screen (kitbase/kb_options.h) into mods/<kit>/src/:

    kit_options.h   kOpRows: the screen's rows, from the OPTIONS table of the kit's HUD (hud-src/<kit>_hud.lua), so the
                    screen and the Mod Options Menu show the same settings, labels and tips
    kit_icon.h      kOpIcon: the mod's icon.png as 96 x 96 RGBA8, and KB_OP_ACCENT, the kit's colour

    python tools/optgen.py lenna leva        these kits
    python tools/optgen.py                   every kit that includes kitbase/kb_options.h
    python tools/optgen.py --check           exit 1 if a written file would change

Its options.json (mods/<kit>/options.json), when it has one, adds the Advanced section: every other setting of the kit's
.ini that a player may change, with its group, label and tip ("advanced"), and the settings left off the screen ("hidden":
ids, probes, workarounds; they stay in the .ini). Every setting of the kit's settings table (KB_F / KB_I in its .c) must be
on the screen, advanced or hidden, or the run stops and names the rest. "when" marks settings that take effect only at the
next mission or after a restart. An Advanced row's limits are the setting's own; an on/off one is a switch, a span over
100 a field, the rest a slider (its step chosen from the span, or "step" in its entry).

A row "Group: Label" goes under the heading Group. An on/off row is a switch, a choice a row of buttons. A slider row becomes a field to type in
when its range is wide: a cooldown, a duration (a tip that starts with "Seconds"), or a span over 100 (flat damage, points); the field's
limits are then the setting's own (kSettings). A kit_options.h whose first line says "hand-written" is left alone.
"""
import glob, json, os, re, sys
from PIL import Image
import lupa

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 96
ACCENT = {'alva': 0x96D6FF, 'ascalon': 0xC83C50, 'destroyer': 0xFF5C78, 'executor': 0xE6C878, 'exusiai': 0xFF5A46, 'faelynn': 0x8CD2FF, 'koleda': 0x50C8FF,
          'lemuen': 0xFFB4D2, 'lenna': 0x78C8FF, 'leva': 0xFFA03C, 'vector': 0xFF8A24, 'voymastina': 0xFFAA3C}


def kits():
    out = []
    for mod in sorted(os.listdir(os.path.join(ROOT, 'mods'))):
        for c in glob.glob(os.path.join(ROOT, 'mods', mod, 'src', '*.c')):
            with open(c, encoding='utf-8', errors='replace') as f:
                if '#include "kitbase/kb_options.h"' in f.read():
                    out.append(mod)
                    break
    return out


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def num(v):
    return ('%g' % v) + ('f' if ('.' in '%g' % v or 'e' in '%g' % v) else '')


def rows(kit):
    path = os.path.join(ROOT, 'mods', kit, 'hud-src', kit + '_hud.lua')
    text = open(path, encoding='utf-8').read()
    m = re.search(r'^local OPTIONS = (\{.*?^\})', text, re.S | re.M)
    if not m:
        raise SystemExit('%s: no OPTIONS table in %s' % (kit, path))
    table = lupa.LuaRuntime(unpack_returned_tuples=True).eval(m.group(1))
    out, group = [], 'General'
    for k in sorted(table.keys()):
        r = table[k]
        name, kind, label, lo, hi, step, tip = r[1], r[2], r[3], r[4], r[5], r[6], r[7] or ''
        if ': ' in label:
            group, label = label.split(': ', 1)
        elif not out:
            group = 'General'
        if kind == 'toggle':
            out.append((name, group, label, 'OP_TOGGLE', 0, 1, 1, tip, ''))
            continue
        if kind == 'choice':
            names = [lo[i] for i in sorted(lo.keys())]
            out.append((name, group, label, 'OP_CHOICE', 0, len(names) - 1, 1, tip, '|'.join(names)))
            continue
        wide = 'Cooldown' in label or tip.startswith('Seconds') or hi - lo > 100
        out.append((name, group, label, 'OP_FIELD' if wide else 'OP_SLIDER', lo, hi, step, tip, ''))
    # a group's rows stay together: a row whose group came earlier moves up to it
    order = []
    for r in out:
        if r[1] not in order:
            order.append(r[1])
    spec = options_spec(kit)
    # options.json's "rename": a row's group and label on the screen (the Mod Options Menu keeps the HUD's)
    for name, (g, l) in (spec or {}).get('rename', {}).items():
        out = [(r[0], g, l) + r[3:] if r[0] == name else r for r in out]
        if g not in order:
            order.append(g)
    out = [r + (0,) for g in order for r in out if r[1] == g]
    if not spec:
        return [r + (0,) for r in out]
    when = {'mission': 'OP_NEXT_MISSION', 'restart': 'OP_RESTART'}
    marks = spec.get('when', {})
    out = [r + (when[marks[r[0]]] if r[0] in marks else 0,) for r in out]
    table = settings_table(kit)
    shown = {r[0] for r in out}
    adv = []
    for e in spec.get('advanced', []):
        name, group, label, tip = e[0], e[1], e[2], e[3]
        extra = e[4] if len(e) > 4 else {}
        if name not in table:
            raise SystemExit('%s: options.json names %s, which is not in its settings table' % (kit, name))
        if name in shown:
            raise SystemExit('%s: %s is on the screen already (its HUD\'s OPTIONS)' % (kit, name))
        typ, lo, hi = table[name]
        lo, hi = extra.get('lo', lo), extra.get('hi', hi)
        if typ == 'I' and lo == 0 and hi == 1:
            row = (name, group, label, 'OP_TOGGLE', 0, 1, 1, tip, '')
        elif hi - lo > 100 and 'step' not in extra:
            row = (name, group, label, 'OP_FIELD', lo, hi, 1, tip, '')
        else:
            step = extra.get('step') or (1 if typ == 'I' else next(x for x in (0.01, 0.05, 0.1, 0.5, 1, 5, 10) if x >= (hi - lo) / 200.0))
            row = (name, group, label, 'OP_SLIDER', lo, hi, step, tip, '')
        adv.append(row + (1, when[marks[name]] if name in marks else 0))
        shown.add(name)
    hidden = set(spec.get('hidden', []))
    missing = [n for n in table if n not in shown and n not in hidden]
    if missing:
        raise SystemExit('%s: settings neither on the screen nor hidden in options.json: %s' % (kit, ' '.join(missing)))
    order = []
    for r in adv:
        if r[1] not in order:
            order.append(r[1])
    return out + [r for g in order for r in adv if r[1] == g]


def options_spec(kit):
    path = os.path.join(ROOT, 'mods', kit, 'options.json')
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else None


def settings_table(kit):
    """name -> ('F' or 'I', lo, hi) from KB_F / KB_I in the kit's .c files"""
    out = {}
    for c in glob.glob(os.path.join(ROOT, 'mods', kit, 'src', '*.c')):
        for typ, name, lo, hi in re.findall(r'KB_([FI])\("([a-z0-9_]+)",\s*[^,]+,\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?\)', open(c, encoding='utf-8', errors='replace').read()):
            out[name] = (typ, float(lo), float(hi))
    return out


# which of the HUD's skill names (NAMES, 1-based) each of the kit's three keys (s_keys[0..2]) fires
KEY_SKILLS = {'ascalon': (3, 1, 2), 'destroyer': (1, 2, 3, 4)}
SMALL = {'of', 'the', 'ex', 'a', 'an', 'and', 'to', 'for', 'in', 'on'}


def nice(name):
    words = re.split(r'([ -])', name.lower())
    out = []
    for i, w in enumerate(words):
        out.append(w if (w in SMALL and i) or w in (' ', '-') else w[:1].upper() + w[1:])
    return ''.join(out)


def key_labels(kit):
    spec = options_spec(kit) or {}
    if 'keys' in spec:
        return spec['keys']
    text = open(os.path.join(ROOT, 'mods', kit, 'hud-src', kit + '_hud.lua'), encoding='utf-8').read()
    m = re.search(r'^local NAMES = (\{[^\n]*\})', text, re.M)
    if not m:
        return [None, None, None]
    t = lupa.LuaRuntime(unpack_returned_tuples=True).eval(m.group(1))
    names = [t[i] for i in sorted(t.keys())]
    return [nice(names[n - 1]) for n in KEY_SKILLS.get(kit, (1, 2, 3))]


def rows_text(kit):
    lines = ['/* %s: the options screen\'s rows. Written by tools/optgen.py from hud-src/%s_hud.lua (OPTIONS) and options.json: edit those, then run it. */' % (kit, kit),
             'static const OpRow kOpRows[] = {']
    for name, group, label, kind, lo, hi, step, tip, choices, adv, when in rows(kit):
        tail = ''
        if choices or adv or when:
            tail = ', %s' % (cstr(choices) if choices else 'NULL')
        if adv or when:
            tail += ', %d, %s' % (adv, when or 'OP_LIVE')
        lines.append('    {%s, %s, %s, %s, %s, %s, %s,\n     %s%s},' % (cstr(name), cstr(group), cstr(label), kind, num(lo), num(hi), num(step), cstr(tip), tail))
    lines.append('};')
    labels = key_labels(kit)
    lines.append('static const char* const kOpKeyLabels[%d] = {%s};' % (len(labels), ', '.join(cstr(n) if n else 'NULL' for n in labels)))
    return '\n'.join(lines) + '\n'


def icon_text(kit):
    im = Image.open(os.path.join(ROOT, 'mods', kit, 'icon.png')).convert('RGBA').resize((SIZE, SIZE), Image.LANCZOS)
    data = im.tobytes()
    lines = ['/* %s: its mod icon for the options screen (%d x %d RGBA8) and its colour. Written by tools/optgen.py from icon.png. */' % (kit, SIZE, SIZE),
             '#define KB_OP_ACCENT 0x%06X' % ACCENT.get(kit, 0xFFE800),
             'static const unsigned char kOpIcon[%d * %d * 4] = {' % (SIZE, SIZE)]
    for i in range(0, len(data), 32):
        lines.append('    ' + ','.join(str(b) for b in data[i:i + 32]) + ',')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    check = '--check' in sys.argv
    bad = 0
    for kit in args or kits():
        src = os.path.join(ROOT, 'mods', kit, 'src')
        for name, make in (('kit_options.h', rows_text), ('kit_icon.h', icon_text)):
            path = os.path.join(src, name)
            old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
            if name == 'kit_options.h' and old and 'hand-written' in old.split('\n', 1)[0]:
                continue
            new = make(kit)
            if new == old:
                continue
            if check:
                print('%s: %s is stale' % (kit, name))
                bad = 1
            else:
                open(path, 'w', encoding='utf-8', newline='\n').write(new)
                print('%s: wrote %s' % (kit, name))
    sys.exit(bad)


if __name__ == '__main__':
    main()
