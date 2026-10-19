#!/usr/bin/env python3
"""Emit the analytic F=x, Q=0, G=1, C=0 public-snapshot ABI source fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys


# Source semantics identifier; not the digest of the containing program bytes.
SEMANTIC_PROFILE = hashlib.sha256(b"swift-osdi.public-consumer.linear.F=x.Q=0.G=1.C=0.v1").digest()


def program():
    tools=Path(__file__).resolve().parents[1]/'OSDIPrecisionVerification'
    sys.path.insert(0,str(tools))
    try:
        from verify_worker import blob, records, real, NONE
        sections=[(8,records('II',[(1,0)]*3)),
            (16,records('IIQ',[(1,1,real(0.)),(2,1,real(1.))])),
            (24,records('IIIIII',[(2,0,10,0,NONE,0)])),
            (24,records('IIIIII',[(i+1,i,i,1,0,0) for i in range(3)])),
            (40,records('IIIIIIIIII',[(i,0,0,0,0,0,NONE,NONE,NONE,0) for i in range(3)])),
            (24,b''),(16,b''),(24,records('IIIIII',[(2,k,v,0,NONE if k<3 else 0,0)
                for k,v in enumerate((0,1,2,1),1)])),(32,b''),
            (16,records('IIII',[(0,0,1,0)])),(1,b'x')]
        data,descriptors=blob(248,sections)
        struct.pack_into('<8sIIQIIII32s',data,0,b'OSDIPR1\0',1,248,len(data),1,0,1,0,SEMANTIC_PROFILE)
        for i,item in enumerate(descriptors):struct.pack_into('<QII',data,72+16*i,*item)
        return bytes(data)
    finally:sys.path.pop(0)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    data=program();(args.output/'linear.program').write_bytes(data)
    (args.output/'manifest.json').write_text(json.dumps({'programSHA256':hashlib.sha256(data).hexdigest(),
        'semanticProfileSHA256':SEMANTIC_PROFILE.hex()},indent=2)+'\n')
    (args.output/'PrecisionProgram.h').write_text('/* Generated analytic source program; no runtime model state. */\n'
        +'static const unsigned char precision_program[] = {'+','.join(map(str,data))+'};\n')


if __name__=='__main__':main()
