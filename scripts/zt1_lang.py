#!/usr/bin/env python3
"""Extra-language text patches for Zenonia 2 (data/<lang>/*.zt1).

  python3 scripts/zt1_lang.py export es   # write/refresh translation templates
  python3 scripts/zt1_lang.py build  es   # pack them into ux0_data/.../data/spa/
  python3 scripts/zt1_lang.py test        # self-check: identity repack == original

Sources are ALWAYS the untouched English files in apk_extract/assets/data/eng/.
Templates live in zenonia2_translation/<lang>/<File>.json as a list of
{"en": ..., "tr": ...} (+ "i" = string index for Str*, "max" = byte limit for Xls*).
Empty/missing "tr" = keep English.

Formats (verified against GxUncompressZT1 / the original files):
  .zt1  = u32 file_size, u32 decompressed_size, zlib stream
  GST   = "GST\\x01", u16 n, u16 offsets[n+1] (string starts + end), raw bytes
          (no implicit terminator; trailing NULs are part of each string)
  GXL   = "GXL\\x01", u16 rec_size, u16 header_size, header, fixed-size records;
          text lives in fixed NUL-padded columns (COLS below, found by hand).

The game font is ASCII + EUC-KR (any byte >= 0x80 is read as a Korean
double-byte char), so translations are folded to ASCII on build.
"""
import json, os, re, struct, sys, unicodedata, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'apk_extract/assets/data/eng')
TPL = os.path.join(ROOT, 'zenonia2_translation')
OUT = os.path.join(ROOT, 'ux0_data/zenonia-2/assets/data')
LANG_DIR = {'es': 'spa', 'pt': 'por'}  # must match langs[] in loader/controls.c
SO = os.path.join(ROOT, 'apk_extract/lib/armeabi/libzenonia2.so')
SO_MD5 = 'cae9d5fe91ecb847aecee45a11b58f53'  # addresses below are only valid for this build
LIB = '_libzenonia2'  # template name for UI strings hardcoded in the .so
# (file_lo, file_hi, vaddr - file_offset): .rodata and .data
SO_RANGES = [(0xe9500, 0xe24d8 + 0xc4d0, 0), (0xffc50, 0xffc50 + 0xedc, 0x1000)]
SO_SKIP_ADDR = [(0xed49c, 0xed6c0)]  # sprite/resource names looked up by string
SO_SKIP = re.compile(
    r'/|\.[a-z0-9]{2,4}$|^_|^%s%|^(Str[A-Z]|Xls|SCH|KTF|LG-|SPH|Yamaha|CHARACTER_|TERMSVC|YSMEN|PQ>R|RQRQ|00FF00$|KOR$|ENG$|Lepe$|Tiaris$)'
    r'|^[A-Z][a-z]+[A-Z][A-Za-z0-9]*$|^[a-z_]+$')

# Text column offsets inside each GXL record. Other columns are numbers.
COLS = {
    'XlsChar': [10], 'XlsChargeItem': [19], 'XlsItem': [12], 'XlsMap': [10],
    'XlsNPC': [11], 'XlsPCNew': [18], 'XlsQUEST': [11], 'XlsSetItem': [16, 27],
    'XlsSkill': [10],
}
# Any '%' sequence is a printf conversion to the engine ("% d" too), any !X is markup.
TOKEN = re.compile(r'!c[0-9A-Fa-f]{6}|![A-Za-z]|%[ #+\-0-9.]*[A-Za-z%]?')


def zt1_read(path):
    d = open(path, 'rb').read()
    fsize, dsize = struct.unpack('<II', d[:8])
    u = zlib.decompress(d[8:])
    assert fsize == len(d) and dsize == len(u), path
    return bytearray(u)


def zt1_write(path, u):
    c = zlib.compress(bytes(u), 9)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(struct.pack('<II', len(c) + 8, len(u)) + c)


def gst_strings(u):
    n = struct.unpack_from('<H', u, 4)[0]
    t = struct.unpack_from('<%dH' % (n + 1), u, 6)
    return [bytes(u[t[i]:t[i + 1]]) for i in range(n)]


def gst_pack(strings):
    n = len(strings)
    offs, pos = [], 6 + 2 * (n + 1)
    for s in strings:
        offs.append(pos)
        pos += len(s)
    offs.append(pos)
    assert pos <= 0xFFFF, 'GST too big for u16 offsets'
    return bytearray(b'GST\x01' + struct.pack('<%dH' % (n + 2), n, *offs) + b''.join(strings))


