"""Writes a mod's default settings file (mods/<mod>/<mod>.ini) from the text its source would write.

    python tools/inigen.py leva faelynn lenna

HellForge keeps mod folders read-only while the game runs, so the mods never write their .ini themselves: the release
ships this one. It takes the default-settings fprintf from mods/<mod>/src/<mod>.c, with the s_* settings it prints and
the Key struct, compiles that alone with a C compiler on this machine (CC, else gcc / clang / zig cc) and runs it.

A mod whose defaults are spread over several files (field-promotions) marks the parts instead, between
/* inigen:begin */ and /* inigen:end */ in any src/*.c: the settings and the writers. They are compiled together and its
main source's *_write_defaults(FILE*) is run. Its source is src/<mod>.c with - read as _ (field_promotions.c).
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

REGION = re.compile(r'/\* inigen:begin \*/\n(.*?)/\* inigen:end \*/', re.S)

def marked(mod, main, s, ver):
    """The program for a mod that marks its settings and writers (see above)."""
    srcdir = os.path.dirname(main)
    files = [main] + sorted(os.path.join(srcdir, f) for f in os.listdir(srcdir)
                            if f.endswith('.c') and os.path.join(srcdir, f) != main)
    parts = [r for f in files for r in REGION.findall(open(f, encoding='utf-8').read())]
    writers = re.findall(r'^void (\w+)\(FILE\* f\)', '\n'.join(parts), re.M)
    entry = next(w for w in re.findall(r'^void (\w+_write_defaults)\(FILE\* f\)', '\n'.join(REGION.findall(s)), re.M))
    protos = ''.join('void %s(FILE* f);\n' % w for w in writers)
    return ('#include <stdio.h>\n#include <stdint.h>\n#define VERSION "%s"\n%s%s\nint main(void){%s(stdout);return 0;}\n'
            % (ver, protos, '\n'.join(parts), entry))

def gen(mod):
    src = os.path.join(ROOT, 'mods', mod, 'src', mod + '.c')
    if not os.path.exists(src):
        src = os.path.join(ROOT, 'mods', mod, 'src', mod.replace('-', '_') + '.c')
    s = open(src, encoding='utf-8').read()
    ver = re.search(r'#define VERSION "([^"]+)"', s).group(1)
    if REGION.search(s):
        build(mod, ver, marked(mod, src, s, ver))
        return
    key = re.search(r'typedef struct Key \{.*?\} Key;', s, re.S).group(0)
    decls = re.findall(r'^static (?:int|float|double|unsigned|uint32_t|Key) s_\w[^;]*;', s, re.M)
    i = s.index('fprintf(f, "; ')
    stmt = s[i:s.index(');\n', i) + 2]
    prog = ('#include <stdio.h>\n#include <stdint.h>\n#define VERSION "%s"\n%s\n%s\nint main(void){FILE* f=stdout;\n%s\nreturn 0;}\n'
            % (ver, key, '\n'.join(decls), stmt))
    build(mod, ver, prog)

def build(mod, ver, prog):
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
