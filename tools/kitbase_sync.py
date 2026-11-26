"""Copies kitbase/*.h into each kit's src/kitbase/ (HellForge builds a mod from its own src/, so each kit carries a copy),
and kitbase/hud/*.lua into the marked regions of each kit's HUD Lua (hud-src/*.lua: "-- kitbase:<name>.lua begin" ...
"-- kitbase:<name>.lua end"; the HUD is then rebuilt with HD2Runtime's SDK as before).

    python tools/kitbase_sync.py KIT...          write those kits' copies (a kit's id: w, lemuen, ...)
    python tools/kitbase_sync.py --all           write every kit's copies
    python tools/kitbase_sync.py --check [KIT...]  exit 1 if a copy is missing or stale (deploy and package run this)

A kit is any mod whose src/*.c includes "kitbase/...". It carries the headers it includes (and the headers those
include), nothing else: a header added to kitbase/ reaches a kit only once the kit includes it. Released kits keep
the copies they shipped with until their next update, so write one kit at a time; --all rewrites every released
kit's sources. A HUD region may bring images (HUD_ASSETS): written into the kit's hud-src/images/ as kb_<name>.
Edit kitbase/ at the repo root, never the copies.
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, 'kitbase')


def kits():
    out = []
    for mod in sorted(os.listdir(os.path.join(ROOT, 'mods'))):
        for c in glob.glob(os.path.join(ROOT, 'mods', mod, 'src', '*.c')):
            with open(c, encoding='utf-8', errors='replace') as f:
                if '#include "kitbase/' in f.read():
                    out.append(mod)
                    break
    return out


def headers():
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(BASE, '*.h')))


INCLUDE = re.compile(r'#include\s+"(?:kitbase/)?(kb_\w+\.h)"')


def wanted(mod):
    """The kitbase headers a kit includes, and the headers those include."""
    out, todo = set(), []
    for c in glob.glob(os.path.join(ROOT, 'mods', mod, 'src', '*.c')):
        with open(c, encoding='utf-8', errors='replace') as f:
            todo += [h for h in re.findall(r'#include\s+"kitbase/(kb_\w+\.h)"', f.read())]
    while todo:
        h = todo.pop()
        if h in out or not os.path.exists(os.path.join(BASE, h)):
            continue
        out.add(h)
        with open(os.path.join(BASE, h), encoding='utf-8') as f:
            todo += INCLUDE.findall(f.read())
    return sorted(out)


def read(p):
    with open(p, 'rb') as f:
        return f.read()


def stale(mod):
    """Header names the kit includes whose copy differs from kitbase/ (or is missing), plus copies it no longer needs."""
    dst = os.path.join(ROOT, 'mods', mod, 'src', 'kitbase')
    want = wanted(mod)
    bad = [h for h in want if not os.path.exists(os.path.join(dst, h)) or read(os.path.join(dst, h)) != read(os.path.join(BASE, h))]
    extra = [os.path.basename(p) for p in glob.glob(os.path.join(dst, '*.h')) if os.path.basename(p) not in want]
    return bad, extra


# images a HUD region needs: kitbase/hud/images/<file> -> the kit's hud-src/images/kb_<file>
HUD_ASSETS = {'hold.lua': ['stun.png']}

REGION = re.compile(r'(-- kitbase:(\w+\.lua) begin[^\n]*\n(?:--[^\n]*\n)?)(.*?)(-- kitbase:\2 end)', re.S)


def hud_files(mods=None):
    out = []
    for mod in mods or sorted(os.listdir(os.path.join(ROOT, 'mods'))):
        for p in sorted(glob.glob(os.path.join(ROOT, 'mods', mod, 'hud-src', '*.lua'))):
            with open(p, encoding='utf-8') as f:
                if REGION.search(f.read()):
                    out.append(p)
    return out


def hud_synced(path):
    """The HUD file's text with every kitbase region filled from kitbase/hud/."""
    with open(path, encoding='utf-8') as f:
        text = f.read()
    def fill(m):
        with open(os.path.join(BASE, 'hud', m.group(2)), encoding='utf-8') as g:
            return m.group(1) + g.read() + m.group(4)
    return text, REGION.sub(fill, text)


def assets(path):
    """(source, destination) of the images the HUD file's regions need."""
    with open(path, encoding='utf-8') as f:
        names = {m.group(2) for m in REGION.finditer(f.read())}
    images = os.path.join(os.path.dirname(path), 'images')
    return [(os.path.join(BASE, 'hud', 'images', a), os.path.join(images, 'kb_' + a)) for n in sorted(names) for a in HUD_ASSETS.get(n, [])]


def check(mods=None):
    ok = True
    for mod in mods or kits():
        bad, extra = stale(mod)
        if bad or extra:
            ok = False
            print(f'kitbase: {mod} is out of sync ({", ".join(bad + extra)}); run tools/kitbase_sync.py')
    for p in hud_files(mods):
        text, want = hud_synced(p)
        if text != want:
            ok = False
            print(f'kitbase: {os.path.relpath(p, ROOT)} is out of sync with kitbase/hud/; run tools/kitbase_sync.py')
        for src, dst in assets(p):
            if not os.path.exists(dst) or read(dst) != read(src):
                ok = False
                print(f'kitbase: {os.path.relpath(dst, ROOT)} is missing or stale; run tools/kitbase_sync.py')
    return ok


def sync(mods):
    for mod in mods:
        dst = os.path.join(ROOT, 'mods', mod, 'src', 'kitbase')
        os.makedirs(dst, exist_ok=True)
        bad, extra = stale(mod)
        for h in bad:
            with open(os.path.join(dst, h), 'wb') as f:
                f.write(read(os.path.join(BASE, h)))
        for h in extra:
            os.remove(os.path.join(dst, h))
        print(f'{mod}: {len(bad)} written, {len(extra)} removed')
    for p in hud_files(mods):
        text, want = hud_synced(p)
        if text != want:
            with open(p, 'w', encoding='utf-8', newline='\n') as f:
                f.write(want)
            print(f'{os.path.relpath(p, ROOT)}: kitbase HUD regions written (rebuild that HUD)')
        for src, dst in assets(p):
            if not os.path.exists(dst) or read(dst) != read(src):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as f:
                    f.write(read(src))
                print(f'{os.path.relpath(dst, ROOT)}: written (rebuild that HUD)')


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    known = kits()
    for a in args:
        if a not in known:
            sys.exit(f'{a} is not a kit (kits: {", ".join(known)})')
    if '--check' in sys.argv[1:]:
        sys.exit(0 if check(args or None) else 1)
    if '--all' in sys.argv[1:]:
        sync(known)
    elif args:
        sync(args)
    else:
        sys.exit(__doc__)
