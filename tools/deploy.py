"""Copies mods from this repo into HellForge's mods folder (the game must be closed).

    python tools/deploy.py                 every mod
    python tools/deploy.py leva lenna      just these
    python tools/deploy.py --hellforge "D:/Games/HellForge" leva

A settings file (<mod>.ini) already in HellForge is never overwritten: it holds your own keys and values.
HellForge keeps mod folders read-only while a session runs and switches off a mod whose folder gained files during
one, so deploy refuses while the game is running.
"""
import argparse, os, shutil, subprocess, sys

import kitbase_sync

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {'__pycache__', 'build', '.vs', '.idea'}

def game_running():
    if os.name != 'nt':
        return False
    try:
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq helldivers2.exe'], capture_output=True, text=True).stdout
        return 'helldivers2.exe' in out.lower()
    except OSError:
        return False

def deploy(mod, target):
    src = os.path.join(ROOT, 'mods', mod)
    dst = os.path.join(target, mod)
    copied = kept = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith('.')]
        rel = os.path.relpath(root, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            to = os.path.join(dst, rel, f)
            if rel == '.' and f.endswith('.ini') and os.path.exists(to):
                kept += 1
                continue
            shutil.copy2(os.path.join(root, f), to)
            copied += 1
    print(f'{mod}: {copied} files copied' + (f', your {kept} .ini kept' if kept else ''))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mods', nargs='*')
    ap.add_argument('--hellforge', default=os.path.join(ROOT, '..', 'HellForge'))
    a = ap.parse_args()
    target = os.path.join(os.path.abspath(a.hellforge), 'mods')
    if not os.path.isdir(target):
        sys.exit(f'no HellForge mods folder at {target} (use --hellforge)')
    if game_running():
        sys.exit('the game is running: close it first (HellForge would switch the mods off)')
    mods = a.mods or sorted(os.listdir(os.path.join(ROOT, 'mods')))
    if not kitbase_sync.check([m for m in mods if m in kitbase_sync.kits()]):
        sys.exit('a kit carries a stale kitbase copy: run tools/kitbase_sync.py first')
    for m in mods:
        deploy(m, target)

if __name__ == '__main__':
    main()
