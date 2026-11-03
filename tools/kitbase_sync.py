"""Copies kitbase/*.h into each kit's src/kitbase/ (HellForge builds a mod from its own src/, so each kit carries a copy),
and kitbase/hud/*.lua into the marked regions of each kit's HUD Lua (hud-src/*.lua: "-- kitbase:<name>.lua begin" ...
"-- kitbase:<name>.lua end"; the HUD is then rebuilt with HD2Runtime's SDK as before).

    python tools/kitbase_sync.py            write the copies
    python tools/kitbase_sync.py --check    exit 1 if any copy is missing or stale (deploy and package run this)

A kit is any mod whose src/*.c includes "kitbase/...". Edit kitbase/ at the repo root, never the copies.
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


def read(p):
    with open(p, 'rb') as f:
        return f.read()


def stale(mod):
    """Header names whose copy in mod differs from kitbase/ (or is missing), plus copies kitbase/ no longer has."""
    dst = os.path.join(ROOT, 'mods', mod, 'src', 'kitbase')
    bad = [h for h in headers() if not os.path.exists(os.path.join(dst, h)) or read(os.path.join(dst, h)) != read(os.path.join(BASE, h))]
    extra = [os.path.basename(p) for p in glob.glob(os.path.join(dst, '*.h')) if os.path.basename(p) not in headers()]
    return bad, extra


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
    return ok


def sync():
    for mod in kits():
        dst = os.path.join(ROOT, 'mods', mod, 'src', 'kitbase')
        os.makedirs(dst, exist_ok=True)
        bad, extra = stale(mod)
        for h in bad:
            with open(os.path.join(dst, h), 'wb') as f:
                f.write(read(os.path.join(BASE, h)))
        for h in extra:
            os.remove(os.path.join(dst, h))
        print(f'{mod}: {len(bad)} written, {len(extra)} removed')
    for p in hud_files():
        text, want = hud_synced(p)
        if text != want:
            with open(p, 'w', encoding='utf-8', newline='\n') as f:
                f.write(want)
            print(f'{os.path.relpath(p, ROOT)}: kitbase HUD regions written (rebuild that HUD)')


if __name__ == '__main__':
    if '--check' in sys.argv[1:]:
        sys.exit(0 if check() else 1)
    sync()
