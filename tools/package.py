"""Builds release zips from this repo: <out>/<mod>-<version>.zip, laid out as <mod>/... for HellForge's ADD MODS.

    python tools/package.py leva faelynn lenna
    python tools/package.py --out ../Releases leva

The zip holds the mod folder as it is here (with its default .ini), minus notes kept only in the repo.
"""
import argparse, json, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ONLY = {'FUTURE.md'}
SKIP_EXT = ('.dll', '.lib', '.pdb', '.bak')

def package(mod, out):
    src = os.path.join(ROOT, 'mods', mod)
    with open(os.path.join(src, 'hellforge-mod.json'), encoding='utf-8') as f:
        version = json.load(f)['version']
    path = os.path.join(out, f'{mod}-{version}.zip')
    files = []
    for root, dirs, names in os.walk(src):
        dirs[:] = sorted(d for d in dirs if not d.startswith('.') and d not in ('build', '__pycache__'))
        for n in sorted(names):
            if n in REPO_ONLY or n.endswith(SKIP_EXT):
                continue
            full = os.path.join(root, n)
            files.append((full, os.path.join(mod, os.path.relpath(full, src)).replace(os.sep, '/')))
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for full, arc in files:
            z.write(full, arc)
    print(f'{path}: {len(files)} files')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mods', nargs='+')
    ap.add_argument('--out', default=os.path.join(ROOT, '..', 'Releases'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for m in a.mods:
        package(m, os.path.abspath(a.out))

if __name__ == '__main__':
    main()
