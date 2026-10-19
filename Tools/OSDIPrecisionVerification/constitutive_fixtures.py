#!/usr/bin/env python3
"""Emit exact source-root conservation and branch-scope falsifiers."""
import hashlib
import json
from pathlib import Path
import struct
from verify_worker import NONE, blob, records, real, setup, phi_program


def program(variant, raw=3):
    kinds, constants, instructions = [], [], []
    def register(kind=1):
        i = len(kinds); kinds.append(kind); return i
    def literal(value):
        i = register(); constants.append((i, 1, real(value))); return i
    def op(code, a, b=NONE, aux=NONE, kind=1):
        i = register(kind); instructions.append((code, i, a, b, aux, 0)); return i
    zero, one, two, three, five = [literal(x) for x in (0., 1., 2., 3., 5.)]
    x = register()
    nonlinear = op(46, x, x)
    nonlinear = op(46, nonlinear, x)
    base = op(44, nonlinear, x)
    negative = op(3, base)
    q0, q1 = base, negative
    if variant == 'branch':
        predicate = op(54, x, zero, kind=3)
        q1 = op(74, predicate, op(44, negative, one), negative)
    if variant == 'growth':
        huge = literal(2.**300)
        q0 = op(46, huge, base); q1 = op(3, q0)
    if variant == 'independent_atoms':
        # Distinct source occurrences are not assumed identical, even when the
        # numeric result agrees. Sound conservative atom identity is tested.
        q1 = op(3, op(44, x, op(46, op(46, x, x), x)))
    f = [op(46, two, base), op(46, three, base), op(3, op(46, five, base))]
    roots = [(2, 1, value, row, NONE, 0) for row, value in enumerate(f)]
    roots += [(2, 2, q0, 0, NONE, 0), (2, 2, q1, 1, NONE, 0)]
    # Q row 2 has no output root: the ABI requires exact zero.
    blocks = [(0,0,0,0,0,0,NONE,NONE,NONE,0), (1,0,0,0,0,0,NONE,NONE,NONE,0),
              (2,0,len(instructions),0,0,0,NONE,NONE,NONE,0)]
    sections = [(8,records('II',[(k,0) for k in kinds])), (16,records('IIQ',constants)),
        (24,records('IIIIII',[(2,x,10,0,NONE,0)])),
        (24,records('IIIIII',[(i+1,i,i,1,0,0) for i in range(3)])),
        (40,records('IIIIIIIIII',blocks)), (24,records('IIIIII',instructions)), (16,b''),
        (24,records('IIIIII',roots)), (32,b''),
        (16,records('IIII',[(i,0,1,0) for i in range(raw)])), (1,b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s',data,0,b'OSDIPR1\0',1,248,len(data),1,0,raw,0,bytes(32))
    for i,item in enumerate(descriptors):struct.pack_into('<QII',data,72+16*i,*item)
    return bytes(data)


def common_kernel_program():
    roots=[(2,2,value,row,NONE,0) for row,value in enumerate((1,5,2,6))]
    blocks=[(0,0,0,0,0,0,NONE,NONE,NONE,0),(1,0,0,0,0,0,NONE,NONE,NONE,0),
            (2,0,3,0,0,0,NONE,NONE,NONE,0)]
    sections=[(8,records('II',[(k,0) for k in (1,1,1,1,3,1,1)])),
        (16,records('IIQ',[(0,1,real(0.))])),
        (24,records('IIIIII',[(2,i+1,10,i,NONE,0) for i in range(3)])),
        (24,records('IIIIII',[(i+1,i,i,1,0,0) for i in range(3)])),
        (40,records('IIIIIIIIII',blocks)),
        (24,records('IIIIII',[(54,4,3,0,NONE,0),(74,5,4,2,1,0),(74,6,4,1,2,0)])),
        (16,b''),(24,records('IIIIII',roots)),(32,b''),
        (16,records('IIII',[(i,0,1,0) for i in range(4)])),(1,b'x')]
    data,descriptors=blob(248,sections)
    struct.pack_into('<8sIIQIIII32s',data,0,b'OSDIPR1\0',1,248,len(data),1,0,4,0,bytes(32))
    for i,item in enumerate(descriptors):struct.pack_into('<QII',data,72+16*i,*item)
    return bytes(data)


def public_probe_program():
    """Match write_probe_fixture(..., quotient=0) in the C source checker driver."""
    original = program('conservation')
    sections = [struct.unpack_from('<QII', original, 72+16*i) for i in range(11)]
    first = (len(original)+7)&~7
    registers, constants, inputs = sections[0][1]+7, sections[1][1]+1, sections[2][1]+6
    constant_at = first+8*registers
    input_at = constant_at+16*constants
    root_at = input_at+24*inputs
    data = bytearray(root_at+6*24);data[:len(original)] = original
    struct.pack_into('<Q',data,16,len(data))
    for ordinal,descriptor in [(0,(first,registers,8)),(1,(constant_at,constants,16)),
                               (2,(input_at,inputs,24)),(7,(root_at,6,24))]:
        struct.pack_into('<QII',data,72+16*ordinal,*descriptor)
    for ordinal,destination in [(0,first),(1,constant_at),(2,input_at)]:
        offset,count,stride=sections[ordinal];data[destination:destination+count*stride]=original[offset:offset+count*stride]
    truth=sections[0][1]+6
    struct.pack_into('<II',data,first+truth*8,3,0)
    struct.pack_into('<IIQ',data,constant_at+(constants-1)*16,truth,3,1)
    endpoints=[(0,NONE),(0,NONE),(NONE,0),(0,1),(1,0),(2,2)]
    for i,(a,b) in enumerate(endpoints):
        reg=sections[0][1]+i
        struct.pack_into('<II',data,first+reg*8,1,0)
        struct.pack_into('<IIIIII',data,input_at+(sections[2][1]+i)*24,2,reg,10,a,b,0)
        struct.pack_into('<IIIIII',data,root_at+i*24,2,1 if i<3 else 2,reg,i%3,NONE,0)
    return bytes(data)


def check_public_probe(output, c_fixtures):
    """Require exact parity with the independent original C fixture recipe."""
    result={}
    for suffix in ('program','setup'):
        generated=(output/('probe-distinct.'+suffix)).read_bytes()
        original=(c_fixtures/('probe-distinct.'+suffix)).read_bytes()
        if generated!=original:raise ValueError('Public probe differs from original C source fixture: '+suffix)
        result[suffix]=hashlib.sha256(generated).hexdigest()
    return result


def emit(output, public_probes=False):
    output.mkdir(parents=True, exist_ok=False)
    fixtures = {name: program(name) for name in ('conservation','branch','growth','independent_atoms')}
    fixtures['loop'] = phi_program()
    fixtures['common_kernel'] = common_kernel_program()
    fixtures['sparse1000'] = program('conservation', 1000)
    if public_probes:fixtures['probe-distinct']=public_probe_program()
    entries = []
    for name, data in fixtures.items():
        raw = 1 if name == 'loop' else 4 if name == 'common_kernel' else 1000 if name == 'sparse1000' else 3
        state = setup(raw)
        pp, sp = output/(name+'.program'), output/(name+'.setup')
        pp.write_bytes(data); sp.write_bytes(state)
        entries.append({'id':name,'programPath':str(pp.resolve()),'setupPath':str(sp.resolve()),
            'programSHA256':hashlib.sha256(data).hexdigest(),'setupSHA256':hashlib.sha256(state).hexdigest()})
    # Exact ABI-1 program with a sparse root inventory and 1000 raw coordinates.
    # The cover wire format would require gigabytes of dense leaf planes; the
    # driver must reject its declared host ceiling before any owned allocation.
    from verify_worker import Control
    control=Control();control.magic=0x313056504944534f;control.version=2;control.headerBytes=464
    control.byteOrder=0x01020304;control.active=control.raw=1000
    control.minBits=53;control.maxBits=control.bits=256
    control.candidate.mode=control.reference.mode=2
    control.arena=536870912;control.maxWork=1000000000
    (output/'sparse1000.request.bin').write_bytes(bytes(control)+bytes(4*1000*8))
    (output/'manifest.json').write_text(json.dumps({'schema':'osdi.constitutive-identity-fixtures.v1','fixtures':entries},indent=2)+'\n')


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path)
    parser.add_argument('--public-probes',action='store_true')
    args=parser.parse_args();emit(args.output,args.public_probes)
