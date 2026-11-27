"""Checks a mod before it is released: the basic things a play test can miss.

    python tools/precheck.py hina w                  these mods
    python tools/precheck.py --all                   every mod under mods/
    python tools/precheck.py hina --session <dir>    also read a played session's logs (HellForge/logs/sessions/<id>)
    python tools/precheck.py hina --sdk <dir>        build it against this HellForge SDK (default ../HellForge/app/sdk)

Each check prints PASS, WARN or FAIL with one line saying why; the run exits 1 when anything FAILs. tools/package.py
runs it and refuses a mod that FAILs (--skip-check to package anyway).

What it checks, for every mod:
  manifest       hellforge-mod.json reads; id, name, version, description; its dll is the source's; its version is the
                 source's VERSION; its icon file is there
  icon           icon.png is there, 256 x 256, not a flat or blank picture, not another mod's icon
  keys           no default key on F5 (HellForge's) or F8 (its editor), in the .ini or the options key
  ini            the shipped .ini is what the source writes (tools/inigen.py), when the source writes one
  files          nothing that must not ship: game dumps, crash dumps, built binaries, BlazBlue voices, parry samples
  changelog      CHANGELOG.md has an entry for "<Name> <version>"
  notices        THIRD_PARTY_NOTICES.md is there, and its "is a character of" line names this mod (a copied file
                 still naming another kit is the usual slip)
  build          it builds with -Wall -Werror against the SDK (skipped without a compiler or the SDK)
for a character kit (one that includes kitbase/kb_options.h), also:
  kitbase        its kit base copies are current (tools/kitbase_sync.py --check)
  options        its options screen: kit_options.h has rows, matches the HUD's OPTIONS and options.json
                 (tools/optgen.py), kit_icon.h matches icon.png, and the source starts the screen
  hud            hud-src/<kit>_hud.lua and hd2runtime.json are there; data/ has the built HUD, and the built HUD holds
                 the current Lua (its first 2000 characters)
  skill icons    every image the HUD asks for (hd2.resources.image) is in hud-src/images: 256 x 256, not flat or
                 blank, not another icon's copy; images there are in the built HUD's resources
  hud contract   the HUD's ffi struct has the DLL's HUD struct's fields in the same order, and every function the HUD
                 looks up in the DLL is exported
with --session:
  session        the mod's log has no REFUSED, crash, "not the reviewed" or "could not" lines; the session did not crash
"""
import argparse, glob, hashlib, json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
BAD_KEYS = {'F5': 'HellForge uses F5', 'F8': 'F8 opens HellForge\'s editor'}
BAD_VK = {0x74: 'F5', 0x77: 'F8'}
NEVER_SHIP = [(r'\.dump$', 'a game dump'), (r'\.dmp$', 'a crash dump'), (r'\.(dll|lib|pdb|exe)$', 'a built binary'),
              (r'(?i)blazblue|bbcf|hakumen_v', 'BlazBlue voice audio'), (r'(?i)parry.*(clash|\.wav|\.ogg)|clash.*\.(wav|ogg)', 'a parry clash sample')]


class Report:
    def __init__(self, mod):
        self.mod, self.rows = mod, []

    def add(self, level, what, why):
        self.rows.append((level, what, why))

    def ok(self, what, why=''): self.add('PASS', what, why)
    def warn(self, what, why): self.add('WARN', what, why)
    def fail(self, what, why): self.add('FAIL', what, why)

    def failed(self): return any(r[0] == 'FAIL' for r in self.rows)

    def show(self):
        worst = 'FAIL' if self.failed() else 'WARN' if any(r[0] == 'WARN' for r in self.rows) else 'PASS'
        print(f'\n== {self.mod}: {worst}')
        for level, what, why in self.rows:
            print(f'  {level:4}  {what:13} {why}')


def read(path, mode='r'):
    with open(path, mode, **({} if 'b' in mode else {'encoding': 'utf-8', 'errors': 'replace'})) as f:
        return f.read()


def main_source(mod):
    d = os.path.join(ROOT, 'mods', mod, 'src')
    c = os.path.join(d, mod.replace('-', '_') + '.c')
    if os.path.exists(c):
        return c
    cs = sorted(glob.glob(os.path.join(d, '*.c')))
    return cs[0] if cs else None


def is_kit(mod):
    c = main_source(mod)
    return bool(c) and '#include "kitbase/kb_options.h"' in read(c)


