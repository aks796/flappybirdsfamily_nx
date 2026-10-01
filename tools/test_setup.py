#!/usr/bin/env python3
"""Host test for the console-side setup code (source/dcr_exefs.h, dcr_formats.h).

Compiles the two headers natively and checks them against Python references:

  1. exefs_build_override(fbf_nx.nsp, tid) is byte-identical to
     tools/make_exefs_override.py's exefs.nsp for the same title id, and its
     main.npdm names that title (ACI0 and the ACID range) and stays 32-bit;
  2. dex_names() over the APK's classes*.dex gives exactly the class names a
     Python dex reader lists (the wrapper's classes.txt);
  3. nro_romfs_file() finds the launcher NRO's fbf_nx.nsp / fbf_nx.build and
     they are the files it was built from; nro_build() reads the number.

    python3 tools/test_setup.py <the game's APK> [launcher/flappybirdsfamily_nx.nro]
"""
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import make_exefs_override as mko  # noqa: E402

HARNESS = r'''
#include "dcr_formats.h"
static unsigned char *slurp(const char *p, size_t *n) {
  FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
  unsigned char *b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(2); fclose(f); return b;
}
int main(int argc, char **argv) {
  if (!strcmp(argv[1], "override")) {        /* override <nsp> <tid hex> <out> */
    size_t n, on; unsigned char *d = slurp(argv[2], &n), *o;
    if (exefs_build_override(d, n, strtoull(argv[3], NULL, 16), &o, &on)) return 1;
    FILE *f = fopen(argv[4], "wb"); fwrite(o, 1, on, f); fclose(f); return 0;
  }
  if (!strcmp(argv[1], "forwarder")) {       /* forwarder <tid hex>: 1 / 0 */
    printf("%d\n", exefs_is_forwarder_tid(strtoull(argv[2], NULL, 16)));
    return 0;
  }
  if (!strcmp(argv[1], "dex")) {             /* dex <file>... : names, one per line */
    Names ns = {0};
    for (int i = 2; i < argc; i++) { size_t n; unsigned char *d = slurp(argv[i], &n); dex_names(d, n, &ns); }
    for (int i = 0; i < ns.n; i++) puts(ns.v[i]);
    return 0;
  }
  if (!strcmp(argv[1], "nro")) {             /* nro <nro> <name> <out> */
    FILE *f = fopen(argv[2], "rb"); long off; size_t size;
    if (nro_romfs_file(f, argv[3], &off, &size)) return 1;
    unsigned char *b = malloc(size); fseek(f, off, SEEK_SET);
    if (fread(b, 1, size, f) != size) return 1;
    FILE *o = fopen(argv[4], "wb"); fwrite(b, 1, size, o); fclose(o);
    printf("%llu\n", (unsigned long long)nro_build(f));
    return 0;
  }
  return 1;
}
'''


def dex_class_names(data):
    """Class descriptors defined by one .dex image, as JNI names (a/b/C$D)."""
    if data[:4] != b"dex\n":
        return []
    string_ids_off, = struct.unpack_from("<I", data, 0x3C)
    type_ids_off, = struct.unpack_from("<I", data, 0x44)
    class_defs_size, class_defs_off = struct.unpack_from("<II", data, 0x60)
    out = []
    for k in range(class_defs_size):
        type_idx, = struct.unpack_from("<I", data, class_defs_off + 32 * k)
        str_idx, = struct.unpack_from("<I", data, type_ids_off + 4 * type_idx)
        p, = struct.unpack_from("<I", data, string_ids_off + 4 * str_idx)
        while data[p] & 0x80:  # the uleb128 UTF-16 length
            p += 1
        p += 1
        desc = data[p:data.index(b"\0", p)].decode("utf-8", "replace")
        if desc.startswith("L") and desc.endswith(";"):
            out.append(desc[1:-1])
    return out


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    apk = sys.argv[1]
    nro = sys.argv[2] if len(sys.argv) == 3 else os.path.join(TOP, 'launcher', 'flappybirdsfamily_nx.nro')
    nsp = os.path.join(TOP, 'fbf_nx.nsp')
    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(t, 'h.c')
        open(src, 'w').write(HARNESS)
        exe = os.path.join(t, 'h')
        subprocess.check_call(['cc', '-O1', '-Wall', '-fsanitize=address,undefined', '-I',
                               os.path.join(TOP, 'source'), '-I', os.path.join(TOP, 'runtime', 'source'), '-DPORT_PAYLOAD_NAME="fbf_nx"',
                               '-o', exe, src])

        # 1. the override
        tid = 0x0571D2F4CB1CF000
        out = os.path.join(t, 'exefs.nsp')
        subprocess.check_call([exe, 'override', nsp, '%016X' % tid, out])
        files = mko.read_pfs0(open(nsp, 'rb').read())
        assert set(files) == {'main', 'main.npdm'}, 'the NSP holds %s' % sorted(files)
        files['main.npdm'] = mko.patch_npdm(files['main.npdm'], tid)[0]
        want = mko.write_pfs0(files)
        got = open(out, 'rb').read()
        assert got == want, 'exefs.nsp differs from make_exefs_override.py'
        npdm = mko.read_pfs0(got)['main.npdm']
        aci0, = struct.unpack_from('<I', npdm, 0x70)
        acid, = struct.unpack_from('<I', npdm, 0x78)
        assert struct.unpack_from('<Q', npdm, aci0 + 0x10)[0] == tid, 'ACI0 program id'
        assert struct.unpack_from('<QQ', npdm, acid + 0x210) == (tid, tid), 'ACID program id range'
        assert not (npdm[0xC] & 1), 'main.npdm must stay 32-bit'
        print('OK: override for %016X is byte-identical to make_exefs_override.py (%d bytes), '
              '32-bit, retargeted' % (tid, len(want)))
        for t_id, fw in ((tid, 1), (0x010000000000100D, 0), (0x0100152000022000, 0)):
            assert subprocess.check_output([exe, 'forwarder', '%016X' % t_id]).decode().strip() == str(fw)
        print('OK: only forwarder title ids (05...) are accepted')

        # 2. the class list
        dexes, want = [], set()
        with zipfile.ZipFile(apk) as z:
            for n in z.namelist():
                if n.startswith('classes') and n.endswith('.dex') and '/' not in n:
                    p = os.path.join(t, n)
                    d = z.read(n)
                    open(p, 'wb').write(d)
                    dexes.append(p)
                    want.update(dex_class_names(d))
        got = sorted(set(subprocess.check_output([exe, 'dex'] + dexes).decode().split('\n')) - {''})
        assert got == sorted(want), 'class names differ: %d vs %d' % (len(got), len(want))
        assert 'com/dotgears/dot_JNILib' in got and 'com/dotgears/game/GameActivity' in got
        print('OK: %d Java class names from %d dex file(s), dot_JNILib and GameActivity among them'
              % (len(got), len(dexes)))

        # 3. the NRO's payload
        if os.path.exists(nro):
            for name, ref in (('fbf_nx.nsp', nsp), ('fbf_nx.build', os.path.join(TOP, 'fbf_nx.build'))):
                p = os.path.join(t, name)
                b = subprocess.check_output([exe, 'nro', nro, name, p]).decode().strip()
                assert open(p, 'rb').read() == open(ref, 'rb').read(), name + ' in the NRO differs'
            assert b == open(os.path.join(TOP, 'fbf_nx.build')).read().strip(), 'build number'
            print('OK: %s carries fbf_nx.nsp and build %s' % (os.path.basename(nro), b))
        else:
            print('(no launcher NRO at %s: skipped)' % nro)


if __name__ == '__main__':
    main()
