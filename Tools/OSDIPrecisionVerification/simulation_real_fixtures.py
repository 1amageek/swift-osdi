#!/usr/bin/env python3
"""Bounded typed REAL lookup fixtures; source defaults remain explicit SSA."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from verify_worker import blob, records, real, NONE

PROFILE = hashlib.sha256(b'swift-osdi.fixture.typed-simulation-real.three-stages.v1').digest()
RAW_GIVEN_PROFILE = hashlib.sha256(b'swift-osdi.fixture.raw-scoped-given.three-stages.v1').digest()


def pack_program(types, constants, inputs, functions, blocks, instructions, roots,
                 parameters=(), raw=2, strings=b'tnomxy', profile=PROFILE):
    sections = [(8, records('II', [(value, 0) for value in types])),
                (16, records('IIQ', constants)), (24, records('IIIIII', inputs)),
                (24, records('IIIIII', functions)), (40, records('IIIIIIIIII', blocks)),
                (24, records('IIIIII', instructions)), (16, b''),
                (24, records('IIIIII', roots)), (32, records('IIIIIIQ', parameters)),
                (16, records('IIII', [(index, 4 + index, 1, 0) for index in range(raw)])),
                (1, strings)]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, raw, 0, profile)
    for index, descriptor in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * index, *descriptor)
    return bytes(data)


def program(legacy=False, bad_value=False, bad_given=False):
    types = [1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1]
    value, given = (8, 12) if legacy else (13, 14)
    inputs = [(0, 3, given, 0, 4, 0), (0, 4, value, 0, 4, 0),
              (1, 7, given, 0, 4, 0), (1, 8, value, 0, 4, 0), (1, 9, 11, 0, 0, 0),
              (2, 12, given, 0, 4, 0), (2, 13, value, 0, 4, 0),
              (2, 14, 11, 1, 0, 0), (2, 15, 10, 0, NONE, 0)]
    if bad_value or bad_given:
        # No instruction consumes this register: only the new input grammar's
        # destination check can reject this mismatch during cold admission.
        inputs.append((2, len(types), 13 if bad_value else 14, 0, 4, 0))
        types.append(3 if bad_value else 1)
    instructions = [(74, 5, 3, 4, 0, 0), (54, 6, 5, 1, NONE, 0),
                    (73, NONE, 2, NONE, NONE, 0),
                    (74, 10, 7, 8, 0, 0), (44, 11, 9, 10, NONE, 0),
                    (74, 16, 12, 13, 0, 0), (44, 17, 14, 16, NONE, 0), (46, 18, 17, 15, NONE, 0)]
    blocks = [(0, 0, 2, 0, 0, 2, 6, 1, 2, 0),
              (0, 2, 0, 0, 0, 0, NONE, NONE, NONE, 0),
              (0, 2, 1, 0, 0, 0, NONE, NONE, NONE, 0),
              (1, 3, 2, 0, 0, 0, NONE, NONE, NONE, 0),
              (2, 5, 3, 0, 0, 0, NONE, NONE, NONE, 0)]
    functions = [(1, 0, 0, 3, 0, 0), (2, 3, 3, 1, 0, 0), (3, 4, 4, 1, 0, 0)]
    roots = [(0, 5, 5, 0, 1, 0), (1, 5, 11, 1, 2, 0)]
    for kind in (1, 2):
        roots += [(2, kind, 18, 0, NONE, 0), (2, kind, 15, 1, NONE, 0)]
    for kind in (3, 4):
        roots += [(2, kind, 17, 0, 0, 0), (2, kind, 2, 1, 0, 0)]
    parameters = [(0, scope, 1, 0, 4, 0, 0) for scope in (1, 2)]
    return pack_program(types, [(0, 1, real(300.15)), (1, 1, real(0)), (2, 1, real(1))],
                        inputs, functions, blocks, instructions, roots, parameters)


def raw_given_program(invalid=None):
    types = [1, 1, 1, 3, 1, 1, 3, 1, 1, 1, 3, 3, 3] + [1] * 10
    inputs = [(0, 3, 15, 0, 0, 0), (0, 4, 1, 0, NONE, 0),
              (1, 6, 15, 1, 0, 0), (1, 7, 1, 1, NONE, 0), (1, 8, 11, 0, 0, 0),
              (2, 10, 15, 0, 0, 0), (2, 11, 15, 1, 0, 0), (2, 12, 2, 1, 0, 0),
              (2, 20, 10, 0, NONE, 0)]
    if invalid:
        destination = len(types)
        types.append(1 if invalid == 'type' else 3)
        inputs.append((0 if invalid == 'stage' else 2, destination, 15,
                       2 if invalid == 'index' else 1, 1 if invalid == 'aux' else 0, 0))
    instructions = [(74, 5, 3, 4, 0, 0), (74, 9, 6, 7, 8, 0),
                    (74, 13, 10, 1, 0, 0), (74, 14, 11, 2, 1, 0),
                    (74, 15, 11, 1, 0, 0), (74, 16, 12, 1, 0, 0),
                    (44, 17, 13, 14, NONE, 0), (44, 18, 17, 15, NONE, 0),
                    (44, 19, 18, 16, NONE, 0), (46, 21, 19, 20, NONE, 0), (46, 22, 16, 20, NONE, 0)]
    roots = [(0, 5, 5, 0, 1, 0), (1, 5, 9, 1, 2, 0),
             (2, 1, 21, 0, NONE, 0), (2, 1, 20, 1, NONE, 0),
             (2, 2, 22, 0, NONE, 0), (2, 2, 20, 1, NONE, 0),
             (2, 3, 19, 0, 0, 0), (2, 3, 1, 1, 0, 0),
             (2, 4, 16, 0, 0, 0), (2, 4, 1, 1, 0, 0)]
    return pack_program(types, [(index, 1, real(index)) for index in range(3)], inputs,
        [(index + 1, index, index, 1, 0, 0) for index in range(3)],
        [(stage, start, count, 0, 0, 0, NONE, NONE, NONE, 0)
         for stage, start, count in [(0, 0, 1), (1, 1, 1), (2, 2, 9)]],
        instructions, roots, [(0, scope, 1, 0, 4, 0, 0) for scope in (1, 2)],
        strings=b'gainxy', profile=RAW_GIVEN_PROFILE)


def presence_program(legacy):
    blocks = [(index, 0, 1 if index == 2 else 0, 0, 0, 0, NONE, NONE, NONE, 0) for index in range(3)]
    return pack_program([3, 1], [], [(2, 0, 12 if legacy else 14, 0, 4, 0)],
                        [(index + 1, index, index, 1, 0, 0) for index in range(3)],
                        blocks, [(11, 1, 0, NONE, NONE, 0)], [(2, 1, 1, 0, NONE, 0)])


def cold_program():
    # The ordinary library remains the existing analytic F=x,Q=0 oracle. Typed
    # queries in all stages test immutable fact capture, not model physics.
    inputs = [(stage, 2 * stage + 3, 14, 0, 4, 0) for stage in range(3)]
    inputs += [(stage, 2 * stage + 4, 13, 0, 4, 0) for stage in range(3)]
    inputs += [(2, 9, 10, 0, NONE, 0)]
    return pack_program([1, 1, 1, 3, 1, 3, 1, 3, 1, 1],
                        [(0, 1, real(0)), (1, 1, real(1)), (2, 1, real(300.15))], inputs,
                        [(index + 1, index, index, 1, 0, 0) for index in range(3)],
                        [(index, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0) for index in range(3)], [],
                        [(2, kind, value, 0, NONE if kind < 3 else 0, 0)
                         for kind, value in enumerate((9, 0, 1, 0), 1)], raw=1)


def setup(facts, parameter_facts=None):
    strings = bytearray()
    entries = []
    for name, kind, given, value in facts:
        offset = len(strings)
        encoded = name.encode('utf8')
        strings.extend(encoded)
        if kind == 4:
            text = value.encode('utf8')
            bits = len(strings) | (len(text) << 32)
            strings.extend(text)
        else:
            bits = real(value)
        entries.append((offset, len(encoded), kind, given, bits))
    if parameter_facts is None:
        parameter_facts = [(index, 0, 0) for index in range(2)]
    data, descriptors = blob(96, [(16, records('IIQ', parameter_facts)),
        (24, records('IIIIQ', entries)), (4, records('I', [(0,), (1,)])), (1, bytes(strings))])
    struct.pack_into('<IIQQII', data, 0, 1, 96, len(data), real(300.15), 2, 2)
    for index, descriptor in enumerate(descriptors):
        struct.pack_into('<QII', data, 32 + 16 * index, *descriptor)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = [('missing', program(), [], 300.15),
             ('string', program(), [('tnom', 4, 1, 'x')], 300.15),
             ('real', program(), [('tnom', 1, 1, 2)], 2),
             ('tiny-real', program(), [('tnom', 1, 1, 2 ** -100)], 2 ** -100),
             ('real-zero', program(), [('tnom', 1, 1, 0)], None),
             ('case-different', program(), [('TNOM', 1, 1, 7)], 300.15),
             ('nongiven-real', program(), [('tnom', 1, 0, 0)], 300.15),
             ('legacy-string', program(legacy=True), [('tnom', 4, 1, 'x')], None),
             ('legacy-real', program(legacy=True), [('tnom', 1, 1, 2)], 2),
             ('duplicate', program(), [('tnom', 1, 1, 2), ('tnom', 4, 1, 'x')], None),
             ('bad-value-type', program(bad_value=True), [], None),
             ('bad-given-type', program(bad_given=True), [], None),
             ('legacy-presence-string', presence_program(True), [('tnom', 4, 1, 'x')], None),
             ('typed-presence-string', presence_program(False), [('tnom', 4, 1, 'x')], None)]
    entries = []
    for name, code, facts, scalar in cases:
        setup_bytes = setup(facts)
        if 'presence-' in name:
            # Presence-only programs have no parameter table.
            data = bytearray(setup_bytes)
            struct.pack_into('<I', data, 40, 0)
            setup_bytes = bytes(data)
        program_path = args.output / (name + '.program')
        setup_path = args.output / (name + '.setup')
        program_path.write_bytes(code)
        setup_path.write_bytes(setup_bytes)
        entries.append({'id': name, 'programPath': str(program_path.resolve()), 'setupPath': str(setup_path.resolve()),
                        'programSHA256': hashlib.sha256(code).hexdigest(), 'setupSHA256': hashlib.sha256(setup_bytes).hexdigest(),
                        'scalar': scalar})
    raw_cases = [('raw-given-model-only', 1, 0, 0, 3),
                 ('raw-given-instance-same', 1, 1, 1, 5),
                 ('raw-given-instance-zero', 1, 1, 0, 5),
                 ('raw-given-neither', 0, 0, 0, 1)]
    for name, model_given, instance_given, value, expected in raw_cases:
        code = raw_given_program()
        setup_bytes = setup([], [(0, model_given, real(1) if model_given else 0),
                                 (1, instance_given, real(value) if instance_given else 0)])
        program_path = args.output / (name + '.program')
        setup_path = args.output / (name + '.setup')
        program_path.write_bytes(code); setup_path.write_bytes(setup_bytes)
        entries.append({'id': name, 'programPath': str(program_path.resolve()), 'setupPath': str(setup_path.resolve()),
            'programSHA256': hashlib.sha256(code).hexdigest(), 'setupSHA256': hashlib.sha256(setup_bytes).hexdigest(),
            'scalar': None, 'expectedF': expected, 'semanticProfileSHA256': RAW_GIVEN_PROFILE.hex()})
    for invalid in ['type', 'index', 'aux', 'stage']:
        name = 'raw-given-invalid-' + invalid
        code, setup_bytes = raw_given_program(invalid), setup([])
        program_path = args.output / (name + '.program'); setup_path = args.output / (name + '.setup')
        program_path.write_bytes(code); setup_path.write_bytes(setup_bytes)
        entries.append({'id': name, 'programPath': str(program_path.resolve()), 'setupPath': str(setup_path.resolve()),
            'programSHA256': hashlib.sha256(code).hexdigest(), 'setupSHA256': hashlib.sha256(setup_bytes).hexdigest(),
            'scalar': None, 'semanticProfileSHA256': RAW_GIVEN_PROFILE.hex()})
    raw_program = raw_given_program()
    (args.output / 'RawGivenProgram.h').write_text('/* Generated scoped-given analytic fixture. */\n'
        + 'static const unsigned char precision_program[] = {' + ','.join(map(str, raw_program)) + '};\n')
    cold = cold_program()
    (args.output / 'PrecisionProgram.h').write_text('/* Generated snapshot-only analytic fixture. */\n'
        + 'static const unsigned char precision_program[] = {' + ','.join(map(str, cold)) + '};\n')
    (args.output / 'cold.program').write_bytes(cold)
    manifest = {'schema': 'osdi.typed-simulation-real-fixtures.v1', 'semanticProfileSHA256': PROFILE.hex(),
                'coldProgramSHA256': hashlib.sha256(cold).hexdigest(),
                'rawGivenProgramSHA256': hashlib.sha256(raw_program).hexdigest(),
                'rawGivenSemanticProfileSHA256': RAW_GIVEN_PROFILE.hex(), 'fixtures': entries}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