# ---- pictures --------------------------------------------------------------------------------------------------------
def picture(path):
    """(width, height, flat, has_alpha, digest): flat is a picture of (nearly) one colour, or all transparent"""
    from PIL import Image, ImageStat
    im = Image.open(path)
    im.load()
    rgba = im.convert('RGBA')
    alpha = rgba.getchannel('A')
    has_alpha = alpha.getextrema()[0] < 255
    visible = alpha.getextrema()[1] > 8
    stat = ImageStat.Stat(rgba.convert('RGB'), mask=alpha.point(lambda a: 255 if a > 8 else 0) if visible else None)
    spread = max(stat.stddev) if visible else 0
    hist = alpha.histogram()
    coverage = sum(hist[9:]) / float(im.size[0] * im.size[1])
    # flat: nothing to see, or a square of one colour (a one-colour silhouette on a clear ground is a real icon)
    return im.size, (not visible) or (coverage > 0.9 and spread < 6), has_alpha, hashlib.sha1(rgba.tobytes()).hexdigest()


def all_icon_digests(skip_mod):
    out = {}
    for path in glob.glob(os.path.join(ROOT, 'mods', '*', 'icon.png')) + glob.glob(os.path.join(ROOT, 'mods', '*', 'hud-src', 'images', '*.png')):
        if os.sep + skip_mod + os.sep in path:
            continue
        try:
            out.setdefault(picture(path)[3], path)
        except Exception:
            pass
    return out


def check_picture(r, what, path, others, want=(256, 256)):
    try:
        size, flat, alpha, digest = picture(path)
    except Exception as e:
        r.fail(what, f'{os.path.relpath(path, ROOT)} does not open as a picture ({e})')
        return None
    rel = os.path.relpath(path, ROOT)
    if flat:
        r.fail(what, f'{rel} is a flat or blank picture (a placeholder?)')
    elif digest in others:
        r.fail(what, f'{rel} is the same picture as {os.path.relpath(others[digest], ROOT)}')
    elif want and tuple(size) != want:
        r.warn(what, f'{rel} is {size[0]} x {size[1]}, not {want[0]} x {want[1]}')
    else:
        r.ok(what, rel)
    return digest


# ---- the checks --------------------------------------------------------------------------------------------------------
def check_manifest(r, mod, src):
    path = os.path.join(ROOT, 'mods', mod, 'hellforge-mod.json')
    try:
        m = json.loads(read(path))
    except Exception as e:
        r.fail('manifest', f'hellforge-mod.json does not read ({e})')
        return None
    missing = [k for k in ('id', 'name', 'version', 'description') if not m.get(k)]
    if missing:
        r.fail('manifest', 'missing ' + ', '.join(missing))
    ver = re.search(r'#define VERSION "([^"]+)"', read(src)) if src else None
    if ver and m.get('version') != ver.group(1):
        r.fail('manifest', f'version {m.get("version")} but the source says {ver.group(1)}')
    if m.get('icon') and not os.path.exists(os.path.join(ROOT, 'mods', mod, m['icon'])):
        r.fail('manifest', f'its icon {m["icon"]} is not there')
    if not r.failed():
        r.ok('manifest', f'{m.get("id")} {m.get("version")}')
    return m


def check_icon(r, mod, manifest):
    path = os.path.join(ROOT, 'mods', mod, (manifest or {}).get('icon') or 'icon.png')
    if not os.path.exists(path):
        (r.fail if is_kit(mod) else r.warn)('icon', 'no icon.png')
        return
    check_picture(r, 'icon', path, all_icon_digests(mod))


def check_keys(r, mod):
    bad = []
    for ini in glob.glob(os.path.join(ROOT, 'mods', mod, '*.ini')):
        for line in read(ini).splitlines():
            m = re.match(r'\s*(key\w*|options_key)\s*=\s*(\S+)', line)
            if not m:
                continue
            v = m.group(2)
            if m.group(1) == 'options_key':
                if v.isdigit() and int(v) in BAD_VK:
                    bad.append(f'{m.group(1)} = {v} ({BAD_VK[int(v)]})')
            elif v.split('+')[-1].upper() in BAD_KEYS:
                bad.append(f'{m.group(1)} = {v}')
    src = main_source(mod)
    if src:
        for m in re.finditer(r"\{(?:'|VK_)(F[58])\b", read(src)):
            bad.append(f'a default key {m.group(1)} in the source')
    if bad:
        r.fail('keys', '; '.join(bad) + ' (avoid F5 and F8)')
    else:
        r.ok('keys', 'no default on F5 or F8')


