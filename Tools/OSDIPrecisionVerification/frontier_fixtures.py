#!/usr/bin/env python3
"""Create exact ABI-1 source-decision falsifiers, independent of cover construction."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from verify_worker import NONE, blob, real, records, setup, phi_program, inactive_program
from verify_range import make_program


def equality_program(comparison=58):
    sections = [(8, records('II', [(1,0)]*5+[(3,0),(1,0)])),
        (16, records('IIQ', [(0,1,real(2.)),(1,1,real(7.)),(2,1,real(1.))])),
        (24, records('IIIIII', [(2,3,10,0,NONE,0)])),
        (24, records('IIIIII', [(i+1,i,i,1,0,0) for i in range(3)])),
        (40, records('IIIIIIIIII', [(0,0,0,0,0,0,NONE,NONE,NONE,0),
            (1,0,0,0,0,0,NONE,NONE,NONE,0),(2,0,3,0,0,0,NONE,NONE,NONE,0)])),
        (24, records('IIIIII', [(46,4,3,3,NONE,0),(comparison,5,4,0,NONE,0),(74,6,5,1,3,0)])),
        (16,b''), (24, records('IIIIII', [(2,1,6,0,NONE,0),(2,2,3,0,NONE,0),(2,3,2,0,0,0),(2,4,2,0,0,0)])),
        (32,b''), (16,records('IIII',[(0,0,1,0)])), (1,b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s',data,0,b'OSDIPR1\0',1,248,len(data),1,0,1,0,bytes(32))
    for i, item in enumerate(descriptors):struct.pack_into('<QII',data,72+16*i,*item)
    return bytes(data)


def correlated_invalid_program():
    # x<0 and x>=0 cannot both hold. No source feasibility lemma is supplied;
    # retaining the invalid selected formula must refuse the complete cover.
    types=[1,1,1,3,3,1,1,1]
    instructions=[(53,3,2,0,NONE,0),(55,4,2,0,NONE,0),
        (47,5,1,0,NONE,0),(74,6,4,5,1,0),(74,7,3,6,1,0)]
    blocks=[(0,0,0,0,0,0,NONE,NONE,NONE,0),
        (1,0,0,0,0,0,NONE,NONE,NONE,0),(2,0,len(instructions),0,0,0,NONE,NONE,NONE,0)]
    sections=[(8,records('II',[(kind,0) for kind in types])),
        (16,records('IIQ',[(0,1,real(0.)),(1,1,real(1.))])),
        (24,records('IIIIII',[(2,2,10,0,NONE,0)])),
        (24,records('IIIIII',[(i+1,i,i,1,0,0) for i in range(3)])),
        (40,records('IIIIIIIIII',blocks)),(24,records('IIIIII',instructions)),
        (16,b''),(24,records('IIIIII',[(2,1,7,0,NONE,0)])),
        (32,b''),(16,records('IIII',[(0,0,1,0)])),(1,b'x')]
    data,descriptors=blob(248,sections)
    struct.pack_into('<8sIIQIIII32s',data,0,b'OSDIPR1\0',1,248,len(data),1,0,1,0,bytes(32))
    for i,item in enumerate(descriptors):struct.pack_into('<QII',data,72+16*i,*item)
    return bytes(data)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    programs={'equality':equality_program(),'strict':equality_program(53),'nonstrict':equality_program(55),
        'correlated_invalid':correlated_invalid_program(),'loop':phi_program(),'inactive':inactive_program(False),
        'live_invalid':inactive_program(True)}
    for name in ('floor','ceil','cast','trunc','sqrt','divide'):
        programs[name]=make_program(name,time_input=False)
    manifest=[]
    for name, data in programs.items():
        pp=args.output/(name+'.program');sp=args.output/(name+'.setup');state=setup()
        pp.write_bytes(data);sp.write_bytes(state)
        manifest.append({'id':name,'programPath':str(pp.resolve()),'setupPath':str(sp.resolve()),
            'programSHA256':hashlib.sha256(data).hexdigest(),'setupSHA256':hashlib.sha256(state).hexdigest()})
    (args.output/'manifest.json').write_text(json.dumps({'schema':'osdi.decision-cover-fixtures.v1','fixtures':manifest},indent=2)+'\n')
    (args.output/'equality.va').write_text('''`include "disciplines.vams"
module equality_branch(inout x);
electrical x;
analog begin
    if (V(x)*V(x)==2.0) I(x)<+7.0;
    else I(x)<+V(x);
    I(x)<+ddt(V(x));
end
endmodule
''')


if __name__=='__main__':main()
