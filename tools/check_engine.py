#!/usr/bin/env python3
"""check_engine.py -- what the port reads inside libflapfire.so, checked
against the engine's own code in the user's APKs.

fbf_menu.c reads engine objects directly (the main menu's buttons, the
event queue), and fbf_game.c knows which engine events the Java gets. Their layout was taken from the disassembly; this
tool takes it from the disassembly AGAIN, for every APK given, and compares
it with the constants in the C source -- so a wrong assumption (hardware run
2026-09-25: the mode button is a dot_ToggleButton, not an ActiveButton) fails
here, on a PC, and not on a console.

    python3 tools/check_engine.py <the game's APK> [<another APK> ...]

Needs pyelftools and capstone.
"""
import io
import os
import re
import struct
import sys
import zipfile

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), 'source')


def source_layout():
    """The offsets fbf_menu.c uses, parsed from it."""
    s = open(os.path.join(SRC, 'fbf_menu.c')).read()
    d = {k: int(v, 16) for k, v in re.findall(r'#define (G_\w+|MS_\w+|FL_\w+) (0x[0-9a-f]+)', s)}
    lay = {}
    for name, body in re.findall(r'static const FbfMenuLayout (k_layout_\w+) = \{([^}]*)\}', s):
        lay[name] = {k: int(v, 16) for k, v in re.findall(r'\.(\w+) = (0x[0-9a-f]+)', body)}
    vts = dict(re.findall(r'\.(vt_\w+) = vtable\("(_ZTV\w+)"\)', s))
    d['flap_instance'] = re.search(r'\.flap_instance = so_try_find_addr_rx\(&g_mod_game, "(\w+)"\)', s).group(1)
    g = open(os.path.join(SRC, 'fbf_game.c')).read()
    d['java'] = [int(x) for x in re.search(r'k_java_code\[\] = \{([^}]*)\}', g).group(1).split(',')]
    d['java_104'] = int(re.search(r'#define JAVA_CODES_104 (\d+)', g).group(1))
    d['java_10'] = int(re.search(r'#define JAVA_CODES_10 (\d+)', g).group(1))
    d['events_max'] = int(re.search(r'#define FBF_EVENTS_MAX (\d+)', open(os.path.join(SRC, 'fbf.h')).read()).group(1))
    return d, lay, vts


class Lib:
    def __init__(self, blob):
        self.data = blob
        self.e = ELFFile(io.BytesIO(blob))
        self.syms = {}
        self.byaddr = {}
        for s in self.e.get_section_by_name('.dynsym').iter_symbols():
            if s.name and s['st_shndx'] != 'SHN_UNDEF':
                self.syms[s.name] = (s['st_value'], s['st_size'])
                self.byaddr.setdefault(s['st_value'] & ~1, s.name)
        self.segs = [(g['p_vaddr'], g['p_filesz'], g['p_offset']) for g in self.e.iter_segments()
                     if g['p_type'] == 'PT_LOAD']
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def off(self, va):
        for v, n, o in self.segs:
            if v <= va < v + n:
                return va - v + o
        raise KeyError(hex(va))

    def word(self, va):
        return struct.unpack_from('<I', self.data, self.off(va))[0]

    def insns(self, name):
        a, n = self.syms[name]
        a &= ~1
        return list(self.md.disasm(self.data[self.off(a):self.off(a) + n], a))

    def symbol_at(self, va):
        for n, (a, sz) in self.syms.items():
            if a <= va < a + max(sz, 1):
                return n
        return None


def mem_operands(insns, mnem_prefix):
    """(base, offset) of each load/store whose mnemonic starts with mnem_prefix."""
    out = []
    for i in insns:
        if i.mnemonic.split('.')[0] == mnem_prefix:
            m = re.search(r'\[(\w+), #(0x[0-9a-f]+|\d+)\]', i.op_str)
            if m:
                out.append((m.group(1), int(m.group(2), 0), i.op_str.split(',')[0]))
    return out