def check_ini(r, mod, src):
    own = os.path.join(ROOT, 'mods', mod, 'src', mod + '.c')
    text = read(own) if os.path.exists(own) else ''
    if 'fprintf(f, "; ' not in text:
        return
    ini = os.path.join(ROOT, 'mods', mod, mod + '.ini')
    if not os.path.exists(ini):
        (r.fail if is_kit(mod) else r.warn)('ini', f'{mod}.ini is not there (tools/inigen.py {mod}): it runs on its built-in defaults')
        return
    import inigen
    try:
        inigen.compiler()
    except SystemExit:
        r.warn('ini', 'no C compiler here: not compared with the source (set CC)')
        return
    before = read(ini, 'rb')
    try:
        import contextlib, io
        with contextlib.redirect_stdout(io.StringIO()):
            inigen.gen(mod)
        after = read(ini, 'rb')
    except Exception as e:
        r.warn('ini', f'the source\'s defaults could not be written ({e})')
        after = before
    finally:
        with open(ini, 'wb') as f:
            f.write(before)
    def settings(raw):
        text = raw.decode('utf-8', 'replace').replace('\r\n', '\n')
        head = re.match(r'; \S+ (\S+?)\.? ', text)
        return (head.group(1) if head else None), dict(re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', text, re.M))
    (v1, shipped), (v2, wanted) = settings(before), settings(after)
    wrong = [f'{k} = {shipped.get(k, "(missing)")} (the source: {v})' for k, v in wanted.items() if shipped.get(k) != v]
    if wrong:
        r.fail('ini', f'{mod}.ini differs from the source\'s defaults: ' + '; '.join(wrong[:3]) + f' (tools/inigen.py {mod})')
    elif v1 != v2:
        r.fail('ini', f'{mod}.ini says version {v1}, the source {v2} (tools/inigen.py {mod})')
    else:
        r.ok('ini', 'matches the source\'s defaults')


def check_files(r, mod):
    bad = []
    for root, dirs, names in os.walk(os.path.join(ROOT, 'mods', mod)):
        dirs[:] = [d for d in dirs if d not in ('build', '__pycache__') and not d.startswith('.')]
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), os.path.join(ROOT, 'mods', mod))
            for pat, what in NEVER_SHIP:
                if re.search(pat, rel):
                    bad.append(f'{rel} ({what})')
    if bad:
        r.fail('files', '; '.join(bad[:6]))
    else:
        r.ok('files', 'nothing that must not ship')


def check_changelog(r, manifest):
    if not manifest:
        return
    name, ver = manifest.get('name', ''), manifest.get('version', '')
    text = read(os.path.join(ROOT, 'CHANGELOG.md')) if os.path.exists(os.path.join(ROOT, 'CHANGELOG.md')) else ''
    heads = [l for l in text.splitlines() if l.startswith('## ')]
    if any(re.search(r'\b%s %s\b' % (re.escape(name), re.escape(ver)), h) for h in heads):
        r.ok('changelog', f'an entry for {name} {ver}')
    else:
        r.warn('changelog', f'no CHANGELOG.md entry for {name} {ver} yet (one, since the last public version)')


def check_notices(r, mod, manifest):
    path = os.path.join(ROOT, 'mods', mod, 'THIRD_PARTY_NOTICES.md')
    if not os.path.exists(path):
        (r.fail if is_kit(mod) else r.warn)('notices', 'no THIRD_PARTY_NOTICES.md')
        return
    text = read(path)
    name = (manifest or {}).get('name', mod)
    m = re.search(r'^(.*) is a character of', text, re.M)
    if m and name.split()[0].lower() not in m.group(1).lower():
        r.fail('notices', f'"{m.group(0)[:60]}..." names someone else (copied from another kit?)')
        return
    others = [k for k in os.listdir(os.path.join(ROOT, 'mods')) if k != mod and is_kit(k)]
    leftover = [k for k in others if re.search(r'^%s is (built on|a character)' % re.escape(k.capitalize()), text, re.M)]
    if leftover:
        r.fail('notices', 'starts with another kit\'s name: ' + ', '.join(leftover))
    else:
        r.ok('notices', 'there, and names this mod')