def gxl_cells(u, cols):
    """Yield (abs_offset, capacity_without_nul, text) for every text cell."""
    rs, hs = struct.unpack_from('<HH', u, 4)
    nrec = (len(u) - hs) // rs
    caps = {}
    for ci, c in enumerate(cols):
        end = cols[ci + 1] if ci + 1 < len(cols) else rs
        cap = end - c - 1
        for r in range(nrec):  # shrink cap until the first non-zero byte seen after any text
            rec = u[hs + r * rs: hs + (r + 1) * rs]
            z = rec.find(0, c, end)
            if z <= c:
                continue
            k = z
            while k < end and rec[k] == 0:
                k += 1
            cap = min(cap, k - c - 1)
        caps[c] = cap
    for r in range(nrec):
        base = hs + r * rs
        for c in cols:
            a = base + c
            z = u.find(0, a, base + rs)
            if z > a:
                yield a, caps[c], bytes(u[a:z])


def kind(name):
    return 'gst' if name.startswith('Str') else 'gxl' if name in COLS else None


def ascii_fold(s):
    s = s.replace('¿', '').replace('¡', '').replace('º', 'o').replace('ª', 'a')
    s = s.replace('“', '"').replace('”', '"').replace('‘', "'").replace('’', "'").replace('…', '...').replace('–', '-').replace('—', '-')
    s = ''.join(ch for ch in unicodedata.normalize('NFKD', s) if not unicodedata.combining(ch))
    return s.encode('ascii', 'replace').decode('ascii')


def source_files():
    for f in sorted(os.listdir(SRC)):
        if f.endswith('.zt1') and not f.startswith('._') and kind(f[:-4]):
            yield f[:-4]


def entries(name):
    """Translatable English strings of a source file, as template entries."""
    u = zt1_read(os.path.join(SRC, name + '.zt1'))
    out, seen = [], set()
    if kind(name) == 'gst':
        for i, s in enumerate(gst_strings(u)):
            t = s.rstrip(b'\0')
            if t and all(32 <= b < 127 for b in t) and re.search(rb'[A-Za-z]{2}', t):
                out.append({'i': i, 'en': t.decode()})
    else:
        for _, cap, t in gxl_cells(u, COLS[name]):
            if all(32 <= b < 127 for b in t) and t.decode() not in seen and t != b'NULL':
                seen.add(t.decode())
                out.append({'en': t.decode(), 'max': cap})
    return out


def so_entries():
    """UI strings in the .so: [{en, max, addrs}], max = smallest slot among copies."""
    import hashlib
    d = open(SO, 'rb').read()
    assert hashlib.md5(d).hexdigest() == SO_MD5, 'unexpected libzenonia2.so'
    ents = {}
    for lo, hi, dv in SO_RANGES:
        ss = [(lo + m.start(), m.group()) for m in re.finditer(rb'[ -~]{2,}', d[lo:hi])
              if d[lo + m.end()] == 0]
        for k, (o, t) in enumerate(ss):
            s = t.decode()
            if (SO_SKIP.search(s) or any(a <= o < b for a, b in SO_SKIP_ADDR)
                    or not re.search(r'[A-Za-z]{2}', TOKEN.sub('', s))):
                continue
            nz = o + len(t)
            while d[nz] == 0:
                nz += 1
            prev = o - ss[k - 1][0] if k else 0
            nxt = ss[k + 1][0] - o if k + 1 < len(ss) else 0
            n2 = ss[k + 2][0] - ss[k + 1][0] if k + 2 < len(ss) else 0
            p2 = ss[k - 1][0] - ss[k - 2][0] if k > 1 else 0
            if nxt % 4 and nxt in (prev, n2):      # member of a char[N] array
                cap = nxt - 1
            elif prev % 4 and prev == p2:
                cap = prev - 1
            elif o % 4 == 0:                       # standalone literal, 4-aligned
                cap = (len(t) + 4) // 4 * 4 - 1
            else:
                cap = len(t)
            cap = max(len(t), min(cap, nz - o - 1))
            e = ents.setdefault(s, {'en': s, 'max': cap, 'addrs': []})
            e['max'] = min(e['max'], cap)
            e['addrs'].append('%x' % (o + dv))
    return list(ents.values())


