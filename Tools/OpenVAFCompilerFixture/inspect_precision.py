#!/usr/bin/env python3
"""Extract bounded optional precision programs from an exact trusted model image."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

SECTIONS = [('registers', 8), ('constants', 16), ('inputs', 24), ('functions', 24),
            ('blocks', 40), ('instructions', 24), ('phi_edges', 16), ('roots', 24),
            ('parameters', 32), ('nodes', 16), ('strings', 1)]

class Export(ctypes.Structure):
    _fields_ = [('version', ctypes.c_uint32), ('record_bytes', ctypes.c_uint32),
                ('descriptor_index', ctypes.c_uint32), ('reserved', ctypes.c_uint32),
                ('program_bytes', ctypes.c_uint64), ('program', ctypes.c_void_p)]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--maximum-program-bytes', type=int, default=64 * 1024 * 1024)
    args = parser.parse_args()
    library = args.library.resolve()
    identity = sha(library.read_bytes())
    if identity != args.expected_sha256:
        raise ValueError('Model artifact identity mismatch')
    if args.maximum_program_bytes < 248 or args.maximum_program_bytes > 256 * 1024 * 1024:
        raise ValueError('Program byte bound must be 248...268435456')
    args.output.mkdir(parents=True, exist_ok=False)
    image = ctypes.CDLL(str(library))
    count = ctypes.c_uint32.in_dll(image, 'OSDI_PRECISION_V1_COUNT').value
    if not 1 <= count <= 64:
        raise ValueError('Invalid descriptor program count')
    table = (Export * count).in_dll(image, 'OSDI_PRECISION_V1')
    records = []
    for record in table:
        if record.version != 1 or record.record_bytes != ctypes.sizeof(Export) or record.reserved:
            raise ValueError('Unsupported precision export record')
        if not 248 <= record.program_bytes <= args.maximum_program_bytes or not record.program:
            raise ValueError('Program byte count exceeds admitted bound')
        data = ctypes.string_at(record.program, record.program_bytes)
        if data[:8] != b'OSDIPR1\0' or struct.unpack_from('<IIQ', data, 8) != (1, 248, len(data)):
            raise ValueError('Invalid precision program header')
        flags, descriptor, nodes, states = struct.unpack_from('<IIII', data, 24)
        if flags != 1 or states != 0 or descriptor != record.descriptor_index:
            raise ValueError('Inconsistent precision program descriptor')
        sections = {}
        previous_end = 248
        for index, (name, expected_stride) in enumerate(SECTIONS):
            offset, items, stride = struct.unpack_from('<QII', data, 72 + index * 16)
            end = offset + items * stride
            if stride != expected_stride or offset < previous_end or end > len(data):
                raise ValueError('Invalid precision section geometry: ' + name)
            sections[name] = {'offset': offset, 'count': items, 'stride': stride}
            previous_end = end
        ins = sections['instructions']
        opcodes = sorted({struct.unpack_from('<I', data, ins['offset'] + i * ins['stride'])[0]
                          for i in range(ins['count'])})
        output = args.output / ('descriptor-' + str(descriptor) + '.program')
        output.write_bytes(data)
        records.append({'descriptorIndex': descriptor, 'programPath': str(output.resolve()),
                        'sha256': sha(data), 'bytes': len(data), 'rawNodeCount': nodes,
                        'semanticProfileSHA256': data[40:72].hex(), 'sections': sections,
                        'opcodes': opcodes})
    receipt = {'schema': 'openvaf.precision-program-inspection.v1',
               'library': str(library), 'librarySHA256': identity, 'programs': records}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))

if __name__ == '__main__':
    main()