def compiler_for_windows():
    if os.environ.get('HFCC'):   # (CC is tools/inigen.py's: a compiler for this machine)
        return os.environ['HFCC'].split()
    for c in ('x86_64-w64-mingw32-gcc',):
        if shutil.which(c):
            return [c]
    if os.name == 'nt':
        for c in ('gcc', 'clang'):
            if shutil.which(c):
                return [c]
    if shutil.which('zig'):
        return ['zig', 'cc', '-target', 'x86_64-windows-gnu']
    try:
        import ziglang  # noqa: F401
        return [sys.executable, '-m', 'ziglang', 'cc', '-target', 'x86_64-windows-gnu']
    except ImportError:
        return None


def check_build(r, mod, sdk):
    srcs = sorted(glob.glob(os.path.join(ROOT, 'mods', mod, 'src', '*.c')))
    if not srcs:
        return
    if not sdk or not os.path.exists(os.path.join(sdk, 'hellforge_mod.h')):
        r.warn('build', f'no SDK at {sdk}: not built (--sdk <HellForge>/app/sdk)')
        return
    cc = compiler_for_windows()
    if not cc:
        r.warn('build', 'no compiler for Windows here (HFCC, mingw gcc or zig): not built')
        return
    with tempfile.TemporaryDirectory() as tmp:
        cmd = cc + ['-shared', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                    '-I', sdk, '-I', os.path.join(ROOT, 'mods', mod, 'src')] + srcs + ['-o', os.path.join(tmp, 'm.dll')]
        p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        first = next((l for l in (p.stderr or p.stdout).splitlines() if 'error' in l), (p.stderr or p.stdout).strip()[:200])
        r.fail('build', first.strip()[:220])
    else:
        r.ok('build', 'builds with -Wall -Werror')


def check_kitbase(r, mod):
    import kitbase_sync
    try:
        bad, extra = kitbase_sync.stale(mod)
        huds = [p for p in kitbase_sync.hud_files([mod]) if kitbase_sync.hud_synced(p)[0] != kitbase_sync.hud_synced(p)[1]]
    except Exception as e:
        r.warn('kitbase', f'could not be compared ({e})')
        return
    if bad or extra or huds:
        r.fail('kitbase', 'out of sync: ' + ', '.join(bad + extra + [os.path.basename(p) for p in huds]) + ' (tools/kitbase_sync.py)')
    else:
        r.ok('kitbase', 'its copies (C and HUD) are current')


def check_options(r, mod, src):
    d = os.path.join(ROOT, 'mods', mod, 'src')
    rows = os.path.join(d, 'kit_options.h')
    if not os.path.exists(rows):
        r.fail('options', 'no src/kit_options.h: its options screen has no rows (tools/optgen.py)')
        return
    n = len(re.findall(r'^\s*\{"\w+", "', read(rows), re.M))
    if not n:
        r.fail('options', 'kit_options.h has no rows')
    if 'KbOptionsStart(' not in read(src):
        r.fail('options', 'the source never starts its options screen (KbOptionsStart)')
    if not os.path.exists(os.path.join(d, 'kit_icon.h')):
        r.fail('options', 'no src/kit_icon.h: no icon on the options screen')
    try:
        import optgen
        if read(rows).split('*/', 1)[0].find('hand-written') < 0 and optgen.rows_text(mod) != read(rows):
            r.fail('options', 'kit_options.h is not what the HUD\'s OPTIONS and options.json give: run tools/optgen.py ' + mod)
        if os.path.exists(os.path.join(d, 'kit_icon.h')) and optgen.icon_text(mod) != read(os.path.join(d, 'kit_icon.h')):
            r.fail('options', 'kit_icon.h is not icon.png: run tools/optgen.py ' + mod)
    except SystemExit as e:
        r.fail('options', str(e))
    except Exception as e:
        r.warn('options', f'not compared with the HUD ({type(e).__name__}: {e})')
    if not r.rows or r.rows[-1][1] != 'options' or r.rows[-1][0] == 'PASS':
        if not any(x[1] == 'options' and x[0] == 'FAIL' for x in r.rows):
            r.ok('options', f'{n} rows, current, with its icon')


def hud_lua(mod):
    paths = glob.glob(os.path.join(ROOT, 'mods', mod, 'hud-src', '*_hud.lua'))
    return paths[0] if paths else None


