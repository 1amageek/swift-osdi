#!/usr/bin/env python3
"""Capture exact singleton chart requests using the existing verified Control ABI."""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import verify_worker
from verify_worker import Control, Context


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def vector(record, count):
    bits, values = record['bitPatterns'], record['values']
    require(len(bits) == len(values) == count, 'Coordinate dimension mismatch')
    for encoded, value in zip(bits, values):
        require(isinstance(encoded, int) and 0 <= encoded < 2**64, 'Invalid binary64 bits')
        require(math.isfinite(value), 'Nonfinite coordinate')
        require(struct.pack('<Q', encoded) == struct.pack('<d', value), 'Coordinate value/bits mismatch')
    return struct.pack('<' + 'Q'*count, *bits), values


def context(record):
    modes = {'transient': 2, 'transientInitialization': 3}
    require(record['mode'] in modes, 'Unsupported source context mode')
    high, low = record['timeHigh'], record['timeLow']
    for name in ('timeHigh', 'timeLow'):
        require(struct.pack('<d', record[name]) == struct.pack('<Q', record[name+'Bits']),
                'Context value/bits mismatch')
    require(math.isfinite(high) and math.isfinite(low) and high+low == high,
            'Invalid normalized time pair')
    require(high >= 0 and (high != 0 or low >= 0), 'Negative absolute time')
    mode = modes[record['mode']]
    require(mode != 3 or (high == 0 and low == 0), 'Invalid initialization time')
    return Context(mode, 0, high, low)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--arena-bytes', required=True, type=int)
    parser.add_argument('--maximum-work-units', required=True, type=int)
    parser.add_argument('--maximum-steps', required=True, type=int)
    parser.add_argument('--guard-capacity', required=True, type=int)
    args = parser.parse_args()
    require(sys.byteorder == 'little' and C.sizeof(Control) == 464 and C.sizeof(Context) == 24,
            'Unsupported Control ABI host')
    for value in (args.arena_bytes, args.maximum_work_units, args.maximum_steps, args.guard_capacity):
        require(0 < value < 2**64, 'Explicit positive bounded resource policy is required')
    manifest_path = args.manifest.resolve(strict=True)
    inputs = {}
    def capture(path, expected=None):
        path = path.resolve(strict=True)
        sha = digest(path)
        require(expected is None or sha == expected, 'Exact input SHA mismatch: ' + str(path))
        inputs[str(path)] = sha
        return path
    capture(manifest_path)
    parent = json.loads(manifest_path.read_text())
    entries = parent['families']
    require({entry['family'] for entry in entries} == {'bulk', 'cmg', 'img', 'soi101'} and len(entries) == 4,
            'Manifest must identify exactly current BULK/CMG/IMG/SOI101 inputs')
    payloads, receipts = {}, []
    for entry in entries:
        family = entry['family']
        program = capture(manifest_path.parent / (family+'.program'), entry['programSHA256'])
        setup = capture(manifest_path.parent / (family+'.setup'), entry['setupSHA256'])
        coordinates_path = capture(Path(entry['coordinatePath']))
        point_path = capture(Path(entry['pointRequestPath']), entry['pointRequestSHA256'])
        coordinates = json.loads(coordinates_path.read_text())
        point = json.loads(point_path.read_text())
        for source in entry['sourceFiles']:
            capture(Path(source['path']), source['sha256'])
        capture(Path(entry['imagePath']), entry['imageSHA256'])
        require(coordinates['activeUnknownCount'] == entry['activeUnknownCount'], 'Active coordinate mismatch')
        require(len(coordinates['rawNodes']) == entry['rawNodeCount'], 'Raw coordinate mismatch')
        require(coordinates['stateCount'] == 0, 'Stateful model is unsupported')
        count = entry['activeUnknownCount']
        high_bytes, high = vector(coordinates['centerHigh'], count)
        low_bytes, low = vector(coordinates['centerLow'], count)
        require(all(a+b == a for a, b in zip(high, low)), 'Nonnormalized coordinate pair')
        control = Control()
        control.magic = 0x313056504944534f
        control.version = 2
        control.headerBytes = C.sizeof(Control)
        control.byteOrder = 0x01020304
        control.descriptor = point['model']['descriptorIndex']
        control.active = count
        control.raw = entry['rawNodeCount']
        control.arena = args.arena_bytes
        control.minBits = point['options']['minimumPrecisionBits']
        control.maxBits = point['options']['maximumPrecisionBits']
        control.bits = point['precisionBits']
        require(2 <= control.minBits <= control.bits <= control.maxBits, 'Invalid source precision policy')
        control.maxWork = args.maximum_work_units
        control.candidate = control.reference = context(point['candidateContext'])
        for index, byte in enumerate(bytes.fromhex(entry['programSHA256'])):
            control.programSha[index] = byte
        # This is the established direct-test request format, not an IPC slab.
        # Region offsets and worker identity remain unset; the current test
        # executable/source hashes belong to its separate build/run receipt.
        payload = bytes(control) + high_bytes + low_bytes + high_bytes + low_bytes
        name = family+'.request.bin'
        payloads[name] = payload
        receipts.append({'family': family, 'version': entry['version'],
                         'programPath': str(program), 'programSHA256': entry['programSHA256'],
                         'setupPath': str(setup), 'setupSHA256': entry['setupSHA256'],
                         'coordinatePath': str(coordinates_path), 'coordinateSHA256': digest(coordinates_path),
                         'pointRequestPath': str(point_path), 'pointRequestSHA256': digest(point_path),
                         'requestFile': name, 'requestSHA256': hashlib.sha256(payload).hexdigest(),
                         'activeUnknownCount': count, 'rawNodeCount': entry['rawNodeCount'],
                         'centerHighBits': coordinates['centerHigh']['bitPatterns'],
                         'centerLowBits': coordinates['centerLow']['bitPatterns'],
                         'context': point['candidateContext'], 'precisionBits': control.bits,
                         'scope': 'Exact biased singleton source formula record/replay input; not a circuit solution, guard cover, PVT calibration or physical proof.'})
    capture(Path(verify_worker.__file__))
    capture(Path(__file__))
    capture(Path(__file__).resolve().parents[2] / 'Sources/COSDI/include/OSDIPrecisionProtocol.h')
    for path, expected in inputs.items():
        require(digest(Path(path)) == expected, 'Input changed during capture: ' + path)
    args.output.mkdir(parents=True, exist_ok=False)
    for name, payload in payloads.items():
        (args.output / name).write_bytes(payload)
    receipt = {'schema': 'osdi.internal-chart-singleton-requests.v1',
               'scope': 'Input capture only; no chart/model execution or qualification is claimed.',
               'controlBytes': C.sizeof(Control), 'controlFields': [(name, getattr(Control, name).offset, C.sizeof(kind)) for name, kind in Control._fields_],
               'resourcePolicy': {'arenaBytes': args.arena_bytes, 'maximumWorkUnits': args.maximum_work_units,
                                  'maximumSteps': args.maximum_steps, 'guardCapacity': args.guard_capacity},
               'inputs': [{'path': path, 'sha256': sha} for path, sha in sorted(inputs.items())],
               'families': receipts}
    (args.output / 'manifest.json').write_text(json.dumps(receipt, indent=2, allow_nan=False)+'\n')
    print(json.dumps({'output': str(args.output.resolve()), 'families': [x['family'] for x in receipts]}))


if __name__ == '__main__':
    main()