def got_vtables(lib, fn):
    """The vtables a function loads from its GOT (ldr rX,[pc,#i] + add rY,pc
    for the GOT base, then ldr rZ,[rY,rX])."""
    ins = lib.insns(fn)
    base = None
    lits = {}
    found = set()
    for k, i in enumerate(ins):
        m = re.match(r'(\w+), \[pc, #(0x[0-9a-f]+|\d+)\]', i.op_str)
        if i.mnemonic.startswith('ldr') and m:
            lits[m.group(1)] = lib.word(((i.address + 4) & ~3) + int(m.group(2), 0))
        m = re.match(r'(\w+), pc$', i.op_str)
        if i.mnemonic == 'add' and m and m.group(1) in lits and base is None:
            base = (lits[m.group(1)] + i.address + 4) & 0xffffffff
        m = re.match(r'(\w+), \[(\w+), (\w+)\]$', i.op_str)
        if i.mnemonic.startswith('ldr') and m and base is not None and m.group(3) in lits:
            slot = (base + lits[m.group(3)]) & 0xffffffff
            try:
                name = lib.symbol_at(lib.word(slot))
            except KeyError:
                continue
            if name and name.startswith('_ZTV'):
                found.add(name)
    return found


def check(apk, d, lay, vts):
    blob = zipfile.ZipFile(apk).read('lib/armeabi-v7a/libflapfire.so')
    lib = Lib(blob)
    has_release = 'Java_com_dotgears_dot_1JNILib_keyreleased' in lib.syms
    L = lay['k_layout_104' if has_release else 'k_layout_10']
    fails = []

    def want(cond, what):
        if not cond:
            fails.append(what)

    for k in ('vt_main', 'vt_sel', 'vt_toggle'):
        want(vts.get(k) in lib.syms, 'fbf_menu.c %s names a vtable of this engine' % k)
    # each object's class, from the code that makes it: the main scene and the
    # selectors by their constructors, the mode button by MainScene::init (the
    # one button it makes by hand)
    want(vts.get('vt_main') in got_vtables(lib, '_ZN9MainSceneC2Ev'), 'vt_main is what makes a MainScene')
    want(vts.get('vt_sel') in got_vtables(lib, '_ZN14SelectorButtonC2Ev'),
         'vt_sel is what makes a SelectorButton')
    want(got_vtables(lib, '_ZN9MainScene4initEv') == {vts.get('vt_toggle')},
         'vt_toggle (%s) is the mode button MainScene::init makes' % vts.get('vt_toggle'))
    # MainScene's fields
    st = {(b, o) for b, o, _ in mem_operands(lib.insns('_ZN9MainScene4initEv'), 'str')}
    for k in ('MS_SEL1', 'MS_SEL2', 'MS_MODE'):
        want(('r4', d[k]) in st, 'MainScene::init stores %s (+0x%x)' % (k, d[k]))
    rs = mem_operands(lib.insns('_ZN9MainScene5resetEv'), 'str')
    want(any(o == d['MS_FOCUS1'] for _, o, _ in rs), 'MainScene::reset sets the cursor at MS_FOCUS1')
    # SelectorButton: select(dev) stores the device, and bird 0
    sel = mem_operands(lib.insns('_ZN14SelectorButton6selectEi'), 'str')
    want(('r0', L['sel_dev'], 'r1') in sel, 'SelectorButton::select stores the device at +0x%x' % L['sel_dev'])
    want(any(o == L['sel_bird'] for _, o, _ in sel), 'SelectorButton::select sets the bird at +0x%x' % L['sel_bird'])
    up = mem_operands(lib.insns('_ZN14SelectorButton8scrollUpEv'), 'ldr')
    want(any(o == L['sel_bird'] for _, o, _ in up), 'scrollUp reads the bird at +0x%x' % L['sel_bird'])
    # the mode button's enabled byte, set by MainScene::update
    ub = mem_operands(lib.insns('_ZN9MainScene6updateEf'), 'strb')
    want(any(o == L['mode_enabled'] for _, o, _ in ub), 'MainScene::update sets the mode button enabled byte')
    # the Game's scene fields
    gs = mem_operands(lib.insns('Java_com_dotgears_dot_1JNILib_getSceneId'), 'ldr')
    want(any(o == d['G_SCENE_ID'] for _, o, _ in gs), 'getSceneId reads G_SCENE_ID')
    gk = lib.insns('_ZN4Game10keyPressedEii')
    want(any(o == d['G_MAIN_SCENE'] for _, o, _ in mem_operands(gk, 'ldr')), 'Game::keyPressed reads G_MAIN_SCENE')
    want(any(o == d['G_CUR_SCENE'] for _, o, _ in mem_operands(gk, 'str')), 'Game::keyPressed sets G_CUR_SCENE')
    # the event queue (fbf_menu.c; fbf_game.c takes a finished round's score from it)
    fl = lib.insns('_ZN12FlapListener11handleEventEiPv')
    want(any(o == d['FL_COUNT'] for _, o, _ in mem_operands(fl, 'ldr')), 'FlapListener count at FL_COUNT')
    fs = {o for _, o, _ in mem_operands(fl, 'str')}
    want(d['FL_CODES'] in fs and d['FL_DATA'] in fs, 'FlapListener codes at FL_CODES, data at FL_DATA')
    want((d['FL_DATA'] - d['FL_CODES']) // 4 == d['events_max'], 'the queue holds FBF_EVENTS_MAX codes')
    gi = lib.insns('Java_com_dotgears_dot_1JNILib_getOutputEvents')
    want(any(o == d['FL_COUNT'] for _, o, _ in mem_operands(gi, 'ldr')), 'getOutputEvents reads the count')
    want(any(i.mnemonic.startswith('add') and ('#0x%x' % d['FL_DATA']) in i.op_str for i in gi),
         'getOutputEvents reads the data at FL_DATA')
    want('_ZTV12FlapListener' in lib.syms, 'FlapListener vtable')
    si = lib.syms.get(d['flap_instance'])
    want(si is not None and si[1] == 4, '%s: a pointer the engine keeps' % d['flap_instance'])
    ins = lib.insns('_ZN12FlapListener8InstanceEv')
    want(any(i.mnemonic.startswith('ldr') for i in ins), 'FlapListener::Instance reads it')
    # engine event -> Java code (getOutputEvents), as fbf_game.c's k_java_code: a
    # case of its switch logs the engine's code and stores the Java's
    codes = [int(i.op_str.split('#')[1], 0) for i in gi if i.mnemonic == 'movs' and i.op_str.startswith('r3, #')]
    pairs = set(zip(codes[0::2], codes[1::2]))
    got = {e: j for e, j in pairs}
    known = d['java_104'] if has_release else d['java_10']
    want(any(i.mnemonic == 'cmp' and i.op_str == 'r3, #0x%x' % (known - 1) for i in gi),
         'getOutputEvents switches over %d engine codes' % known)
    for e, j in enumerate(d['java'][:known]):
        if j >= 0:
            want(got.get(e) == j, 'engine event %d is Java code %d' % (e, j))
        else:
            want(e not in got, 'engine event %d has no Java code' % e)
    # newScore: the score panel's virtual call, engine event 0 with the score
    ns = lib.syms['_ZN10dot_Engine8newScoreEi'][0]
    vt = lib.syms['_ZTV4Game'][0]
    want(lib.word(vt + 8 + 0x24) == ns, 'Game vtable +0x24 is newScore')
    return has_release, fails


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d, lay, vts = source_layout()
    bad = 0
    for apk in sys.argv[1:]:
        rel, fails = check(apk, d, lay, vts)
        name = os.path.basename(apk)
        if fails:
            bad += 1
            print('FAIL %s (%s):' % (name, '1.0.4' if rel else '1.0'))
            for f in fails:
                print('   not true:', f)
        else:
            print('OK: %s (engine %s): every object layout the port reads matches the engine' %
                  (name, '1.0.4' if rel else '1.0'))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