def check_hud(r, mod):
    lua = hud_lua(mod)
    if not lua:
        r.fail('hud', 'no hud-src/<kit>_hud.lua: the kit has no HUD')
        return None
    if not os.path.exists(os.path.join(ROOT, 'mods', mod, 'hud-src', 'hd2runtime.json')):
        r.fail('hud', 'no hud-src/hd2runtime.json')
    patch = os.path.join(ROOT, 'mods', mod, 'data', '9ba626afa44a3aa3.patch_0')
    if not os.path.exists(patch) or os.path.getsize(patch) < 1024:
        r.fail('hud', 'data/ has no built HUD (build hud-src with HD2Runtime\'s SDK)')
        return lua
    if read(lua)[:2000].encode('utf-8') not in read(patch, 'rb'):
        r.fail('hud', 'the built HUD in data/ is not the current hud-src Lua: rebuild it')
    else:
        r.ok('hud', 'built, and current with hud-src')
    return lua


def check_skill_icons(r, mod, lua):
    text = re.sub(r'--[^\n]*', '', read(lua))   # (comments out: a note may name images that are not loaded)
    ids = []
    # a list walked into hd2.resources.image: for _, id in ipairs({'a', 'b'}) do ... image, 'prefix_' .. id / image, id
    # (and a helper that loads its argument: local function load_icon(id) ... hd2.resources.image, id)
    loaders = [m.group(1) for m in re.finditer(r'local\s+function\s+(\w+)\s*\(\s*(\w+)\s*\)(.{0,200}?)\bend\b', text, re.S)
               if re.search(r'hd2\.resources\.image\s*,\s*%s\b' % m.group(2), m.group(3))]
    for m in re.finditer(r'for\s+\w+\s*,\s*(\w+)\s+in\s+ipairs\(\{([^}]*)\}\)\s*do(.{0,300})', text, re.S):
        var, items, body = m.group(1), re.findall(r"'([\w.-]+)'", m.group(2)), m.group(3)
        pre = re.search(r"(?:hd2\.resources\.image\s*,|%s\s*\()\s*'([\w.-]*)'\s*\.\.\s*%s\b" % ('|'.join(loaders) or 'x^', var), body)
        if pre:
            ids += [pre.group(1) + i for i in items]
        elif re.search(r'hd2\.resources\.image\s*,\s*%s\b' % var, body) or any(re.search(r'\b%s\s*\(\s*%s\s*\)' % (f, var), body) for f in loaders):
            ids += items
    ids += re.findall(r"hd2\.resources\.image\s*(?:,|\()\s*'([\w.-]+)'\s*\)", text)
    ids = list(dict.fromkeys(ids))
    if not ids:
        r.fail('skill icons', 'the HUD asks for no images: its skill tiles show short names (placeholders)')
        return
    images = os.path.join(ROOT, 'mods', mod, 'hud-src', 'images')
    others = all_icon_digests(mod)
    seen, missing = {}, []
    for i in ids:
        path = os.path.join(images, i + '.png')
        if not os.path.exists(path):
            missing.append(i)
            continue
        dig = check_picture(r, 'skill icons', path, others)
        if dig and dig in seen:
            r.fail('skill icons', f'{i} is the same picture as {seen[dig]}')
        seen[dig] = i
    if missing:
        r.fail('skill icons', 'no picture for ' + ', '.join(missing) + ' (hud-src/images/<id>.png): placeholders in game')
    gpu = os.path.join(ROOT, 'mods', mod, 'data', '9ba626afa44a3aa3.patch_0.gpu_resources')
    if seen and (not os.path.exists(gpu) or os.path.getsize(gpu) == 0):
        r.fail('skill icons', 'the built HUD holds no pictures (data/*.gpu_resources is empty): rebuild it with the images')


def c_struct_fields(body):
    fields = []
    for decl in re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S).split(';'):
        decl = decl.strip()
        if not decl:
            continue
        m = re.match(r'(?:const\s+)?(?:unsigned\s+)?[\w]+\s+(.*)', decl, re.S)
        if not m:
            continue
        for part in m.group(1).split(','):
            name = re.match(r'\s*\**\s*(\w+)', part)
            if name:
                fields.append(name.group(1))
    return fields


