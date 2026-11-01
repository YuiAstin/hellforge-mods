"""Writes a mod's default settings file (mods/<mod>/<mod>.ini) from the text its source would write.

    python tools/inigen.py leva faelynn lenna

HellForge keeps mod folders read-only while the game runs, so the mods never write their .ini themselves: the release
ships this one. It takes the default-settings fprintf from mods/<mod>/src/<mod>.c, with the s_* settings it prints and
the Key struct, compiles that alone with a C compiler on this machine (CC, else gcc / clang / zig cc) and runs it.
"""
import os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def compiler():
    if os.environ.get('CC'):
        return os.environ['CC'].split()
    for c in ('gcc', 'clang', 'cc'):
        if shutil.which(c):
            return [c]
    if shutil.which('zig'):
        return ['zig', 'cc']
    sys.exit('no C compiler found (set CC, or install gcc / clang / zig)')

def gen(mod):
    src = os.path.join(ROOT, 'mods', mod, 'src', mod + '.c')
    s = open(src, encoding='utf-8').read()
    ver = re.search(r'#define VERSION "([^"]+)"', s).group(1)
    key = re.search(r'typedef struct Key \{.*?\} Key;', s, re.S).group(0)
    decls = re.findall(r'^static (?:int|float|double|unsigned|uint32_t|Key) s_\w[^;]*;', s, re.M)
    i = s.index('fprintf(f, "; ')
    stmt = s[i:s.index(');\n', i) + 2]
    prog = ('#include <stdio.h>\n#include <stdint.h>\n#define VERSION "%s"\n%s\n%s\nint main(void){FILE* f=stdout;\n%s\nreturn 0;}\n'
            % (ver, key, '\n'.join(decls), stmt))
    with tempfile.TemporaryDirectory() as tmp:
        c, exe = os.path.join(tmp, 'g.c'), os.path.join(tmp, 'g' + ('.exe' if os.name == 'nt' else ''))
        open(c, 'w').write(prog)
        subprocess.check_call(compiler() + ['-w', '-o', exe, c])
        text = subprocess.check_output([exe]).decode().replace('\r\n', '\n')
    out = os.path.join(ROOT, 'mods', mod, mod + '.ini')
    with open(out, 'w', newline='\r\n') as f:
        f.write(text)
    print(f'{out}: {mod} {ver}, {text.count("=")} settings')

if __name__ == '__main__':
    for m in sys.argv[1:] or sys.exit(__doc__):
        gen(m)