def export(lang):
    for name in list(source_files()) + [LIB]:
        path = os.path.join(TPL, lang, name + '.json')
        old = {}
        if os.path.exists(path):
            old = {(e.get('i'), e['en']): e.get('tr', '') for e in json.load(open(path, encoding='utf-8'))}
        es = so_entries() if name == LIB else entries(name)
        for e in es:
            e['tr'] = old.get((e.get('i'), e['en']), '')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(es, f, ensure_ascii=False, indent=1)
        print('%-22s %4d strings' % (name, len(es)))


def checked(e, warn):
    """ASCII-folded translation bytes, or None to keep English."""
    tr = e.get('tr') or ''
    if not tr.strip():
        return None
    tr = ascii_fold(tr)
    if any(ord(ch) < 32 for ch in tr):
        warn('control chars in translation, keeping English: %r' % tr)
        return None
    toks = lambda x: sorted(t for t in TOKEN.findall(x) if 'max' not in e or 'addrs' in e or t[0] != '%')
    # Xls text is drawn, not printf'd: the English itself has "50% additional".
    if toks(tr) != toks(e['en']):
        warn('format tokens differ, keeping English: %r' % e['en'][:60])
        return None
    b = tr.encode()
    if 'addrs' in e and len(b) > e['max']:  # printf'd code strings: never cut
        warn('too long (%d > %d), keeping English: %r' % (len(b), e['max'], tr))
        return None
    if 'max' in e and len(b) > e['max']:
        warn('too long (%d > %d), truncated: %r' % (len(b), e['max'], tr))
        b = b[:e['max']]
    return b


def build(lang):
    out = os.path.join(OUT, LANG_DIR[lang])
    total = done = 0
    for name in source_files():
        tpl = os.path.join(TPL, lang, name + '.json')
        if not os.path.exists(tpl):
            continue
        warn = lambda m: print('  [%s] %s' % (name, m))
        ents = json.load(open(tpl, encoding='utf-8'))
        u = zt1_read(os.path.join(SRC, name + '.zt1'))
        if kind(name) == 'gst':
            strs = gst_strings(u)
            for e in ents:
                b = checked(e, warn)
                total += 1
                if b is not None and strs[e['i']].rstrip(b'\0') == e['en'].encode():
                    nul = len(strs[e['i']]) - len(strs[e['i']].rstrip(b'\0'))
                    strs[e['i']] = b + b'\0' * nul
                    done += 1
            u = gst_pack(strs)
        else:
            tr = {}
            for e in ents:
                total += 1
                b = checked(e, warn)
                if b is not None:
                    tr[e['en'].encode()] = b
                    done += 1
            for a, cap, t in list(gxl_cells(u, COLS[name])):
                if t in tr:
                    b = tr[t][:cap]
                    u[a:a + cap + 1] = b + b'\0' * (cap + 1 - len(b))
        zt1_write(os.path.join(out, name + '.zt1'), u)
    tpl = os.path.join(TPL, lang, LIB + '.json')
    if os.path.exists(tpl):  # read by loader/main.c apply_lang_strings()
        warn = lambda m: print('  [%s] %s' % (LIB, m))
        with open(os.path.join(out, 'libstrings.txt'), 'w', newline='\n') as f:
            for e in json.load(open(tpl, encoding='utf-8')):
                total += 1
                b = checked(e, warn)
                if b is not None and b.decode() != e['en']:
                    done += 1
                    for a in e['addrs']:
                        f.write('%s\t%d\t%s\t%s\n' % (a, e['max'], e['en'], b.decode()))
    print('%s: %d/%d strings translated -> %s' % (lang, done, total, out))


def test():
    for f in sorted(os.listdir(SRC)):
        if not f.endswith('.zt1') or f.startswith('._'):
            continue
        u = zt1_read(os.path.join(SRC, f))
        if u[:4] == b'GST\x01':
            assert gst_pack(gst_strings(u)) == u, f
        for name in COLS:
            if f == name + '.zt1':
                for a, cap, t in gxl_cells(u, COLS[name]):
                    assert len(t) <= cap and not any(u[a + len(t): a + cap + 1]), (f, a, t)
    assert TOKEN.findall('%d% de !L!cFF0000x!') == ['%d', '% d', '!L', '!cFF0000']
    assert ascii_fold('¿Qué pasó, niño? ¡Ação!') == 'Que paso, nino? Acao!'
    print('ok')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'test':
        test()
    elif cmd in ('export', 'build') and len(sys.argv) > 2 and sys.argv[2] in LANG_DIR:
        globals()[cmd](sys.argv[2])
    else:
        sys.exit(__doc__)