def check_contract(r, mod, src, lua):
    c, l = read(src), read(lua)
    lm = re.search(r'typedef struct (\w+_state)\s*\{(.*?)\}\s*\1\s*;', l, re.S)
    cm = None
    for m in re.finditer(r'typedef struct (\w+)\s*\{(.*?)\}\s*\1\s*;', c, re.S):
        if m.group(1).lower().endswith('hud'):
            cm = m
    if not lm or not cm:
        r.warn('hud contract', 'its HUD struct was not found on both sides: not compared')
    else:
        lf, cf = c_struct_fields(lm.group(2)), c_struct_fields(cm.group(2))
        if lf != cf[:len(lf)]:
            k = next((i for i in range(min(len(lf), len(cf))) if lf[i] != cf[i]), min(len(lf), len(cf)))
            r.fail('hud contract', f'the HUD reads {cm.group(1)} wrong from field {k + 1}: Lua {lf[k:k + 3]} vs C {cf[k:k + 3]}')
        else:
            r.ok('hud contract', f'{lm.group(1)}: {len(lf)} fields match {cm.group(1)}')
    kit = mod.replace('-', '_')
    exported = set(re.findall(r'HF_MOD_EXPORT\s+[\w\s\*]+?\b(\w+)\s*\(', c))
    if re.search(r'KB_ROSTER_EXPORTS\(%s\)' % kit, c):
        base = read(os.path.join(ROOT, 'kitbase', 'kb_roster.h'))
        exported |= {n.replace('kit##', kit) for n in re.findall(r'(kit##_\w+)\s*\(', base)}
    if re.search(r'KB_OPTIONS_EXPORTS\(%s\)' % kit, c):
        exported.add(kit + '_options_screen')
    wanted = set(re.findall(r"GetProcAddress\(\w+,\s*'(%s_\w+)'\)" % kit, l)) | set(re.findall(r"KIT\.proc\(\w+,\s*'(%s_\w+)'\)" % kit, l))
    wanted |= {kit + '_' + n for n in re.findall(r"gfl2_dll\(['\"](\w+)['\"]\)", l)}
    missing = sorted(w for w in wanted if w not in exported)
    if missing:
        r.fail('hud contract', 'the HUD looks up what the DLL does not export: ' + ', '.join(missing))
    elif wanted:
        r.ok('hud contract', f'{len(wanted)} DLL functions the HUD uses are exported')


def check_session(r, mod, manifest, session):
    mid = (manifest or {}).get('id', '')
    log = os.path.join(session, f'mod-{mid}.log')
    if not os.path.exists(log):
        r.warn('session', f'no mod-{mid}.log in that session (was the mod on?)')
        return
    bad = [l.strip() for l in read(log).splitlines() if re.search(r'REFUSED|\bcrash|not the reviewed|could not|is off\b|unavailable', l)]
    loader = os.path.join(session, 'modloader.log')
    crash = [l.strip() for l in read(loader).splitlines() if 'crash:' in l][:2] if os.path.exists(loader) else []
    if crash:
        r.fail('session', 'the session crashed: ' + crash[0][:160])
    if bad:
        r.warn('session', f'{len(bad)} line(s) to read in mod-{mid}.log, the first: ' + bad[0][:150])
    elif not crash:
        r.ok('session', f'mod-{mid}.log: nothing refused or off')


def check(mod, sdk, session):
    r = Report(mod)
    if not os.path.isdir(os.path.join(ROOT, 'mods', mod)):
        r.fail('mod', 'no such folder under mods/')
        return r
    src = main_source(mod)
    manifest = check_manifest(r, mod, src)
    check_icon(r, mod, manifest)
    check_keys(r, mod)
    check_ini(r, mod, src)
    check_files(r, mod)
    check_changelog(r, manifest)
    check_notices(r, mod, manifest)
    if is_kit(mod):
        check_kitbase(r, mod)
        check_options(r, mod, src)
        lua = check_hud(r, mod)
        if lua:
            check_skill_icons(r, mod, lua)
            check_contract(r, mod, src, lua)
    check_build(r, mod, sdk)
    if session:
        check_session(r, mod, manifest, session)
    return r


def run(mods, sdk=None, session=None, quiet=False):
    """the reports; prints them unless quiet"""
    sdk = sdk or os.path.join(ROOT, '..', 'HellForge', 'app', 'sdk')
    reports = [check(m, os.path.abspath(sdk), session) for m in mods]
    if not quiet:
        for rep in reports:
            rep.show()
    return reports


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mods', nargs='*')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--sdk')
    ap.add_argument('--session')
    a = ap.parse_args()
    mods = sorted(os.listdir(os.path.join(ROOT, 'mods'))) if a.all else a.mods
    if not mods:
        sys.exit(__doc__)
    reports = run(mods, a.sdk, a.session)
    failed = [r.mod for r in reports if r.failed()]
    print('\n%d mod(s) checked; %s' % (len(reports), ('FAIL: ' + ', '.join(failed)) if failed else 'nothing failed'))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
